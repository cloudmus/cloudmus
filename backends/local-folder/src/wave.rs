// The wave: a station of all the music there is, in an order that learns
// from the listener — what's skipped comes up less, with its artist and its
// folder (an album, say), what's played through comes up more
// (docs/protocol.md §7.1).

use std::collections::{HashMap, HashSet};
use std::sync::Mutex;
use std::time::{SystemTime, UNIX_EPOCH};

use serde_json::{json, Value};

use crate::catalog::{self, Track};

pub const ID: &str = "__wave__";
const STATION: &str = "wave";
// What the first answer brings, and what the queue is kept topped up to.
const FIRST: usize = 10;
const AHEAD: usize = 8;
// Fewer than this queued after the playing track, and more are sent.
const LOW: usize = 3;
// How many a pick looks at: enough for the taste to show, few enough that
// reading their tags stays quick in a large library.
const LOOK_AT: usize = 24;

const LIKED: f64 = 1.6;
const SKIPPED: f64 = 0.35;
const MOST: f64 = 6.0;
const LEAST: f64 = 0.05;

pub fn playlist(tr: impl Fn(&str) -> String) -> Value {
    json!({
        "id": ID, "title": tr("Wave"), "kind": "radioStation", "trackCount": 0, "featured": true,
        "description": tr("All your music, in an order that learns from what you skip and what you play through.")
    })
}

// What's known of a track it served: what to credit or blame.
struct Served {
    artist: String,
    folder: String,
}

struct Station {
    random: u64,
    // Played lately before it started (catalog.startRadio's
    // recentTrackIds), the latest first: kept apart while there are others.
    recent: Vec<String>,
    served: HashMap<String, Served>,
    // In the order it served them; where the queue stands is the front's to
    // say (queuedAfter), this is the fallback.
    order: Vec<String>,
    // Taste, by "artist:<name>" and "folder:<path>": 1 is neutral.
    weights: HashMap<String, f64>,
}

static CURRENT: Mutex<Option<Station>> = Mutex::new(None);

impl Station {
    fn new(recent: Vec<String>) -> Self {
        let seed = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|time| time.as_nanos() as u64)
            .unwrap_or(0)
            ^ u64::from(std::process::id()).rotate_left(32);
        Self {
            random: seed | 1,
            recent,
            served: HashMap::new(),
            order: Vec::new(),
            weights: HashMap::new(),
        }
    }

    // xorshift64*: enough for shuffling music.
    fn next(&mut self) -> u64 {
        self.random ^= self.random >> 12;
        self.random ^= self.random << 25;
        self.random ^= self.random >> 27;
        self.random.wrapping_mul(0x2545_F491_4F6C_DD1D)
    }

    fn below(&mut self, bound: usize) -> usize {
        (self.next() % bound as u64) as usize
    }

    fn unit(&mut self) -> f64 {
        (self.next() >> 11) as f64 / (1u64 << 53) as f64
    }

    fn weight(&self, key: &str) -> f64 {
        self.weights.get(key).copied().unwrap_or(1.0)
    }

    fn scale(&mut self, key: String, factor: f64) {
        let weight = self.weights.entry(key).or_insert(1.0);
        *weight = (*weight * factor).clamp(LEAST, MOST);
    }

    // Credits (or blames) the track's artist and folders: its own folder
    // fully, the ones it's in less — an album more than its artist's folder.
    fn learn(&mut self, id: &str, factor: f64) {
        let Some(served) = self.served.get(id) else {
            return;
        };
        let (artist, folder) = (served.artist.clone(), served.folder.clone());
        if !artist.is_empty() {
            self.scale(format!("artist:{artist}"), factor);
        }
        let mut path = folder.as_str();
        let mut strength = 1.0;
        while !path.is_empty() {
            self.scale(format!("folder:{path}"), factor.powf(strength));
            strength /= 2.0;
            path = path.rsplit_once('/').map_or("", |(parent, _)| parent);
        }
    }

    fn appeal(&self, artist: &str, folder: &str, last_artist: Option<&str>) -> f64 {
        let mut appeal = if artist.is_empty() {
            1.0
        } else {
            self.weight(&format!("artist:{artist}"))
        };
        let mut path = folder;
        while !path.is_empty() {
            appeal *= self.weight(&format!("folder:{path}"));
            path = path.rsplit_once('/').map_or("", |(parent, _)| parent);
        }
        // Not the same artist twice running, unless there's little else.
        if !artist.is_empty() && last_artist == Some(artist) {
            appeal *= 0.2;
        }
        appeal
    }

    // `count` more tracks out of `pool`: none it has served, none played
    // lately while there are others — then the ones played longest ago.
    fn pick(&mut self, pool: &[Track], count: usize) -> Vec<Value> {
        let recent: HashSet<&str> = self.recent.iter().map(String::as_str).collect();
        let mut fresh: Vec<&Track> = pool
            .iter()
            .filter(|track| !self.served.contains_key(track.id()) && !recent.contains(track.id()))
            .collect();
        if fresh.is_empty() {
            // Played lately, the oldest first; then round again.
            let by_id: HashMap<&str, &Track> = pool.iter().map(|track| (track.id(), track)).collect();
            fresh = self
                .recent
                .iter()
                .rev()
                .filter(|id| !self.served.contains_key(id.as_str()))
                .filter_map(|id| by_id.get(id.as_str()).copied())
                .collect();
            if fresh.is_empty() {
                self.served.clear();
                self.recent.clear();
                fresh = pool.iter().collect();
            }
            return fresh
                .into_iter()
                .take(count)
                .map(|track| self.serve(track))
                .collect();
        }
        let mut picked = Vec::new();
        let mut last_artist = self
            .order
            .last()
            .and_then(|id| self.served.get(id))
            .map(|served| served.artist.clone());
        while picked.len() < count && !fresh.is_empty() {
            // A few at random, one of them by how much they appeal.
            let mut looked = Vec::new();
            for _ in 0..LOOK_AT.min(fresh.len()) {
                let at = self.below(fresh.len());
                let track = fresh.swap_remove(at);
                let (artist, folder) = (track.artist(), track.folder());
                let appeal = self.appeal(&artist, &folder, last_artist.as_deref());
                looked.push((track, artist, folder, appeal));
            }
            let total: f64 = looked.iter().map(|(.., appeal)| appeal).sum();
            let mut target = self.unit() * total;
            let mut chosen = looked.len() - 1;
            for (index, (.., appeal)) in looked.iter().enumerate() {
                if target < *appeal {
                    chosen = index;
                    break;
                }
                target -= appeal;
            }
            let (track, artist, folder, _) = looked.swap_remove(chosen);
            // The others go back for the next pick.
            fresh.extend(looked.into_iter().map(|(track, ..)| track));
            self.served.insert(
                track.id().to_owned(),
                Served {
                    artist: artist.clone(),
                    folder,
                },
            );
            self.order.push(track.id().to_owned());
            last_artist = Some(artist);
            picked.push(track.track());
        }
        picked
    }

    fn serve(&mut self, track: &Track) -> Value {
        self.served.insert(
            track.id().to_owned(),
            Served {
                artist: track.artist(),
                folder: track.folder(),
            },
        );
        self.order.push(track.id().to_owned());
        track.track()
    }

    // How many are queued after `id`: the front's count, else its own.
    fn queued_after(&self, id: &str, params: &Value) -> usize {
        params["queuedAfter"]
            .as_u64()
            .map(|count| count as usize)
            .unwrap_or_else(|| {
                self.order
                    .iter()
                    .position(|served| served == id)
                    .map_or(0, |at| self.order.len() - at - 1)
            })
    }
}

fn added(tracks: Vec<Value>, replace: bool) -> Option<Value> {
    if tracks.is_empty() {
        return None;
    }
    let mut params = json!({"stationId": STATION, "tracks": tracks});
    if replace {
        params["replaceUpcoming"] = json!(true);
    }
    Some(json!({"jsonrpc": "2.0", "method": "radio/tracksAdded", "params": params}))
}

/// catalog.startRadio: a new run of the wave; None if `seed` isn't one.
pub fn start(params: &Value, tr: impl Fn(&str) -> String) -> Option<Value> {
    if params["seed"].as_str().is_some_and(|seed| seed != ID) {
        return None;
    }
    let recent = params["recentTrackIds"]
        .as_array()
        .map(|ids| ids.iter().filter_map(Value::as_str).map(str::to_owned).collect())
        .unwrap_or_default();
    let pool = catalog::pool(tr);
    let mut station = Station::new(recent);
    let tracks = station.pick(&pool, FIRST);
    *CURRENT.lock().unwrap() = Some(station);
    Some(json!({"stationId": STATION, "initialTracks": tracks}))
}

/// feedback.trackStarted / trackFinished / skip: what the wave learns, and
/// the radio/tracksAdded it then has to send, if any.
pub fn feedback(method: &str, params: &Value, tr: impl Fn(&str) -> String) -> Option<Value> {
    let id = params["trackId"].as_str()?;
    let mut current = CURRENT.lock().unwrap();
    let station = current.as_mut()?;
    if !station.served.contains_key(id) {
        return None;
    }
    // A late one counts for the taste, and changes nothing queued.
    let deferred = params["deferred"] == true;
    match method {
        "feedback.skip" => {
            station.learn(id, SKIPPED);
            if deferred {
                return None;
            }
            // "Not this": what's queued is picked again, with that in mind —
            // and what was queued may come up again later.
            if let Some(at) = station.order.iter().position(|served| served == id) {
                for dropped in station.order.split_off(at + 1) {
                    station.served.remove(&dropped);
                }
            }
            let pool = catalog::pool(tr);
            let tracks = station.pick(&pool, AHEAD);
            added(tracks, true)
        }
        "feedback.trackFinished" | "feedback.trackStarted" => {
            if method == "feedback.trackFinished" {
                station.learn(id, LIKED);
            }
            let queued = station.queued_after(id, params);
            if deferred || queued >= LOW {
                return None;
            }
            let pool = catalog::pool(tr);
            let tracks = station.pick(&pool, AHEAD - queued.min(AHEAD));
            added(tracks, false)
        }
        _ => None,
    }
}
