use serde_json::{json, Value};
use std::fs;
use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};

struct Backend {
    process: Child,
    input: ChildStdin,
    output: BufReader<ChildStdout>,
    notifications: Vec<Value>,
}

impl Backend {
    fn start(config: &Path, music: Option<&Path>, index: Option<&Path>) -> Self {
        let mut command = Command::new(env!("CARGO_BIN_EXE_cloudmus-backend-local-folder"));
        command
            .env("XDG_CONFIG_HOME", config)
            .env("APPDATA", config)
            .env("XDG_CACHE_HOME", config)
            .env("LOCALAPPDATA", config)
            .env_remove("CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR");
        if let Some(root) = music {
            command.env("CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR", root);
        }
        let mut process = command
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .spawn()
            .unwrap();
        let input = process.stdin.take().unwrap();
        let output = BufReader::new(process.stdout.take().unwrap());
        let mut backend = Self {
            process,
            input,
            output,
            notifications: Vec::new(),
        };
        let result = backend.call("initialize", json!({"locale": "ru_RU.UTF-8"}));
        assert_eq!(result["source"]["id"], "local-folder");
        assert_eq!(result["capabilities"]["hostMedia"], true);
        assert_ne!(
            result["source"]["description"],
            "Music files on this device"
        );
        assert_eq!(backend.notification()["params"]["scope"], "own");
        if let Some(index) = index {
            backend.call("media.setLibrary", json!({"path": index, "revision": 1}));
        }
        backend
    }

    // The next notification, kept on the way or still to come.
    fn notification(&mut self) -> Value {
        if !self.notifications.is_empty() {
            return self.notifications.remove(0);
        }
        let mut line = String::new();
        self.output.read_line(&mut line).unwrap();
        let message: Value = serde_json::from_str(&line).unwrap();
        assert!(message.get("id").is_none(), "{message}");
        message
    }

    // The next response; notifications on the way are kept.
    fn recv(&mut self) -> Value {
        loop {
            let mut line = String::new();
            self.output.read_line(&mut line).unwrap();
            let message: Value = serde_json::from_str(&line).unwrap();
            if message.get("id").is_some() {
                return message;
            }
            self.notifications.push(message);
        }
    }

    fn send(&mut self, method: &str, params: Value) -> Value {
        writeln!(
            self.input,
            "{}",
            json!({"jsonrpc": "2.0", "id": 7, "method": method, "params": params})
        )
        .unwrap();
        self.recv()
    }

    fn call(&mut self, method: &str, params: Value) -> Value {
        let response = self.send(method, params);
        assert_eq!(response["id"], 7);
        assert!(response.get("result").is_some(), "{response}");
        response["result"].clone()
    }
}

impl Drop for Backend {
    fn drop(&mut self) {
        let _ = self.process.kill();
        let _ = self.process.wait();
    }
}

fn write_file(root: &Path, relative: &str, contents: &[u8]) -> PathBuf {
    let path = root.join(relative);
    fs::create_dir_all(path.parent().unwrap()).unwrap();
    fs::write(&path, contents).unwrap();
    path
}

#[test]
fn lists_nested_and_loose_tracks_and_resolves_webm() {
    let temp = tempfile::tempdir().unwrap();
    let music = temp.path().join("Музыка");
    let sample = write_file(
        &music,
        "Альбом/Live/Моя песня.WEBM",
        include_bytes!("data/sample.webm"),
    );
    write_file(&music, "Альбом/Bad.flac", b"bad tags");
    write_file(&music, "loose.mp3", b"");
    write_file(&music, "Ignored.mkv", b"");
    write_file(temp.path(), "outside/escape.flac", b"");
    #[cfg(unix)]
    std::os::unix::fs::symlink(temp.path().join("outside"), music.join("escape")).unwrap();

    let mut backend = Backend::start(temp.path(), Some(&music), None);
    let result = backend.call("catalog.listPlaylists", json!({}));
    let playlists = result["playlists"].as_array().unwrap();
    assert_eq!(playlists.len(), 2);
    assert_eq!(
        (
            playlists[0]["id"].as_str(),
            playlists[0]["trackCount"].as_i64()
        ),
        (Some("Альбом"), Some(2))
    );
    assert_eq!(playlists[1]["id"], "__root__");

    let result = backend.call("catalog.listTracks", json!({"playlistId": "Альбом"}));
    let tracks = result["tracks"].as_array().unwrap();
    assert_eq!(tracks[0]["title"], "Bad");
    assert!(tracks[0]["bitrateKbps"].is_null());
    assert_eq!(tracks[1]["id"], "Альбом/Live/Моя песня.WEBM");
    assert_eq!(tracks[1]["title"], "Моя песня");
    assert_eq!(tracks[1]["codec"], "opus");
    assert!(tracks[1]["durationMs"].as_u64().unwrap() > 250);
    assert!(tracks[1]["bitrateKbps"].as_u64().unwrap() > 0);

    let stream = backend.call(
        "playback.resolveStream",
        json!({"trackId": tracks[1]["id"]}),
    );
    assert_eq!(
        stream["stream"]["url"],
        url::Url::from_file_path(sample).unwrap().as_str()
    );
    assert_eq!(stream["stream"]["mimeType"], "audio/webm");
    assert_eq!(
        backend.call("playback.play", json!({"trackId": tracks[1]["id"]}))["accepted"],
        true
    );
    let notification = backend.notification();
    assert_eq!(notification["method"], "track/streamReady");
    assert_eq!(notification["params"]["requestId"], 7);
    assert_eq!(notification["params"]["stream"], stream["stream"]);
    assert_eq!(
        backend.send(
            "playback.resolveStream",
            json!({"trackId":"../outside/escape.flac"})
        )["error"]["code"],
        1300
    );
    assert_eq!(
        backend.send(
            "playback.resolveStream",
            json!({"trackId":"escape/escape.flac"})
        )["error"]["code"],
        1300
    );
}

#[test]
fn settings_preserve_legacy_music_folder_and_other_config() {
    let temp = tempfile::tempdir().unwrap();
    let music = temp.path().join("My Music");
    let replacement = temp.path().join("New Music");
    write_file(&music, "track.mp3", b"");
    fs::create_dir(&replacement).unwrap();
    let config = write_file(
        temp.path(),
        "cloudmus/backends/local-folder/config.json",
        json!({"musicDir": music, "other": true})
            .to_string()
            .as_bytes(),
    );

    let mut backend = Backend::start(temp.path(), None, None);
    let fields = backend.call("settings.describe", json!({}));
    assert_eq!(fields["fields"][0]["value"], json!(music));
    assert_eq!(
        backend.call("catalog.listPlaylists", json!({}))["playlists"][0]["trackCount"],
        1
    );
    assert_eq!(
        backend.send(
            "settings.update",
            json!({"values":{"musicDir": music.join("missing")}})
        )["error"]["data"]["key"],
        "musicDir"
    );
    backend.call(
        "settings.update",
        json!({"values":{"musicDir": replacement}}),
    );
    let stored: Value = serde_json::from_slice(&fs::read(config).unwrap()).unwrap();
    assert_eq!(
        stored,
        json!({"settings":{"musicDir": replacement}, "other": true})
    );
}

#[test]
fn android_includes_managed_roots_and_media_as_distinct_inputs() {
    let temp = tempfile::tempdir().unwrap();
    let library = temp.path().join("Shared");
    let saved = write_file(&library, "offline.mp3", b"");
    let published = write_file(
        temp.path(),
        "Music/published.webm",
        include_bytes!("data/sample.webm"),
    );
    let cover = url::Url::from_file_path(temp.path().join("cover.jpg")).unwrap();
    let index = write_file(temp.path(), "library.json", json!({
        "scope": "all",
        "folders": [{"id": "internal", "path": library}],
        "items": [
            {"id": "@android/media/external/audio/media/1", "own": true,
                "path": saved, "uri": "content://media/external/audio/media/1"},
            {"id": "@android/media/external/audio/media/2", "own": false,
                "title":"Device song", "mimeType":"audio/mp4", "uri":"content://media/external/audio/media/2"},
            {"id": "@android/media/external/audio/media/3", "own": true,
                "title":"Older saved song", "mimeType":"audio/webm", "uri":"content://media/external/audio/media/3",
                "path": published},
            {"id": "@android/media/external/audio/media/4", "own": true,
                "title":"Scoped song", "mimeType":"audio/ogg", "uri":"content://media/external/audio/media/4",
                "coverUrl": cover.as_str(), "codec": "opus", "bitrateKbps": 128}
        ]
    }).to_string().as_bytes());
    let mut backend = Backend::start(temp.path(), None, Some(&index));
    // Only what the app saved, until device music is switched on.
    let result = backend.call("catalog.listPlaylists", json!({}));
    assert_eq!(result["playlists"].as_array().unwrap().len(), 2);
    let fields = backend.call("settings.describe", json!({}))["fields"].clone();
    assert!(fields.as_array().unwrap().iter().any(|field| field["key"] == "deviceMusic"));
    backend.call("settings.update", json!({"values": {"deviceMusic": true}}));
    assert_eq!(backend.notification()["params"]["scope"], "all");
    let result = backend.call("catalog.listPlaylists", json!({}));
    assert_eq!(result["playlists"].as_array().unwrap().len(), 3);
    let result = backend.call(
        "catalog.listTracks",
        json!({"playlistId":"@android/internal/__root__"}),
    );
    let track = &result["tracks"][0];
    assert_eq!(track["id"], "@android/internal/offline.mp3");
    assert_eq!(
        backend.call("playback.resolveStream", json!({"trackId": track["id"]}))["stream"]
            ["mimeType"],
        "audio/mpeg"
    );
    let stream = backend.call(
        "playback.resolveStream",
        json!({"trackId":"@android/media/external/audio/media/2"}),
    );
    assert_eq!(
        stream["stream"]["url"],
        "content://media/external/audio/media/2"
    );
    let result = backend.call(
        "catalog.listTracks",
        json!({"playlistId":"@android/saved-media"}),
    );
    let tracks = result["tracks"].as_array().unwrap();
    assert_eq!(tracks.len(), 2);
    assert_eq!(tracks[0]["codec"], "opus");
    assert!(tracks[0]["bitrateKbps"].as_u64().unwrap() > 0);
    assert_eq!(tracks[1]["coverUrl"], cover.as_str());
    assert_eq!(tracks[1]["codec"], "opus");
    assert_eq!(tracks[1]["bitrateKbps"], 128);
    for track in tracks {
        let result = backend.call("playback.resolveStream", json!({"trackId": track["id"]}));
        assert_eq!(result["stream"]["codec"], track["codec"]);
        assert_eq!(result["stream"]["bitrateKbps"], track["bitrateKbps"]);
    }
}
