//! The request handlers: what each protocol method does with the account,
//! the catalog and the stream server.

use std::collections::HashMap;
use std::sync::{Arc, Mutex};

use librespot_core::error::ErrorKind;
use serde_json::{json, Value};
use tokio::task::AbortHandle;

use crate::account::{upstream_error, Account};
use crate::catalog::{self, Liked};
use crate::collection;
use crate::config;
use crate::generated::methods::*;
use crate::generated::models::{
    CancelParams, ErrorParams, LikeParams, ListLikedParams, ListPlaylistsResult, ListTracksParams, PlayParams,
    ResolveStreamParams, ResolveStreamResult, SkipParams, StartRadioParams, StreamReadyParams, TrackFinishedParams,
    TrackStartedParams, UnlikeParams,
};
use crate::i18n::{self, tr, trf};
use crate::radio::{self, RadioSession};
use crate::rpc::errors::{
    app_error_data, parse_params, to_result, REASON_UNAVAILABLE, RESOURCE_NOT_FOUND, STATE_INVALID,
    UPSTREAM_UNREACHABLE,
};
use crate::rpc::settings::{Field, Group, Kind, SettingsStore};
use crate::rpc::{Notifier, RpcError, Server, SourceInfo};
use crate::stream::{ResolveError, StreamServer};

const QUALITY_KEY: &str = "streamQuality";
const QUALITY_OPTIONS: &[(&str, &str)] =
    &[("320", "Very high (320 kbps)"), ("160", "High (160 kbps)"), ("96", "Normal (96 kbps)")];

fn capabilities() -> Value {
    json!({
        "playback": {
            "providesStream": true,
            "resolveStream": true,
            "selfPlayback": false,
            "controls": {"pause": false, "seek": false, "volume": false},
        },
        "browse": {"playlists": true, "likedTracks": true, "radio": true, "search": false, "editPlaylists": false},
        "feedback": {"like": true, "dislike": false, "skip": false},
        // Saved by the front from the stream (docs/protocol.md §7.5).
        "download": false,
        "downloadFromStream": true,
        "auth": {"required": true, "flow": "oauthRedirect"},
    })
}

struct Backend {
    account: Arc<Account>,
    liked: Liked,
    streams: Arc<StreamServer>,
    settings: SettingsStore,
    notifier: Notifier,
    /// Stream resolutions in flight, by the request id playback.cancel names.
    inflight: Mutex<HashMap<i64, AbortHandle>>,
    radio: tokio::sync::Mutex<RadioSession>,
}

/// A catalog failure as the front should see it.
fn catalog_error(e: &librespot_core::Error) -> RpcError {
    match e.kind {
        ErrorKind::NotFound => RpcError::new(RESOURCE_NOT_FOUND, tr("Playlist not found")).with_data(app_error_data(
            false,
            Some(&e.to_string()),
            None,
            None,
        )),
        _ => upstream_error(e),
    }
}

impl Backend {
    async fn resolve(&self, track_id: &str) -> Result<ResolveStreamResult, RpcError> {
        let session = self.account.session().await?;
        match self.streams.resolve(&session, track_id, &self.settings.get_str(QUALITY_KEY)).await {
            Ok(stream) => Ok(ResolveStreamResult { stream }),
            Err(ResolveError::Unplayable(why)) => {
                Err(RpcError::new(RESOURCE_NOT_FOUND, trf("Track not playable: {error}", &[("error", &why.0)]))
                    .with_data(app_error_data(false, None, Some(track_id), Some(REASON_UNAVAILABLE))))
            }
            Err(ResolveError::Upstream(e)) => {
                let mut error = upstream_error(&e);
                error.message =
                    trf("Failed to resolve stream for {track}: {error}", &[("track", &track_id), ("error", &e)]);
                if let Some(data) = error.data.as_mut() {
                    data["trackId"] = track_id.into();
                }
                Err(error)
            }
        }
    }

    /// Runs `work` as a task playback.cancel can stop.
    async fn cancellable<T: Send + 'static>(
        self: &Arc<Self>,
        request_id: i64,
        work: impl std::future::Future<Output = T> + Send + 'static,
    ) -> Option<T> {
        let task = tokio::spawn(work);
        self.inflight.lock().unwrap().insert(request_id, task.abort_handle());
        let result = task.await.ok();
        self.inflight.lock().unwrap().remove(&request_id);
        result
    }
}

pub async fn build_server() -> std::io::Result<Server> {
    let translator = i18n::translator();
    let mut server = Server::new(
        SourceInfo {
            id: "spotify",
            name: "Spotify",
            version: env!("CARGO_PKG_VERSION"),
            description: "Spotify streaming service (Premium)",
        },
        capabilities(),
        translator.clone(),
    );
    server.capabilities_mut()["settings"] = true.into();
    let notifier = server.notifier();
    let backend = Arc::new(Backend {
        account: Account::new(notifier.clone()),
        liked: Liked::default(),
        streams: StreamServer::start().await?,
        settings: SettingsStore::new(
            config::config_file(),
            vec![Group { id: "playback", title: "Playback" }],
            vec![Field {
                key: QUALITY_KEY,
                label: "Quality",
                kind: Kind::Enum(QUALITY_OPTIONS),
                default: json!("320"),
                description: Some("Applies from the next track, and to the tracks you save."),
                group: Some("playback"),
            }],
            translator,
        ),
        notifier,
        inflight: Mutex::new(HashMap::new()),
        radio: tokio::sync::Mutex::new(RadioSession::default()),
    });

    macro_rules! handle {
        ($name:expr, |$b:ident, $params:ident, $id:ident| $body:expr) => {{
            let backend = backend.clone();
            server.method($name, move |$params: Value, $id: i64| {
                let $b = backend.clone();
                async move { $body }
            });
        }};
    }

    // --- auth ---
    handle!(AUTH_GET_STATUS, |b, _p, _id| to_result(b.account.get_status()));
    handle!(AUTH_START, |b, _p, _id| {
        b.account.start();
        Ok(json!({}))
    });
    handle!(AUTH_CANCEL, |b, _p, _id| {
        b.account.cancel();
        Ok(json!({}))
    });
    handle!(AUTH_LOGOUT, |b, _p, _id| {
        b.account.logout();
        Ok(json!({}))
    });

    // --- catalog ---
    handle!(CATALOG_LIST_PLAYLISTS, |b, _p, _id| {
        let session = b.account.session().await?;
        let mut playlists = catalog::list_playlists(&session, &b.liked).await.map_err(|e| catalog_error(&e))?;
        // Right after Liked Songs, which it plays from.
        playlists.insert(1, radio::liked_station());
        to_result(ListPlaylistsResult { playlists })
    });
    handle!(CATALOG_LIST_TRACKS, |b, p, _id| {
        let params: ListTracksParams = parse_params(p)?;
        let session = b.account.session().await?;
        let page = catalog::list_tracks(&session, &b.liked, &params.playlist_id, params.cursor.as_deref())
            .await
            .map_err(|e| catalog_error(&e))?;
        to_result(page)
    });
    handle!(CATALOG_LIST_LIKED, |b, p, _id| {
        let params: ListLikedParams = parse_params(p)?;
        let session = b.account.session().await?;
        let page =
            catalog::list_liked(&session, &b.liked, params.cursor.as_deref()).await.map_err(|e| catalog_error(&e))?;
        to_result(page)
    });

    // --- playback ---
    handle!(PLAYBACK_RESOLVE_STREAM, |b, p, id| {
        let params: ResolveStreamParams = parse_params(p)?;
        let worker = b.clone();
        match b.cancellable(id, async move { worker.resolve(&params.track_id).await }).await {
            Some(result) => to_result(result?),
            None => Err(RpcError::new(STATE_INVALID, tr("Stream resolution cancelled"))),
        }
    });
    handle!(PLAYBACK_PLAY, |b, p, id| {
        let params: PlayParams = parse_params(p)?;
        let worker = b.clone();
        let backend = b.clone();
        // Answered at once; the stream follows as track/streamReady (§11.1).
        tokio::spawn(async move {
            let track_id = params.track_id.clone();
            let outcome = backend.cancellable(id, async move { worker.resolve(&params.track_id).await }).await;
            match outcome {
                None => {} // cancelled: the front no longer wants it
                Some(Ok(result)) => backend
                    .notifier
                    .notify(TRACK_STREAM_READY, StreamReadyParams { request_id: id, track_id, stream: result.stream }),
                Some(Err(e)) => {
                    backend.notifier.notify(ERROR, ErrorParams { code: e.code, message: e.message, data: e.data })
                }
            }
        });
        Ok(json!({"accepted": true}))
    });
    handle!(PLAYBACK_CANCEL, |b, p, _id| {
        let params: CancelParams = parse_params(p)?;
        if let Some(task) = b.inflight.lock().unwrap().remove(&params.request_id) {
            task.abort();
        }
        Ok(json!({}))
    });

    // --- radio ---
    handle!(CATALOG_START_RADIO, |b, p, _id| {
        let params: StartRadioParams = parse_params(p)?;
        let Some(seed) = params.seed.filter(|s| !s.is_empty()) else {
            return Err(RpcError::new(STATE_INVALID, tr("Spotify radio needs a track to start from"))
                .with_data(app_error_data(false, None, None, None)));
        };
        let session = b.account.session().await?;
        let recent = params.recent_track_ids.unwrap_or_default();
        let result =
            b.radio.lock().await.start(&session, &b.liked, &seed, &recent).await.map_err(|e| catalog_error(&e))?;
        to_result(result)
    });
    handle!(FEEDBACK_TRACK_STARTED, |b, p, _id| {
        let params: TrackStartedParams = parse_params(p)?;
        let session = b.account.session().await?;
        let mut radio = b.radio.lock().await;
        radio
            .track_started(&session, &b.liked, &b.notifier, &params.track_id, params.queued_after)
            .await
            .map_err(|e| upstream_error(&e))?;
        Ok(json!({}))
    });
    handle!(FEEDBACK_TRACK_FINISHED, |b, p, _id| {
        let params: TrackFinishedParams = parse_params(p)?;
        let session = b.account.session().await?;
        let mut radio = b.radio.lock().await;
        radio
            .track_finished(&session, &b.liked, &b.notifier, &params.track_id, params.queued_after)
            .await
            .map_err(|e| upstream_error(&e))?;
        Ok(json!({}))
    });
    handle!(FEEDBACK_SKIP, |b, p, _id| {
        let params: SkipParams = parse_params(p)?;
        let session = b.account.session().await?;
        b.radio
            .lock()
            .await
            .skip(&session, &b.liked, &b.notifier, &params.track_id)
            .await
            .map_err(|e| upstream_error(&e))?;
        Ok(json!({}))
    });

    // --- feedback ---
    handle!(FEEDBACK_LIKE, |b, p, _id| {
        let params: LikeParams = parse_params(p)?;
        set_liked(&b, &params.track_id, true).await
    });
    handle!(FEEDBACK_UNLIKE, |b, p, _id| {
        let params: UnlikeParams = parse_params(p)?;
        set_liked(&b, &params.track_id, false).await
    });

    // --- settings ---
    handle!(SETTINGS_DESCRIBE, |b, _p, _id| to_result(b.settings.describe()));
    handle!(SETTINGS_UPDATE, |b, p, _id| {
        let Some(values) = p.get("values").and_then(Value::as_object).cloned() else {
            return Err(RpcError::invalid_params(tr("values: expected an object")));
        };
        b.settings.update(&values)?;
        Ok(json!({}))
    });

    Ok(server)
}

async fn set_liked(b: &Backend, track_id: &str, liked: bool) -> Result<Value, RpcError> {
    let session = b.account.session().await?;
    let uri = format!("spotify:track:{track_id}");
    collection::write(&session, collection::TRACKS_SET, &uri, !liked).await.map_err(|e| {
        let mut error = upstream_error(&e);
        error.code = UPSTREAM_UNREACHABLE;
        error
    })?;
    b.liked.set(&uri, liked).await;
    Ok(json!({}))
}
