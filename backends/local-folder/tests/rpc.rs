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
    // All of it first, then each folder with what's under it, a folder
    // inside another listed under it.
    let summary: Vec<_> = playlists
        .iter()
        .map(|list| {
            (
                list["id"].as_str().unwrap(),
                list["trackCount"].as_i64().unwrap(),
                list["parentId"].as_str(),
            )
        })
        .collect();
    assert_eq!(
        summary,
        [
            ("__wave__", 0, None),
            ("__root__", 3, None),
            ("Альбом", 2, None),
            ("Альбом/Live", 1, Some("Альбом"))
        ]
    );
    assert_eq!(playlists[0]["kind"], "radioStation");
    assert_ne!(playlists[1]["title"], "All music");

    let result = backend.call("catalog.listTracks", json!({"playlistId": "Альбом"}));
    let tracks = result["tracks"].as_array().unwrap();
    assert_eq!(tracks[0]["title"], "Bad");
    assert!(tracks[0]["bitrateKbps"].is_null());
    assert_eq!(tracks[1]["id"], "Альбом/Live/Моя песня.WEBM");
    assert_eq!(tracks[1]["title"], "Моя песня");
    assert_eq!(tracks[1]["codec"], "opus");
    assert!(tracks[1]["durationMs"].as_u64().unwrap() > 250);
    assert!(tracks[1]["bitrateKbps"].as_u64().unwrap() > 0);
    assert_eq!(tracks[1]["location"], sample.display().to_string());

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
        backend.call("catalog.listPlaylists", json!({}))["playlists"][1]["trackCount"],
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
    // Another app's file, which the app may read too.
    let device = write_file(
        temp.path(),
        "Download/device.webm",
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
                "title":"Device song", "mimeType":"audio/mp4", "uri":"content://media/external/audio/media/2",
                "path": device},
            {"id": "@android/media/external/audio/media/3", "own": true,
                "title":"Older saved song", "mimeType":"audio/webm", "uri":"content://media/external/audio/media/3",
                "path": published},
            {"id": "@android/media/external/audio/media/4", "own": true,
                "title":"Scoped song", "mimeType":"audio/ogg", "uri":"content://media/external/audio/media/4",
                "coverUrl": cover.as_str(), "codec": "opus", "bitrateKbps": 128},
            {"id": "@android/media/external/audio/media/5", "own": true,
                "title":"Nested song", "mimeType":"audio/mpeg", "uri":"content://media/external/audio/media/5",
                "relativePath": "Music/CloudMus/Artist/Album/"}
        ]
    }).to_string().as_bytes());
    let mut backend = Backend::start(temp.path(), None, Some(&index));
    // Only what the app saved, until device music is switched on.
    let result = backend.call("catalog.listPlaylists", json!({}));
    let lists = result["playlists"].as_array().unwrap();
    let summary: Vec<_> = lists
        .iter()
        .map(|list| (list["id"].as_str().unwrap(), list["parentId"].as_str()))
        .collect();
    assert_eq!(
        summary,
        [
            ("__wave__", None),
            ("@android/saved-media", None),
            ("@android/saved-media/Artist", Some("@android/saved-media")),
            ("@android/saved-media/Artist/Album", Some("@android/saved-media/Artist"))
        ]
    );
    assert_eq!(lists[3]["title"], "Album");
    let fields = backend.call("settings.describe", json!({}))["fields"].clone();
    assert!(fields.as_array().unwrap().iter().any(|field| field["key"] == "deviceMusic"));
    backend.call("settings.update", json!({"values": {"deviceMusic": true}}));
    assert_eq!(backend.notification()["params"]["scope"], "all");
    let result = backend.call("catalog.listPlaylists", json!({}));
    let ids: Vec<_> = result["playlists"]
        .as_array()
        .unwrap()
        .iter()
        .map(|list| list["id"].as_str().unwrap())
        .collect();
    assert_eq!(
        ids,
        [
            "__wave__",
            "@android/saved-media",
            "@android/saved-media/Artist",
            "@android/saved-media/Artist/Album",
            "@android/device-media"
        ]
    );
    let result = backend.call(
        "catalog.listTracks",
        json!({"playlistId":"@android/device-media"}),
    );
    assert_eq!(result["tracks"][0]["title"], "Device song");
    assert_eq!(result["tracks"][0]["codec"], "opus");
    let result = backend.call(
        "catalog.listTracks",
        json!({"playlistId":"@android/saved-media"}),
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
    // The app's folder's file, then what MediaStore has of the app's.
    assert_eq!(tracks.len(), 4);
    assert_eq!(tracks[0]["id"], "@android/internal/offline.mp3");
    assert_eq!(tracks[1]["codec"], "opus");
    assert!(tracks[1]["bitrateKbps"].as_u64().unwrap() > 0);
    assert_eq!(tracks[2]["coverUrl"], cover.as_str());
    assert_eq!(tracks[2]["codec"], "opus");
    assert_eq!(tracks[2]["bitrateKbps"], 128);
    assert_eq!(tracks[3]["title"], "Nested song");
    assert_eq!(tracks[3]["location"], "Music/CloudMus/Artist/Album/");
    assert_eq!(tracks[1]["location"], published.display().to_string());
    for track in &tracks[..3] {
        let result = backend.call("playback.resolveStream", json!({"trackId": track["id"]}));
        assert_eq!(result["stream"]["codec"], track["codec"]);
        assert_eq!(result["stream"]["bitrateKbps"], track["bitrateKbps"]);
    }
}

#[test]
fn deletes_tracks_and_folders_and_hands_back_host_items() {
    let temp = tempfile::tempdir().unwrap();
    let music = temp.path().join("Music");
    let single = write_file(&music, "Artist/Single/one.mp3", b"");
    write_file(&music, "Artist/Album/two.mp3", b"");
    write_file(&music, "Artist/Album/three.mp3", b"");
    write_file(&music, "Other/four.mp3", b"");
    let index = write_file(temp.path(), "library.json", json!({
        "scope": "own", "folders": [],
        "items": [{"id": "@android/media/external/audio/media/9", "own": true, "title": "Saved",
            "uri": "content://media/external/audio/media/9", "relativePath": "Music/CloudMus/"}]
    }).to_string().as_bytes());
    let mut backend = Backend::start(temp.path(), Some(&music), Some(&index));
    let lists = backend.call("catalog.listPlaylists", json!({}))["playlists"].clone();
    let deletable = |id: &str| {
        lists.as_array().unwrap().iter().find(|list| list["id"] == id).unwrap()["deletable"] == true
    };
    assert!(!deletable("__root__"));
    assert!(!deletable("@android/saved-media"));
    assert!(deletable("Artist/Album"));

    // One track: its folder, left empty, goes with it; the parent stays.
    let result = backend.call("catalog.deleteTracks", json!({"trackIds": ["Artist/Single/one.mp3", "missing.mp3"]}));
    assert_eq!(result["deleted"], json!(["Artist/Single/one.mp3"]));
    assert_eq!(result["failed"][0]["trackId"], "missing.mp3");
    assert!(!single.exists() && !music.join("Artist/Single").exists());
    assert!(music.join("Artist").is_dir());

    // A folder with what's in it.
    let result = backend.call("catalog.deletePlaylist", json!({"playlistId": "Artist"}));
    assert_eq!(result["deleted"].as_array().unwrap().len(), 2);
    assert!(!music.join("Artist").exists());
    assert!(music.join("Other/four.mp3").exists());
    assert_eq!(
        backend.send("catalog.deletePlaylist", json!({"playlistId": "__root__"}))["error"]["code"],
        1300
    );

    // What the front's library keeps, the front deletes.
    let result = backend.call(
        "catalog.deleteTracks",
        json!({"trackIds": ["@android/media/external/audio/media/9"]}),
    );
    assert_eq!(result["host"][0]["uri"], "content://media/external/audio/media/9");
    assert_eq!(result["deleted"], json!([]));
}

#[test]
fn the_wave_plays_everything_once_and_learns_from_skips() {
    let temp = tempfile::tempdir().unwrap();
    let music = temp.path().join("Music");
    let mut all = Vec::new();
    for folder in ["A/One", "A/Two", "B/Three"] {
        for n in 0..6 {
            write_file(&music, &format!("{folder}/{n}.mp3"), b"");
            all.push(format!("{folder}/{n}.mp3"));
        }
    }
    let mut backend = Backend::start(temp.path(), Some(&music), None);
    let ids = |tracks: &Value| -> Vec<String> {
        tracks.as_array().unwrap().iter().map(|track| track["id"].as_str().unwrap().to_owned()).collect()
    };

    // Not what was played lately, while there's other music.
    let recent = &all[..4];
    let result = backend.call("catalog.startRadio", json!({"seed": "__wave__", "recentTrackIds": recent}));
    assert_eq!(result["stationId"], "wave");
    let first = ids(&result["initialTracks"]);
    assert_eq!(first.len(), 10);
    assert!(first.iter().all(|id| !recent.contains(id)));

    // Near the end of the queue it sends more, none it has sent.
    backend.call("feedback.trackStarted", json!({"trackId": first[8], "queuedAfter": 1}));
    let event = backend.notification();
    assert_eq!(event["method"], "radio/tracksAdded");
    assert!(event["params"].get("replaceUpcoming").is_none());
    let more = ids(&event["params"]["tracks"]);
    assert_eq!(more.len(), 4);
    assert!(more.iter().all(|id| !first.contains(id)));

    // A skip: what's queued is picked anew.
    backend.call("feedback.skip", json!({"trackId": more[0], "playedMs": 1000}));
    let event = backend.notification();
    assert_eq!(event["params"]["replaceUpcoming"], true);
    assert!(!ids(&event["params"]["tracks"]).contains(&more[0]));
    // Late, it changes nothing queued.
    backend.call("feedback.skip", json!({"trackId": first[0], "playedMs": 1000, "deferred": true}));
    backend.call("feedback.trackStarted", json!({"trackId": first[1], "queuedAfter": 5}));
    assert!(backend.notifications.is_empty());

    assert_eq!(
        backend.send("catalog.startRadio", json!({"seed": "A/One"}))["error"]["code"],
        1300
    );
}
