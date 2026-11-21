mod catalog;
mod config;
mod matroska;
mod metadata;

use serde_json::{json, Value};
use std::io::{self, BufRead, Write};
use std::sync::{mpsc, Arc, Mutex};

const DESCRIPTION: &str = "Music files on this device";
const LOCALES: [&str; 7] = ["en", "be", "de", "es", "fr", "it", "ru"];

fn select_locale(value: Option<&str>) -> &'static str {
    let language = value
        .unwrap_or("")
        .trim()
        .split(['-', '_', '.', '@'])
        .next()
        .unwrap_or("");
    LOCALES
        .iter()
        .copied()
        .find(|locale| locale.eq_ignore_ascii_case(language))
        .unwrap_or("en")
}

type RpcResult = Result<Value, Value>;

fn error(code: i32, message: &str) -> Value {
    json!({"code": code, "message": message})
}

fn dispatch(method: &str, params: &Value, tr: impl Fn(&str) -> String) -> RpcResult {
    let required = |name: &str| {
        params[name]
            .as_str()
            .ok_or_else(|| error(-32602, &format!("{name}: {}", tr("expected text"))))
    };
    let missing = |message, name, id| {
        let mut value = error(1300, &tr(message));
        value["data"] = json!({"retryable": false, "detail": format!("{name}={id}")});
        value
    };
    match method {
        "settings.describe" => Ok(config::describe(&tr)),
        "settings.update" => {
            let values = params["values"]
                .as_object()
                .ok_or_else(|| error(-32602, &tr("values: expected an object")))?;
            config::update(values, &tr).map_err(|(key, message)| {
                let mut reply = error(-32602, &message);
                reply["data"] = json!({"key": key, "message": message});
                reply
            })?;
            Ok(json!({}))
        }
        "catalog.listPlaylists" => Ok(catalog::playlists(&tr)),
        "catalog.listTracks" => {
            let id = required("playlistId")?;
            catalog::tracks(id, &tr).ok_or_else(|| missing("Playlist not found", "playlistId", id))
        }
        "playback.play" | "playback.resolveStream" => {
            let id = required("trackId")?;
            let stream =
                catalog::resolve(id).ok_or_else(|| missing("Track not found", "trackId", id))?;
            Ok(json!({"stream": stream}))
        }
        "playback.cancel" => Ok(json!({})),
        _ => Err(error(-32601, &format!("method not found: {method}"))),
    }
}

// How much of the host's media library to be given (docs/protocol.md §7.10).
fn media_wanted() -> Value {
    let scope = if config::device_music() { "all" } else { "own" };
    json!({"jsonrpc": "2.0", "method": "media/wanted", "params": {"scope": scope}})
}

fn reply(id: Value, result: RpcResult) -> Value {
    match result {
        Ok(result) => json!({"jsonrpc": "2.0", "id": id, "result": result}),
        Err(error) => json!({"jsonrpc": "2.0", "id": id, "error": error}),
    }
}

fn write_messages(messages: &[Value]) -> io::Result<()> {
    let mut out = io::stdout().lock();
    for message in messages {
        serde_json::to_writer(&mut out, message)?;
        out.write_all(b"\n")?;
    }
    out.flush()
}

fn handle(message: &Value, translate: impl Fn(&str) -> String) -> io::Result<()> {
    let method = message["method"].as_str().unwrap_or_default();
    let id = &message["id"];
    let result = dispatch(method, &message["params"], translate);
    if id.is_null() {
        return Ok(());
    }
    if method == "playback.play" {
        if let Ok(payload) = &result {
            let event = json!({"jsonrpc": "2.0", "method": "track/streamReady", "params": {
                "requestId": id, "trackId": message["params"]["trackId"], "stream": payload["stream"]
            }});
            return write_messages(&[reply(id.clone(), Ok(json!({"accepted": true}))), event]);
        }
    }
    write_messages(&[reply(id.clone(), result)])
}

fn main() -> io::Result<()> {
    let translations = Arc::new(
        serde_json::from_str::<Value>(include_str!("locales.json")).expect("bundled locales"),
    );
    let (sender, receiver) = mpsc::sync_channel::<(Value, &'static str)>(16);
    let receiver = Arc::new(Mutex::new(receiver));
    // A large catalog scan should not keep the front from resolving a track.
    for _ in 0..2 {
        let receiver = Arc::clone(&receiver);
        let translations = Arc::clone(&translations);
        std::thread::spawn(move || loop {
            let request = receiver.lock().unwrap().recv();
            let Ok((message, locale)) = request else {
                break;
            };
            if handle(&message, |key| {
                translations[locale][key].as_str().unwrap_or(key).to_owned()
            })
            .is_err()
            {
                break;
            }
        });
    }

    let mut locale = "en";
    for line in io::stdin().lock().lines() {
        let line = line?;
        if line.trim().is_empty() {
            continue;
        }
        let Ok(message) = serde_json::from_str::<Value>(&line) else {
            write_messages(&[reply(Value::Null, Err(error(-32700, "Parse error")))])?;
            continue;
        };
        let id = &message["id"];
        let params = &message["params"];
        let method = message["method"].as_str();
        if message["jsonrpc"] != "2.0"
            || method.is_none()
            || !(id.is_null() || id.is_number() || id.is_string())
        {
            write_messages(&[reply(Value::Null, Err(error(-32600, "Invalid request")))])?;
            continue;
        }
        let method = method.unwrap();
        let result = match method {
            "initialize" => {
                locale = select_locale(params["locale"].as_str());
                Ok(json!({
                    "protocolVersion": "1.20",
                    "source": {"id": "local-folder", "name": "Local Folder", "version": "0.1.0",
                        "description": translations[locale][DESCRIPTION].as_str().unwrap_or(DESCRIPTION)},
                    "capabilities": {
                        "playback": {"providesStream": true, "resolveStream": true, "selfPlayback": false,
                            "controls": {"pause": false, "seek": false, "volume": false}},
                        "browse": {"playlists": true, "likedTracks": false, "radio": false, "search": false},
                        "feedback": {"like": false, "dislike": false, "skip": false},
                        "download": false, "auth": {"required": false, "flow": "none"},
                        "network": false, "settings": true, "hostMedia": true,
                        "localization": {"locales": LOCALES}
                    }
                }))
            }
            "localization.setLanguage" => match params["locale"].as_str() {
                Some(value) => {
                    locale = select_locale(Some(value));
                    Ok(
                        json!({"locale": locale, "description": translations[locale][DESCRIPTION].as_str().unwrap_or(DESCRIPTION)}),
                    )
                }
                None => Err(error(-32602, "locale: expected text")),
            },
            "shutdown" => Ok(json!({})),
            "media.setLibrary" => match params["path"].as_str() {
                Some(path) if !path.is_empty() => {
                    config::set_host_library(path.into());
                    Ok(json!({}))
                }
                _ => Err(error(-32602, "path: expected text")),
            },
            "settings.describe" | "settings.update" => {
                handle(&message, |key| {
                    translations[locale][key].as_str().unwrap_or(key).to_owned()
                })?;
                // Device music may have been switched.
                if method == "settings.update" {
                    write_messages(&[media_wanted()])?;
                }
                continue;
            }
            _ => {
                if sender.send((message, locale)).is_err() {
                    break;
                }
                continue;
            }
        };
        if !id.is_null() {
            let ok = result.is_ok();
            write_messages(&[reply(id.clone(), result)])?;
            if ok && method == "initialize" {
                write_messages(&[media_wanted()])?;
            }
        }
        if method == "shutdown" {
            break;
        }
    }
    Ok(())
}
