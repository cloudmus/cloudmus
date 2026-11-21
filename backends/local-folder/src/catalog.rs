use std::collections::{BTreeMap, HashSet};
use std::fs;
use std::hash::{DefaultHasher, Hash, Hasher};
use std::io::Write;
use std::path::{Component, Path, PathBuf};

use serde_json::{json, Value};
use url::Url;

use crate::{config, metadata};

const ROOT: &str = "__root__";

struct Playlist {
    title: String,
    tracks: Vec<Track>,
}

enum Track {
    File { id: String, path: PathBuf },
    Media(Value),
}

struct Library {
    playlists: BTreeMap<String, Playlist>,
    loose: Option<Playlist>,
    seen: HashSet<PathBuf>,
}

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
            loose: None,
            seen: HashSet::new(),
        };
        if let Some(root) = config::music_root() {
            library.add_root(&root, "", tr("Root"), tr("Library"), None);
        }
        if let Some(index) = config::android_index() {
            if let Ok(bytes) = fs::read(&index) {
                match serde_json::from_slice::<Value>(&bytes) {
                    Ok(data) => library.add_android(&data, tr),
                    Err(error) => eprintln!("local-folder: read Android index: {error}"),
                }
            }
        }
        library
    }

    fn add_android(&mut self, data: &Value, tr: impl Fn(&str) -> String) {
        if let Some(roots) = data["roots"].as_array() {
            for root in roots {
                let (Some(id), Some(path)) = (root["id"].as_str(), root["path"].as_str()) else {
                    continue;
                };
                self.add_root(
                    Path::new(path),
                    &format!("@android/{id}/"),
                    tr("Saved tracks"),
                    tr("Saved tracks"),
                    Some(tr("Saved tracks")),
                );
            }
        }
        if let Some(records) = data["records"].as_array() {
            let mut seen = HashSet::new();
            for record in records {
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
                let (key, title) = if record["saved"] == true {
                    let folder = record["relativePath"]
                        .as_str()
                        .and_then(|path| path.strip_prefix("Music/CloudMus/"))
                        .and_then(|path| path.split('/').next())
                        .filter(|folder| !folder.is_empty());
                    let title = tr("Saved tracks");
                    match folder {
                        Some(folder) => (
                            format!("@android/saved-media/{folder}"),
                            format!("{title} · {folder}"),
                        ),
                        None => ("@android/saved-media".into(), title),
                    }
                } else {
                    ("@android/device-media".into(), tr("Device music"))
                };
                self.playlists
                    .entry(key)
                    .or_insert_with(|| Playlist {
                        title,
                        tracks: Vec::new(),
                    })
                    .tracks
                    .push(Track::Media(record.clone()));
            }
        }
    }

    fn add_root(
        &mut self,
        root: &Path,
        prefix: &str,
        root_title: String,
        library_title: String,
        fixed_title: Option<String>,
    ) {
        let canonical = match fs::canonicalize(root) {
            Ok(path) => path,
            Err(error) => {
                if error.kind() != std::io::ErrorKind::NotFound {
                    eprintln!("local-folder: canonicalize {}: {error}", root.display());
                }
                return;
            }
        };
        let mut loose = Vec::new();
        let mut has_subdirs = false;
        for path in entries(root) {
            if path.is_dir() {
                has_subdirs = true;
                let Some(name) = path.file_name().and_then(|name| name.to_str()) else {
                    continue;
                };
                let mut tracks = Vec::new();
                let mut directories = vec![path.clone()];
                while let Some(directory) = directories.pop() {
                    if !fs::canonicalize(&directory).is_ok_and(|path| path.starts_with(&canonical))
                    {
                        continue;
                    }
                    for entry in entries(&directory) {
                        if entry
                            .symlink_metadata()
                            .is_ok_and(|metadata| metadata.is_dir())
                        {
                            directories.push(entry);
                        } else if let Some(track) = self.file(root, &canonical, prefix, entry) {
                            tracks.push(track);
                        }
                    }
                }
                tracks.sort_by(|left, right| left.id().cmp(right.id()));
                if !tracks.is_empty() {
                    let title = match &fixed_title {
                        Some(label) => format!("{label} · {name}"),
                        None => name.into(),
                    };
                    self.playlists
                        .insert(format!("{prefix}{name}"), Playlist { title, tracks });
                }
            } else if let Some(track) = self.file(root, &canonical, prefix, path) {
                loose.push(track);
            }
        }
        if !loose.is_empty() {
            let title = fixed_title
                .or_else(|| {
                    root.file_name()
                        .and_then(|name| name.to_str())
                        .map(str::to_owned)
                })
                .unwrap_or(if has_subdirs {
                    root_title
                } else {
                    library_title
                });
            let playlist = Playlist {
                title,
                tracks: loose,
            };
            if prefix.is_empty() {
                self.loose = Some(playlist);
            } else {
                self.playlists.insert(format!("{prefix}{ROOT}"), playlist);
            }
        }
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
        if config::android_index().is_some() && !self.seen.insert(resolved.clone()) {
            return None;
        }
        Some(Track::File {
            id: format!("{prefix}{id}"),
            path: entry,
        })
    }

    fn lists(&self) -> impl Iterator<Item = (&str, &Playlist)> {
        self.playlists
            .iter()
            .map(|(id, list)| (id.as_str(), list))
            .chain(self.loose.iter().map(|list| (ROOT, list)))
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
        .filter(|_| record["saved"] == true)
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
            json!({
                "id": id, "title": list.title, "trackCount": list.tracks.len(), "kind": "playlist"
            })
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
    if let (Some(index), Some(suffix)) = (config::android_index(), id.strip_prefix("@android/")) {
        let data: Value = serde_json::from_slice(&fs::read(index).ok()?).ok()?;
        let (kind, path) = suffix.split_once('/')?;
        if kind == "media" {
            let record = data["records"]
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
        let root = data["roots"]
            .as_array()?
            .iter()
            .find(|root| root["id"] == kind)?["path"]
            .as_str()?;
        return resolve_file(Path::new(root), path);
    }
    resolve_file(&config::music_root()?, id)
}
