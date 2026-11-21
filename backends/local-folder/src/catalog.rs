use std::collections::{BTreeMap, HashSet};
use std::fs;
use std::hash::{DefaultHasher, Hash, Hasher};
use std::io::Write;
use std::path::{Component, Path, PathBuf};

use serde_json::{json, Value};
use url::Url;

use crate::{config, metadata};

const ROOT: &str = "__root__";

// A folder of tracks, listed as a playlist with everything under it.
struct Playlist {
    title: String,
    // The playlist it's listed under (Playlist.parentId); none at the top.
    parent: Option<String>,
    // Where it's listed among the others: its group, then its path.
    order: (u8, String),
    tracks: Vec<Track>,
}

#[derive(Clone)]
enum Track {
    File { id: String, path: PathBuf },
    Media(Value),
}

struct Library {
    playlists: BTreeMap<String, Playlist>,
    seen: HashSet<PathBuf>,
}

// The groups of the top level, in the order they're listed.
const MUSIC_FOLDER: u8 = 0;
const SAVED: u8 = 1;
const DEVICE: u8 = 2;

fn relative_id(path: &Path) -> Option<String> {
    path.components()
        .map(|part| match part {
            Component::Normal(text) => text.to_str(),
            _ => None,
        })
        .collect::<Option<Vec<_>>>()
        .map(|parts| parts.join("/"))
}

fn entries(path: &Path) -> Vec<PathBuf> {
    match fs::read_dir(path) {
        Ok(entries) => {
            let mut paths: Vec<_> = entries
                .filter_map(Result::ok)
                .map(|entry| entry.path())
                .collect();
            paths.sort();
            paths
        }
        Err(error) => {
            if error.kind() != std::io::ErrorKind::NotFound {
                eprintln!("local-folder: read_dir {}: {error}", path.display());
            }
            Vec::new()
        }
    }
}

impl Library {
    fn new(tr: impl Fn(&str) -> String) -> Self {
        let mut library = Self {
            playlists: BTreeMap::new(),
            seen: HashSet::new(),
        };
        let host = config::host_library().and_then(|index| match fs::read(&index) {
            Ok(bytes) => serde_json::from_slice::<Value>(&bytes)
                .map_err(|error| eprintln!("local-folder: read the host's media library: {error}"))
                .ok(),
            Err(_) => None,
        });
        if let Some(root) = config::music_root() {
            let tracks = library.scan(&root, "");
            // With the device's own library beside it, it's one folder among
            // others: named as such.
            let title = match (&host, root.file_name().and_then(|name| name.to_str())) {
                (Some(_), Some(name)) => name.to_owned(),
                _ => tr("All music"),
            };
            library.add_tree(MUSIC_FOLDER, ROOT, title, "", false, tracks);
        }
        if let Some(data) = host {
            library.add_host(&data, tr);
        }
        library
    }

    // Every audio file under `root`, with the folders it's in, below `root`.
    fn scan(&mut self, root: &Path, prefix: &str) -> Vec<(Vec<String>, Track)> {
        let Ok(canonical) = fs::canonicalize(root) else {
            return Vec::new();
        };
        let mut found = Vec::new();
        let mut directories = vec![(root.to_path_buf(), Vec::new())];
        while let Some((directory, folders)) = directories.pop() {
            if !fs::canonicalize(&directory).is_ok_and(|path| path.starts_with(&canonical)) {
                continue;
            }
            for entry in entries(&directory) {
                // A link to a folder isn't followed: it could lead out, or round.
                if entry
                    .symlink_metadata()
                    .is_ok_and(|metadata| metadata.is_dir())
                {
                    let Some(name) = entry.file_name().and_then(|name| name.to_str()) else {
                        continue;
                    };
                    let mut inner: Vec<String> = folders.clone();
                    inner.push(name.to_owned());
                    directories.push((entry, inner));
                } else if let Some(track) = self.file(root, &canonical, prefix, entry) {
                    found.push((folders.clone(), track));
                }
            }
        }
        found
    }

    // A playlist `root_id` with all of `tracks`, and one for each folder
    // they're in, holding what's under it. A folder's id is `root_id`'s
    // children's prefix plus its path: for the music folder, the bare path
    // (its top folders are listed beside it, as they always were); for the
    // others, under their root.
    fn add_tree(
        &mut self,
        group: u8,
        root_id: &str,
        root_title: String,
        folder_prefix: &str,
        nested: bool,
        tracks: Vec<(Vec<String>, Track)>,
    ) {
        if tracks.is_empty() {
            return;
        }
        let mut lists: BTreeMap<String, Playlist> = BTreeMap::new();
        for (folders, track) in tracks {
            for depth in 1..=folders.len() {
                let path = folders[..depth].join("/");
                let parent = if depth > 1 {
                    Some(format!("{folder_prefix}{}", folders[..depth - 1].join("/")))
                } else {
                    nested.then(|| root_id.to_owned())
                };
                lists
                    .entry(format!("{folder_prefix}{path}"))
                    .or_insert_with(|| Playlist {
                        title: folders[depth - 1].clone(),
                        parent,
                        order: (group, path),
                        tracks: Vec::new(),
                    })
                    .tracks
                    .push(track.clone());
            }
            lists
                .entry(root_id.to_owned())
                .or_insert_with(|| Playlist {
                    title: root_title.clone(),
                    parent: None,
                    order: (group, String::new()),
                    tracks: Vec::new(),
                })
                .tracks
                .push(track);
        }
        for (id, mut list) in lists {
            list.tracks.sort_by(|left, right| left.id().cmp(right.id()));
            match self.playlists.get_mut(&id) {
                Some(existing) => existing.tracks.extend(list.tracks),
                None => {
                    self.playlists.insert(id, list);
                }
            }
        }
    }

    // The media library the front's platform keeps (media.setLibrary): the
    // app's own folders, read as files, and its items, played by their uri.
    // What the app saved goes under Saved tracks, by the folders the saving
    // made; the rest of the device's music under Device music, by its folders.
    fn add_host(&mut self, data: &Value, tr: impl Fn(&str) -> String) {
        const SAVED_ID: &str = "@android/saved-media";
        const DEVICE_ID: &str = "@android/device-media";
        let mut saved = Vec::new();
        if let Some(roots) = data["folders"].as_array() {
            for root in roots {
                let (Some(id), Some(path)) = (root["id"].as_str(), root["path"].as_str()) else {
                    continue;
                };
                saved.extend(self.scan(Path::new(path), &format!("@android/{id}/")));
            }
        }
        let mut device = Vec::new();
        if let Some(records) = data["items"].as_array() {
            let device_music = config::device_music();
            let mut seen = HashSet::new();
            for record in records {
                let own = record["own"] == true;
                // What the front still lists from before the setting changed.
                if !own && !device_music {
                    continue;
                }
                let (Some(id), Some(uri)) = (record["id"].as_str(), record["uri"].as_str()) else {
                    continue;
                };
                if !id.starts_with("@android/media/")
                    || !uri.starts_with("content://media/")
                    || !seen.insert(id)
                {
                    continue;
                }
                if let Some(path) = record["path"]
                    .as_str()
                    .and_then(|text| fs::canonicalize(text).ok())
                {
                    if self.seen.contains(&path) {
                        continue;
                    }
                }
                let relative = record["relativePath"].as_str().unwrap_or_default();
                let relative = if own {
                    relative.strip_prefix("Music/CloudMus/").unwrap_or_default()
                } else {
                    relative
                };
                let folders = relative
                    .split('/')
                    .filter(|part| !part.is_empty())
                    .map(str::to_owned)
                    .collect();
                let entry = (folders, Track::Media(record.clone()));
                if own {
                    saved.push(entry);
                } else {
                    device.push(entry);
                }
            }
        }
        self.add_tree(SAVED, SAVED_ID, tr("Saved tracks"), &format!("{SAVED_ID}/"), true, saved);
        self.add_tree(DEVICE, DEVICE_ID, tr("Device music"), &format!("{DEVICE_ID}/"), true, device);
    }

    fn file(
        &mut self,
        root: &Path,
        canonical: &Path,
        prefix: &str,
        entry: PathBuf,
    ) -> Option<Track> {
        metadata::mime(&metadata::extension(&entry))?;
        let resolved = fs::canonicalize(&entry).ok()?;
        if !resolved.starts_with(canonical) || !resolved.is_file() {
            return None;
        }
        let id = relative_id(entry.strip_prefix(root).ok()?)?;
        if config::host_library().is_some() && !self.seen.insert(resolved.clone()) {
            return None;
        }
        Some(Track::File {
            id: format!("{prefix}{id}"),
            path: entry,
        })
    }

    // In the order they're listed: each group's root, then its folders, a
    // folder before what's in it.
    fn lists(&self) -> impl Iterator<Item = (&str, &Playlist)> {
        let mut lists: Vec<_> = self.playlists.iter().map(|(id, list)| (id.as_str(), list)).collect();
        lists.sort_by(|left, right| left.1.order.cmp(&right.1.order));
        lists.into_iter()
    }
}

impl Track {
    fn id(&self) -> &str {
        match self {
            Self::File { id, .. } => id,
            Self::Media(value) => value["id"].as_str().unwrap_or_default(),
        }
    }

    fn track(&self) -> Value {
        let (title, artists, album, duration, cover, codec, bitrate) = match self {
            Self::File { path, .. } => {
                let info = metadata::read(path, true);
                let cover = info.cover.and_then(|bytes| cover_url(path, &bytes));
                let title = if info.title.is_empty() {
                    path.file_stem()
                        .unwrap_or_default()
                        .to_string_lossy()
                        .into_owned()
                } else {
                    info.title
                };
                (
                    title,
                    info.artists,
                    info.album,
                    info.duration_ms,
                    cover,
                    info.codec,
                    info.bitrate,
                )
            }
            Self::Media(value) => {
                let text = |key: &str| value[key].as_str().unwrap_or_default().to_owned();
                let (info, cover) = media_metadata(value, true);
                (
                    text("title"),
                    Vec::from_iter(
                        value["artist"]
                            .as_str()
                            .filter(|s| !s.is_empty() && *s != "<unknown>")
                            .map(str::to_owned),
                    ),
                    text("album"),
                    value["durationMs"]
                        .as_u64()
                        .filter(|duration| *duration > 0)
                        .unwrap_or(info.duration_ms),
                    cover,
                    info.codec,
                    info.bitrate,
                )
            }
        };
        let artists = if artists.is_empty() {
            vec!["Unknown".into()]
        } else {
            artists
        };
        let artists: Vec<_> = artists
            .iter()
            .map(|name| json!({"id": name, "name": name}))
            .collect();
        let mut track =
            json!({"id": self.id(), "title": title, "artists": artists, "durationMs": duration});
        if !album.is_empty() && album != "<unknown>" {
            track["album"] = json!({"id": album, "title": album});
        }
        if let Some(cover) = cover {
            track["coverUrl"] = json!(cover);
        }
        if let Some(codec) = codec {
            track["codec"] = json!(codec);
        }
        if let Some(bitrate) = bitrate {
            track["bitrateKbps"] = json!(bitrate);
        }
        track
    }
}

fn media_metadata(record: &Value, include_cover: bool) -> (metadata::Metadata, Option<String>) {
    let path = record["path"]
        .as_str()
        .filter(|_| record["own"] == true)
        .map(Path::new)
        .filter(|path| fs::File::open(path).is_ok());
    let mut info = path
        .map(|path| metadata::read(path, include_cover))
        .unwrap_or_default();
    if let Some(codec) = record["codec"].as_str().filter(|codec| !codec.is_empty()) {
        info.codec = Some(codec.to_owned());
    }
    if let Some(bitrate) = record["bitrateKbps"]
        .as_u64()
        .and_then(|value| u32::try_from(value).ok())
        .filter(|value| *value > 0)
    {
        info.bitrate = Some(bitrate);
    }
    let cover = if include_cover {
        path.zip(info.cover.take())
            .and_then(|(path, bytes)| cover_url(path, &bytes))
            .or_else(|| record["coverUrl"].as_str().map(str::to_owned))
    } else {
        None
    };
    (info, cover)
}

fn cover_url(path: &Path, bytes: &[u8]) -> Option<String> {
    let extension = if bytes.starts_with(b"\x89PNG") {
        "png"
    } else if bytes.starts_with(b"\xff\xd8") {
        "jpg"
    } else if bytes.starts_with(b"GIF8") {
        "gif"
    } else if bytes.starts_with(b"BM") {
        "bmp"
    } else if bytes.starts_with(b"RIFF") && bytes.get(8..12) == Some(b"WEBP") {
        "webp"
    } else {
        return None;
    };
    let resolved = fs::canonicalize(path).ok()?;
    let info = fs::metadata(&resolved).ok()?;
    let mut key = DefaultHasher::new();
    resolved.hash(&mut key);
    info.modified().ok().hash(&mut key);
    info.len().hash(&mut key);
    let dir = config::cover_dir();
    let cover = dir.join(format!("{:016x}.{extension}", key.finish()));
    if !cover.is_file() {
        fs::create_dir_all(&dir).ok()?;
        #[cfg(target_os = "android")]
        if !cover_room(&dir, bytes.len() as u64) {
            return None;
        }
        let mut temp = tempfile::NamedTempFile::new_in(dir).ok()?;
        temp.write_all(bytes).ok()?;
        temp.persist(&cover).ok()?;
    }
    Url::from_file_path(cover).ok().map(Into::into)
}

#[cfg(target_os = "android")]
fn cover_room(dir: &Path, bytes: u64) -> bool {
    use std::os::unix::ffi::OsStrExt;

    let Ok(path) = std::ffi::CString::new(dir.as_os_str().as_bytes()) else {
        return false;
    };
    let mut stats = std::mem::MaybeUninit::<libc::statvfs>::uninit();
    // The struct is initialized when statvfs succeeds.
    if unsafe { libc::statvfs(path.as_ptr(), stats.as_mut_ptr()) } != 0 {
        return false;
    }
    let stats = unsafe { stats.assume_init() };
    stats.f_bavail.saturating_mul(stats.f_frsize) >= (1 << 30) + bytes
}

pub fn playlists(tr: impl Fn(&str) -> String) -> Value {
    let library = Library::new(tr);
    let lists: Vec<_> = library
        .lists()
        .map(|(id, list)| {
            let mut playlist = json!({
                "id": id, "title": list.title, "trackCount": list.tracks.len(), "kind": "playlist"
            });
            if let Some(parent) = &list.parent {
                playlist["parentId"] = json!(parent);
            }
            playlist
        })
        .collect();
    json!({"playlists": lists})
}

pub fn tracks(id: &str, tr: impl Fn(&str) -> String) -> Option<Value> {
    let library = Library::new(tr);
    let (_, playlist) = library.lists().find(|(key, _)| *key == id)?;
    Some(json!({"tracks": playlist.tracks.iter().map(Track::track).collect::<Vec<_>>() }))
}

fn resolve_file(root: &Path, id: &str) -> Option<Value> {
    let path = Path::new(id);
    if !path
        .components()
        .all(|part| matches!(part, Component::Normal(_)))
    {
        return None;
    }
    let root = fs::canonicalize(root).ok()?;
    let resolved = fs::canonicalize(root.join(path)).ok()?;
    if !resolved.starts_with(root) || !resolved.is_file() {
        return None;
    }
    let url = Url::from_file_path(&resolved).ok()?;
    let info = metadata::read(&resolved, false);
    let mime =
        metadata::mime(&metadata::extension(&resolved)).unwrap_or("application/octet-stream");
    let mut stream = json!({"kind": "url", "url": url.as_str(), "mimeType": mime});
    if let Some(codec) = info.codec {
        stream["codec"] = json!(codec);
    }
    if let Some(bitrate) = info.bitrate {
        stream["bitrateKbps"] = json!(bitrate);
    }
    Some(stream)
}

pub fn resolve(id: &str) -> Option<Value> {
    if let (Some(index), Some(suffix)) = (config::host_library(), id.strip_prefix("@android/")) {
        let data: Value = serde_json::from_slice(&fs::read(index).ok()?).ok()?;
        let (kind, path) = suffix.split_once('/')?;
        if kind == "media" {
            let record = data["items"]
                .as_array()?
                .iter()
                .find(|item| item["id"] == id)?;
            let uri = record["uri"].as_str()?.strip_prefix("content://media/")?;
            let mime = record["mimeType"]
                .as_str()
                .filter(|value| !value.is_empty())
                .unwrap_or("application/octet-stream");
            let (info, _) = media_metadata(record, false);
            let mut stream =
                json!({"kind": "url", "url": format!("content://media/{uri}"), "mimeType": mime});
            if let Some(codec) = info.codec {
                stream["codec"] = json!(codec);
            }
            if let Some(bitrate) = info.bitrate {
                stream["bitrateKbps"] = json!(bitrate);
            }
            return Some(stream);
        }
        let root = data["folders"]
            .as_array()?
            .iter()
            .find(|root| root["id"] == kind)?["path"]
            .as_str()?;
        return resolve_file(Path::new(root), path);
    }
    resolve_file(&config::music_root()?, id)
}
