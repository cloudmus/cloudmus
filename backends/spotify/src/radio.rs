//! catalog.startRadio and the feedback.* that keep a station going — the
//! queue logic of `cloudmus_backend_ytmusic/radio.py`, on Spotify's own
//! track radio: spclient's "inspired by" mix, a playlist of about fifty
//! tracks made for a seed track.
//!
//! A station is seeded with a track ("Start Radio from This Track", the
//! bare track id), or is the Liked Songs station listed with the playlists,
//! which follows the account's latest likes. The front owns the queue
//! (docs/protocol.md §7.1): a skip ("not this") replaces what's queued with
//! a fresh batch; a track played to the end only tops the queue up once it
//! runs low. A fresh batch is the mix of the last track played to the end,
//! so the station follows what was actually listened to.

use std::collections::{HashMap, HashSet};
use std::time::{Duration, Instant};

use librespot_core::{Error, Session, SpotifyUri};
use librespot_metadata::{Metadata, Playlist as SpotifyPlaylist};
use rand::seq::IndexedRandom;
use serde_json::Value;

use crate::catalog::{self, Liked};
use crate::generated::methods::RADIO_TRACKS_ADDED;
use crate::generated::models::{Playlist, StartRadioResult, Track, TracksAddedParams};
use crate::i18n::tr;
use crate::rpc::Notifier;

pub const LIKED_STATION_ID: &str = "__liked_radio__";
/// Tracks sent at a time.
const BATCH: usize = 20;
/// Topped up once fewer than this many served tracks are left to play.
const UPCOMING_LOW_WATER: usize = 3;
/// With only lately played tracks to serve, this many of them are — the
/// ones played longest ago.
const REPEATS_AT_A_TIME: usize = 3;
/// For this long after tracks were sent, a track's start doesn't ask for
/// more: its queuedAfter is from before they came.
const STALE_COUNT: Duration = Duration::from_secs(3);
/// Likes the Liked Songs station picks its seeds among.
const LIKED_SEEDS: usize = 50;

pub fn liked_station() -> Playlist {
    Playlist {
        id: LIKED_STATION_ID.into(),
        title: tr("Radio from Liked Songs"),
        kind: "radioStation".into(),
        ..Default::default()
    }
}

/// The track ids of a seed track's mix, in Spotify's order.
async fn mix_of(session: &Session, track_id: &str) -> Result<Vec<String>, Error> {
    let uri = catalog::track_uri(track_id)?;
    let response = session.spclient().get_radio_for_track(&uri).await?;
    let json: Value = serde_json::from_slice(&response)?;
    let Some(playlist) = json
        .get("mediaItems")
        .and_then(Value::as_array)
        .and_then(|items| items.iter().find_map(|i| i.get("uri").and_then(Value::as_str)))
    else {
        return Ok(Vec::new());
    };
    let playlist = SpotifyPlaylist::get(session, &SpotifyUri::from_uri(playlist)?).await?;
    Ok(playlist
        .tracks()
        .filter_map(|u| matches!(u, SpotifyUri::Track { .. }).then(|| u.to_id().ok()).flatten())
        .collect())
}

#[derive(Default)]
pub struct RadioSession {
    /// Fixed for the session, in every radio/tracksAdded: the front drops
    /// one whose stationId doesn't match what start() returned.
    station_id: Option<String>,
    /// The track the next batch follows.
    last_good: Option<String>,
    played: HashSet<String>,
    skipped: HashSet<String>,
    /// Served, not started yet, in queue order.
    upcoming: Vec<String>,
    /// Played lately before this session: track id -> how far back.
    recent: HashMap<String, usize>,
    /// Mixes fetched, by seed track: the same seed gives the same mix.
    mixes: HashMap<String, Vec<String>>,
    topped_up_at: Option<Instant>,
}

impl RadioSession {
    fn sent_lately(&self) -> bool {
        self.topped_up_at.is_some_and(|at| at.elapsed() <= STALE_COUNT)
    }

    /// The seed of the next batch: the anchor, the last track played to
    /// the end, or the station's own seed.
    async fn seed(&self, session: &Session, liked: &Liked, anchor: Option<&str>) -> Result<Option<String>, Error> {
        if let Some(anchor) = anchor.or(self.last_good.as_deref()) {
            return Ok(Some(anchor.to_owned()));
        }
        match self.station_id.as_deref() {
            Some(LIKED_STATION_ID) => {
                let uris = liked.uris(session, false).await?;
                let pick = uris[..uris.len().min(LIKED_SEEDS)].choose(&mut rand::rng()).cloned();
                Ok(pick.and_then(|u| u.strip_prefix("spotify:track:").map(str::to_owned)))
            }
            other => Ok(other.map(str::to_owned)),
        }
    }

    /// Up to a batch of the seed's mix, minus what played, was skipped or
    /// is queued — and minus what played lately while there's anything
    /// else; failing that, a few of those, the oldest first.
    fn fresh(&self, mix: &[String], seed_is_station: Option<&str>) -> Vec<String> {
        let mut tracks = Vec::new();
        let mut held = Vec::new();
        let mut seen = HashSet::new();
        for id in mix {
            if !seen.insert(id) || self.played.contains(id) || self.skipped.contains(id) || self.upcoming.contains(id) {
                continue;
            }
            // The track a station is started from was asked for, however lately it played.
            if self.recent.contains_key(id) && Some(id.as_str()) != seed_is_station {
                held.push(id.clone());
            } else {
                tracks.push(id.clone());
            }
        }
        if tracks.is_empty() {
            held.sort_by_key(|id| std::cmp::Reverse(self.recent[id]));
            held.truncate(REPEATS_AT_A_TIME);
            return held;
        }
        tracks.truncate(BATCH);
        tracks
    }

    async fn batch(&mut self, session: &Session, liked: &Liked, anchor: Option<&str>) -> Result<Vec<Track>, Error> {
        let Some(seed) = self.seed(session, liked, anchor).await? else { return Ok(Vec::new()) };
        if !self.mixes.contains_key(&seed) {
            let mix = mix_of(session, &seed).await?;
            self.mixes.insert(seed.clone(), mix);
        }
        let started_from = self.station_id.clone();
        let ids = self.fresh(&self.mixes[&seed], started_from.as_deref());
        let uris: Vec<SpotifyUri> = ids.iter().filter_map(|id| catalog::track_uri(id).ok()).collect();
        let tracks = catalog::tracks(session, &uris).await?;
        Ok(tracks.iter().filter_map(|t| catalog::to_track(t, None)).collect())
    }

    pub async fn start(
        &mut self,
        session: &Session,
        liked: &Liked,
        seed: &str,
        recent: &[String],
    ) -> Result<StartRadioResult, Error> {
        *self = RadioSession {
            station_id: Some(seed.to_owned()),
            mixes: std::mem::take(&mut self.mixes),
            ..Default::default()
        };
        for (rank, id) in recent.iter().enumerate() {
            self.recent.entry(id.clone()).or_insert(rank);
        }
        let mut tracks = self.batch(session, liked, None).await?;
        // A track's mix starts with the track itself: the radio starts with it.
        if seed != LIKED_STATION_ID && !tracks.iter().any(|t| t.id == seed) {
            if let Ok(uri) = catalog::track_uri(seed) {
                if let Some(track) =
                    catalog::tracks(session, &[uri]).await?.first().and_then(|t| catalog::to_track(t, None))
                {
                    tracks.insert(0, track);
                }
            }
        }
        self.upcoming = tracks.iter().map(|t| t.id.clone()).collect();
        self.topped_up_at = Some(Instant::now());
        Ok(StartRadioResult { station_id: seed.to_owned(), initial_tracks: tracks })
    }

    async fn top_up(
        &mut self,
        session: &Session,
        liked: &Liked,
        notifier: &Notifier,
        replace: bool,
        anchor: Option<&str>,
    ) -> Result<(), Error> {
        let Some(station_id) = self.station_id.clone() else { return Ok(()) };
        if replace {
            // What's queued is superseded: its tracks may come back.
            self.upcoming.clear();
        }
        let tracks = self.batch(session, liked, anchor).await?;
        if tracks.is_empty() {
            return Ok(());
        }
        self.upcoming.extend(tracks.iter().map(|t| t.id.clone()));
        self.topped_up_at = Some(Instant::now());
        notifier.notify(
            RADIO_TRACKS_ADDED,
            TracksAddedParams { station_id, tracks, replace_upcoming: replace.then_some(true) },
        );
        Ok(())
    }

    /// The front's queue isn't what was served (it keeps downloaded tracks,
    /// drops duplicates): no more than `queued_after` served tracks can
    /// still be waiting.
    fn follow_front(&mut self, queued_after: Option<i64>) {
        if self.sent_lately() {
            return;
        }
        if let Some(left) = queued_after.map(|n| n.max(0) as usize) {
            if self.upcoming.len() > left {
                self.upcoming.drain(..self.upcoming.len() - left);
            }
        }
    }

    pub async fn track_started(
        &mut self,
        session: &Session,
        liked: &Liked,
        notifier: &Notifier,
        track_id: &str,
        queued_after: Option<i64>,
    ) -> Result<(), Error> {
        self.played.insert(track_id.to_owned());
        let position = self.upcoming.iter().position(|id| id == track_id);
        if let Some(position) = position {
            self.upcoming.drain(..=position);
        }
        self.follow_front(queued_after);
        // A jump ahead that leaves little queued gets more now, following
        // the track picked; one reached in order is topped up once played.
        let low = match queued_after {
            None => position.is_some_and(|p| p > 0) && self.upcoming.len() < UPCOMING_LOW_WATER,
            Some(n) => (n.max(0) as usize) < UPCOMING_LOW_WATER && !self.sent_lately(),
        };
        if low {
            self.top_up(session, liked, notifier, false, Some(track_id)).await?;
        }
        Ok(())
    }

    pub async fn track_finished(
        &mut self,
        session: &Session,
        liked: &Liked,
        notifier: &Notifier,
        track_id: &str,
        queued_after: Option<i64>,
    ) -> Result<(), Error> {
        self.last_good = Some(track_id.to_owned());
        self.follow_front(queued_after);
        let left = queued_after.map(|n| n.max(0) as usize).unwrap_or(self.upcoming.len());
        if left < UPCOMING_LOW_WATER {
            self.top_up(session, liked, notifier, false, None).await?;
        }
        Ok(())
    }

    pub async fn skip(
        &mut self,
        session: &Session,
        liked: &Liked,
        notifier: &Notifier,
        track_id: &str,
    ) -> Result<(), Error> {
        self.skipped.insert(track_id.to_owned());
        // "Not this": a fresh batch replaces what's queued.
        self.top_up(session, liked, notifier, true, None).await
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ids(v: &[&str]) -> Vec<String> {
        v.iter().map(|s| s.to_string()).collect()
    }

    #[test]
    fn fresh_leaves_out_played_skipped_and_queued() {
        let mut radio = RadioSession { station_id: Some("a".into()), ..Default::default() };
        radio.played.insert("b".into());
        radio.skipped.insert("c".into());
        radio.upcoming.push("d".into());
        assert_eq!(radio.fresh(&ids(&["a", "b", "c", "d", "e", "e"]), Some("a")), ids(&["a", "e"]));
    }

    #[test]
    fn lately_played_tracks_wait_unless_nothing_else_is_left() {
        let mut radio = RadioSession { station_id: Some("s".into()), ..Default::default() };
        radio.recent = HashMap::from([("x".into(), 0), ("y".into(), 5), ("z".into(), 2)]);
        assert_eq!(radio.fresh(&ids(&["x", "new", "y"]), Some("s")), ids(&["new"]));
        // only recent ones: the ones played longest ago first
        assert_eq!(radio.fresh(&ids(&["x", "y", "z"]), Some("s")), ids(&["y", "z", "x"]));
        // the seed itself plays however lately it did
        assert_eq!(radio.fresh(&ids(&["x"]), Some("x")), ids(&["x"]));
    }

    #[test]
    fn the_front_count_trims_what_is_considered_queued() {
        let mut radio = RadioSession { upcoming: ids(&["a", "b", "c", "d"]), ..Default::default() };
        radio.follow_front(Some(2));
        assert_eq!(radio.upcoming, ids(&["c", "d"]));
        // right after a top-up, the count is from before it and is ignored
        radio.topped_up_at = Some(Instant::now());
        radio.follow_front(Some(0));
        assert_eq!(radio.upcoming, ids(&["c", "d"]));
    }

    #[test]
    fn batches_are_capped() {
        let radio = RadioSession::default();
        let mix: Vec<String> = (0..60).map(|i| format!("t{i}")).collect();
        assert_eq!(radio.fresh(&mix, None).len(), BATCH);
    }
}
