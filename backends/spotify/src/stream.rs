//! Spotify's audio, served to the front on 127.0.0.1.
//!
//! Spotify hands out encrypted files from its CDN; librespot fetches them
//! (`AudioFile`, which reads ahead and seeks by requesting ranges) and
//! decrypts them (`AudioDecrypt`). This module puts an HTTP server in front
//! of that, so to the front a Spotify track is an ordinary stream URL
//! (`providesStream`): mpv plays it, seeks in it with Range requests, and
//! the front saves tracks from it the same way.
//!
//! The URL carries a random token of this process: the port is open to
//! every local user, the account's streams shouldn't be.

use std::collections::VecDeque;
use std::convert::Infallible;
use std::io::{Read, Seek, SeekFrom};
use std::net::SocketAddr;
use std::sync::{Arc, Mutex};

use bytes::Bytes;
use futures_util::stream;
use http::{header, Method, Request, Response, StatusCode};
use http_body_util::{combinators::BoxBody, BodyExt, Empty, StreamBody};
use hyper::body::{Frame, Incoming};
use hyper::server::conn::http1;
use hyper::service::service_fn;
use hyper_util::rt::TokioIo;
use librespot_audio::{AudioDecrypt, AudioFile};
use librespot_core::{Error, Session, SpotifyId, SpotifyUri};
use librespot_metadata::audio::{AudioFileFormat, AudioFiles, AudioItem};
use tokio::net::TcpListener;
use tokio::sync::mpsc;

use crate::generated::models::StreamDescriptor;

/// Spotify's Ogg files start with a header of its own (holding loudness
/// data) that no decoder knows; real Ogg begins here (librespot-playback's
/// SPOTIFY_OGG_HEADER_END).
const SPOTIFY_OGG_HEADER_END: u64 = 0xa7;
/// Tracks kept open: the one playing, the one preloaded next, and a few the
/// user may go back to.
const OPEN_TRACKS: usize = 6;
const CHUNK: usize = 64 * 1024;

/// The quality setting: a nominal bitrate and the formats to try for it,
/// nearest first (as librespot-playback does).
pub fn formats_for(quality: &str) -> &'static [AudioFileFormat] {
    use AudioFileFormat::*;
    match quality {
        "96" => &[OGG_VORBIS_96, MP3_96, OGG_VORBIS_160, MP3_160, MP3_256, OGG_VORBIS_320, MP3_320],
        "160" => &[OGG_VORBIS_160, MP3_160, OGG_VORBIS_96, MP3_96, MP3_256, OGG_VORBIS_320, MP3_320],
        _ => &[OGG_VORBIS_320, MP3_320, MP3_256, OGG_VORBIS_160, MP3_160, OGG_VORBIS_96, MP3_96],
    }
}

fn bitrate_kbps(format: AudioFileFormat) -> i64 {
    use AudioFileFormat::*;
    match format {
        OGG_VORBIS_96 | MP3_96 => 96,
        OGG_VORBIS_160 | MP3_160 | MP3_160_ENC => 160,
        MP3_256 => 256,
        _ => 320,
    }
}

/// Bytes per second librespot should fetch ahead at.
fn data_rate(format: AudioFileFormat) -> usize {
    (bitrate_kbps(format) as usize * 1024).div_ceil(8)
}

/// Why a track won't play: told to the listener as "unavailable" (§9.1),
/// unlike a network failure.
#[derive(Debug)]
pub struct Unplayable(pub String);

pub enum ResolveError {
    Unplayable(Unplayable),
    Upstream(Error),
}

impl From<Error> for ResolveError {
    fn from(e: Error) -> Self {
        ResolveError::Upstream(e)
    }
}

struct OpenTrack {
    key: String,
    file: Mutex<AudioDecrypt<AudioFile>>,
    /// Where the playable data starts in the file, and how long it is.
    offset: u64,
    len: u64,
    mime_type: &'static str,
}

pub struct StreamServer {
    addr: SocketAddr,
    token: String,
    tracks: Mutex<VecDeque<Arc<OpenTrack>>>,
}

/// A byte range of a `len`-byte body: (start, end inclusive).
pub fn parse_range(value: &str, len: u64) -> Option<(u64, u64)> {
    let spec = value.trim().strip_prefix("bytes=")?;
    // Several ranges in one request: mpv never asks for that; serve the first.
    let spec = spec.split(',').next()?.trim();
    let (start, end) = spec.split_once('-')?;
    if len == 0 {
        return None;
    }
    let (start, end) = if start.is_empty() {
        let suffix: u64 = end.parse().ok()?;
        (len.saturating_sub(suffix), len - 1)
    } else {
        let start: u64 = start.parse().ok()?;
        let end = if end.is_empty() { len - 1 } else { end.parse::<u64>().ok()?.min(len - 1) };
        (start, end)
    };
    (start <= end && start < len).then_some((start, end))
}

impl StreamServer {
    pub async fn start() -> std::io::Result<Arc<Self>> {
        let listener = TcpListener::bind(("127.0.0.1", 0)).await?;
        let mut token = [0u8; 16];
        rand::RngCore::fill_bytes(&mut rand::rng(), &mut token);
        let server = Arc::new(StreamServer {
            addr: listener.local_addr()?,
            token: token.iter().map(|b| format!("{b:02x}")).collect(),
            tracks: Mutex::new(VecDeque::new()),
        });
        log::info!("streams served on {}", server.addr);
        let accepting = server.clone();
        tokio::spawn(async move {
            loop {
                let (socket, _) = match listener.accept().await {
                    Ok(conn) => conn,
                    Err(e) => {
                        log::warn!("stream server accept: {e}");
                        continue;
                    }
                };
                let server = accepting.clone();
                tokio::spawn(async move {
                    let service = service_fn(move |req| {
                        let server = server.clone();
                        async move { Ok::<_, Infallible>(server.handle(req)) }
                    });
                    if let Err(e) = http1::Builder::new().serve_connection(TokioIo::new(socket), service).await {
                        log::debug!("stream connection ended: {e}");
                    }
                });
            }
        });
        Ok(server)
    }

    /// Finds a playable file of the track (or of one of its alternatives,
    /// Spotify's stand-ins for a release unavailable in this country),
    /// opens it, and returns the URL it is served at.
    pub async fn resolve(
        &self,
        session: &Session,
        track_id: &str,
        quality: &str,
    ) -> Result<StreamDescriptor, ResolveError> {
        let uri = crate::catalog::track_uri(track_id)?;
        let item = playable_item(session, uri).await?;
        let Some((format, file_id)) = formats_for(quality).iter().find_map(|f| item.files.get(f).map(|id| (*f, *id)))
        else {
            return Err(ResolveError::Unplayable(Unplayable(format!("{} has no format CloudMus plays", item.name))));
        };
        let key = format!("{track_id}-{format:?}");
        let mime_type = AudioFiles::mime_type(format).unwrap_or("application/octet-stream");
        let codec = if AudioFiles::is_ogg_vorbis(format) { "vorbis" } else { "mp3" };

        if self.find(&key).is_none() {
            let spotify_id = SpotifyId::try_from(&item.track_id)?;
            let file = AudioFile::open(session, file_id, data_rate(format)).await?;
            let file_len = file.get_stream_loader_controller()?.len() as u64;
            let audio_key = match session.audio_key().request(spotify_id, file_id).await {
                Ok(k) => Some(k),
                Err(e) => {
                    // As librespot-playback: some files aren't encrypted.
                    log::warn!("no key for {track_id}, serving it undecrypted: {e}");
                    None
                }
            };
            let offset = if AudioFiles::is_ogg_vorbis(format) { SPOTIFY_OGG_HEADER_END } else { 0 };
            let track = Arc::new(OpenTrack {
                key: key.clone(),
                file: Mutex::new(AudioDecrypt::new(audio_key, file)),
                offset,
                len: file_len.saturating_sub(offset),
                mime_type,
            });
            let mut tracks = self.tracks.lock().unwrap();
            tracks.push_front(track);
            tracks.truncate(OPEN_TRACKS);
        }

        Ok(StreamDescriptor {
            kind: "url".into(),
            url: format!("http://{}/{}/{key}", self.addr, self.token),
            mime_type: mime_type.into(),
            codec: Some(codec.into()),
            bitrate_kbps: Some(bitrate_kbps(format)),
            // One reader per open file: parallel ranges would only take turns.
            max_connections: Some(1),
            ..Default::default()
        })
    }

    fn find(&self, key: &str) -> Option<Arc<OpenTrack>> {
        let mut tracks = self.tracks.lock().unwrap();
        let index = tracks.iter().position(|t| t.key == key)?;
        // Most recently used first: the one evicted is the one least likely wanted.
        let track = tracks.remove(index)?;
        tracks.push_front(track.clone());
        Some(track)
    }

    fn handle(&self, req: Request<Incoming>) -> Response<BoxBody<Bytes, std::io::Error>> {
        let status = |code: StatusCode| {
            Response::builder().status(code).body(Empty::new().map_err(|e| match e {}).boxed()).unwrap()
        };
        if req.method() != Method::GET && req.method() != Method::HEAD {
            return status(StatusCode::METHOD_NOT_ALLOWED);
        }
        let path = req.uri().path().trim_start_matches('/');
        let Some((token, key)) = path.split_once('/') else { return status(StatusCode::NOT_FOUND) };
        if token != self.token {
            return status(StatusCode::NOT_FOUND);
        }
        let Some(track) = self.find(key) else { return status(StatusCode::NOT_FOUND) };

        let range = req.headers().get(header::RANGE).and_then(|v| v.to_str().ok());
        let (code, start, end) = match range {
            None => (StatusCode::OK, 0, track.len.saturating_sub(1)),
            Some(value) => match parse_range(value, track.len) {
                Some((start, end)) => (StatusCode::PARTIAL_CONTENT, start, end),
                None => {
                    return Response::builder()
                        .status(StatusCode::RANGE_NOT_SATISFIABLE)
                        .header(header::CONTENT_RANGE, format!("bytes */{}", track.len))
                        .body(Empty::new().map_err(|e| match e {}).boxed())
                        .unwrap();
                }
            },
        };
        let length = if track.len == 0 { 0 } else { end - start + 1 };
        let mut response = Response::builder()
            .status(code)
            .header(header::CONTENT_TYPE, track.mime_type)
            .header(header::ACCEPT_RANGES, "bytes")
            .header(header::CONTENT_LENGTH, length);
        if code == StatusCode::PARTIAL_CONTENT {
            response = response.header(header::CONTENT_RANGE, format!("bytes {start}-{end}/{}", track.len));
        }
        if req.method() == Method::HEAD || length == 0 {
            return response.body(Empty::new().map_err(|e| match e {}).boxed()).unwrap();
        }
        response.body(body(track, start, length)).unwrap()
    }
}

/// The track's bytes [start, start + length), read on a blocking thread:
/// AudioFile's reads wait for the download.
fn body(track: Arc<OpenTrack>, start: u64, length: u64) -> BoxBody<Bytes, std::io::Error> {
    let (tx, rx) = mpsc::channel::<std::io::Result<Bytes>>(4);
    tokio::task::spawn_blocking(move || {
        let mut position = start;
        let end = start + length;
        let mut buf = vec![0u8; CHUNK];
        while position < end {
            let want = CHUNK.min((end - position) as usize);
            let read = {
                let mut file = track.file.lock().unwrap();
                // Seeked every time: another request may have moved it.
                file.seek(SeekFrom::Start(track.offset + position)).and_then(|_| file.read(&mut buf[..want]))
            };
            let chunk = match read {
                Ok(0) => break,
                Ok(n) => {
                    position += n as u64;
                    Ok(Bytes::copy_from_slice(&buf[..n]))
                }
                Err(e) => Err(e),
            };
            let failed = chunk.is_err();
            if tx.blocking_send(chunk).is_err() || failed {
                break; // the player hung up, or the download failed
            }
        }
    });
    let frames = stream::unfold(rx, |mut rx| async move { rx.recv().await.map(|chunk| (chunk.map(Frame::data), rx)) });
    StreamBody::new(frames).boxed()
}

/// The track, or failing that an available alternative of it.
async fn playable_item(session: &Session, uri: SpotifyUri) -> Result<AudioItem, ResolveError> {
    let item = AudioItem::get_file(session, uri).await?;
    if let Err(reason) = &item.availability {
        return Err(ResolveError::Unplayable(Unplayable(format!("{}: {reason}", item.name))));
    }
    if !item.files.is_empty() {
        return Ok(item);
    }
    for alternative in item.alternatives.iter().flat_map(|a| a.0.iter()) {
        match AudioItem::get_file(session, alternative.clone()).await {
            Ok(alt) if alt.availability.is_ok() && !alt.files.is_empty() => return Ok(alt),
            Ok(_) => {}
            Err(e) => log::debug!("alternative {alternative:?}: {e}"),
        }
    }
    Err(ResolveError::Unplayable(Unplayable(format!("{} isn't available in your country", item.name))))
}

#[cfg(test)]
mod tests {
    use super::*;

    impl StreamServer {
        /// Serves a local file as if it were a resolved Spotify track.
        fn insert_file(&self, key: &str, file: std::fs::File, offset: u64) -> String {
            let len = file.metadata().unwrap().len() - offset;
            self.tracks.lock().unwrap().push_front(Arc::new(OpenTrack {
                key: key.into(),
                file: Mutex::new(AudioDecrypt::new(None, AudioFile::Cached(file))),
                offset,
                len,
                mime_type: "audio/ogg",
            }));
            format!("http://{}/{}/{key}", self.addr, self.token)
        }
    }

    /// A bare HTTP/1.1 GET over a TcpStream: status line, headers, body.
    async fn get(url: &str, range: Option<&str>) -> (String, String, Vec<u8>) {
        use tokio::io::{AsyncReadExt, AsyncWriteExt};
        let url = url::Url::parse(url).unwrap();
        let mut socket = tokio::net::TcpStream::connect((url.host_str().unwrap(), url.port().unwrap())).await.unwrap();
        let range = range.map(|r| format!("Range: {r}\r\n")).unwrap_or_default();
        let request = format!("GET {} HTTP/1.1\r\nHost: x\r\n{range}Connection: close\r\n\r\n", url.path());
        socket.write_all(request.as_bytes()).await.unwrap();
        let mut raw = Vec::new();
        socket.read_to_end(&mut raw).await.unwrap();
        let split = raw.windows(4).position(|w| w == b"\r\n\r\n").unwrap();
        let head = String::from_utf8_lossy(&raw[..split]).into_owned();
        let (status, headers) = head.split_once("\r\n").unwrap_or((&head, ""));
        (status.to_owned(), headers.to_lowercase(), raw[split + 4..].to_vec())
    }

    #[tokio::test]
    async fn serves_ranges_past_spotifys_header() {
        let server = StreamServer::start().await.unwrap();
        let mut path = std::env::temp_dir();
        path.push(format!("cloudmus-stream-test-{}", std::process::id()));
        let payload: Vec<u8> = (0..300_000u32).map(|i| (i % 251) as u8).collect();
        let mut data = vec![0xffu8; SPOTIFY_OGG_HEADER_END as usize];
        data.extend_from_slice(&payload);
        std::fs::write(&path, &data).unwrap();
        let url = server.insert_file("t1", std::fs::File::open(&path).unwrap(), SPOTIFY_OGG_HEADER_END);

        let (status, headers, body) = get(&url, None).await;
        assert!(status.contains("200"), "{status}");
        assert!(headers.contains(&format!("content-length: {}", payload.len())));
        assert!(headers.contains("accept-ranges: bytes"));
        assert_eq!(body, payload);

        let (status, headers, body) = get(&url, Some("bytes=1000-1999")).await;
        assert!(status.contains("206"), "{status}");
        assert!(headers.contains(&format!("content-range: bytes 1000-1999/{}", payload.len())));
        assert_eq!(body, &payload[1000..2000]);

        let (status, _, _) = get(&url, Some("bytes=999999999-")).await;
        assert!(status.contains("416"), "{status}");

        // a wrong token, or a track not open
        let wrong = url.replace(&server.token, "0000");
        assert!(get(&wrong, None).await.0.contains("404"));
        assert!(get(&url.replace("/t1", "/t2"), None).await.0.contains("404"));
        std::fs::remove_file(&path).unwrap();
    }

    #[test]
    fn ranges() {
        assert_eq!(parse_range("bytes=0-", 100), Some((0, 99)));
        assert_eq!(parse_range("bytes=10-19", 100), Some((10, 19)));
        assert_eq!(parse_range("bytes=90-200", 100), Some((90, 99)));
        assert_eq!(parse_range("bytes=-10", 100), Some((90, 99)));
        assert_eq!(parse_range("bytes=100-", 100), None);
        assert_eq!(parse_range("bytes=5-1", 100), None);
        assert_eq!(parse_range("items=0-1", 100), None);
        assert_eq!(parse_range("bytes=0-1,5-6", 100), Some((0, 1)));
    }

    #[test]
    fn quality_picks_nearest_format_first() {
        assert_eq!(formats_for("96")[0], AudioFileFormat::OGG_VORBIS_96);
        assert_eq!(formats_for("160")[0], AudioFileFormat::OGG_VORBIS_160);
        assert_eq!(formats_for("320")[0], AudioFileFormat::OGG_VORBIS_320);
        assert_eq!(bitrate_kbps(AudioFileFormat::MP3_256), 256);
        assert_eq!(data_rate(AudioFileFormat::OGG_VORBIS_160), 20 * 1024);
    }
}
