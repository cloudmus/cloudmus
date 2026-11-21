//! The smallest backend on this crate: one empty playlist and a setting.
//! Run through the conformance suite in CI, so the server itself is held
//! to the protocol and not only the backends built on it:
//!
//!   python -m rpc_common.testing.conformance backends/target/debug/examples/minimal

use std::collections::BTreeMap;
use std::sync::Arc;

use rpc_common::errors::{app_error_data, RESOURCE_NOT_FOUND};
use rpc_common::generated::models::{ListPlaylistsResult, Playlist};
use rpc_common::i18n::Translator;
use rpc_common::settings::{Field, Kind, PathKind, SettingsStore};
use rpc_common::{RpcError, Server, SourceInfo};
use serde_json::json;

#[tokio::main]
async fn main() {
    let translator = Arc::new(Translator::new(BTreeMap::new()));
    let info = SourceInfo { id: "minimal", name: "Minimal", version: "0.1.0", description: "An empty source" };
    let capabilities = json!({
        "playback": {"providesStream": true, "resolveStream": true, "selfPlayback": false,
            "controls": {"pause": false, "seek": false, "volume": false}},
        "browse": {"playlists": true, "likedTracks": false, "radio": false, "search": false},
        "feedback": {"like": false, "dislike": false, "skip": false},
        "download": false,
        "auth": {"required": false, "flow": "none"},
        "network": false,
    });
    let mut server = Server::new(info, capabilities, translator.clone());
    let config = std::env::temp_dir().join(format!("cloudmus-minimal-{}.json", std::process::id()));
    let field = Field {
        key: "musicDir",
        label: "Music folder",
        kind: Kind::Path(PathKind::Directory),
        default: json!(""),
        description: None,
        group: None,
    };
    Arc::new(SettingsStore::new(config, vec![], vec![field], translator)).register(&mut server);
    server.method("catalog.listPlaylists", |_, _| async {
        let playlist =
            Playlist { id: "empty".into(), title: "Empty".into(), kind: "playlist".into(), ..Default::default() };
        rpc_common::errors::to_result(ListPlaylistsResult { playlists: vec![playlist] })
    });
    server.method("catalog.listTracks", |_, _| async { Ok(json!({"tracks": []})) });
    for method in ["playback.play", "playback.resolveStream"] {
        server.method(method, |params, _| async move {
            let id = params.get("trackId").and_then(|v| v.as_str()).unwrap_or_default().to_owned();
            Err(RpcError::new(RESOURCE_NOT_FOUND, "Track not found").with_data(app_error_data(
                false,
                None,
                Some(&id),
                None,
            )))
        });
    }
    server.method("playback.cancel", |_, _| async { Ok(json!({})) });
    server.run().await;
}
