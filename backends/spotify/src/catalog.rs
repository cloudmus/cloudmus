//! The library: the account's playlists (its rootlist), their tracks, and
//! Liked Songs — everything read through spclient, with track metadata
//! fetched in batches.

use std::collections::HashSet;
use std::time::{Duration, Instant};

use librespot_core::{Error, FileId, Session, SpotifyUri};
use librespot_metadata::image::{ImageSize, Images};
use librespot_metadata::{Metadata, Playlist as SpotifyPlaylist, Track as SpotifyTrack};
use librespot_protocol::extended_metadata::{BatchedEntityRequest, EntityRequest, ExtensionQuery};
use librespot_protocol::extension_kind::ExtensionKind;
use librespot_protocol::playlist4_external::SelectedListContent;
use protobuf::{EnumOrUnknown, Message};
use tokio::sync::Mutex;

use crate::collection;
use crate::generated::models::{Album, Artist, ListTracksResult, Playlist, Track};
use crate::i18n::tr;

pub const LIKED_PLAYLIST_ID: &str = "__liked__";
/// Tracks per listTracks/listLiked page: one metadata batch.
const PAGE_SIZE: usize = 100;
/// Liked Songs are re-read when older than this; likes made here update
/// the copy directly.
const LIKED_MAX_AGE: Duration = Duration::from_secs(60);
const LIKED_PAGE_LIMIT: i32 = 1000;
const ROOTLIST_LENGTH: usize = 1000;

pub fn image_url(id: &FileId) -> Option<String> {
    id.to_base16().ok().map(|hex| format!("https://i.scdn.co/image/{hex}"))
}

fn largest_cover(images: &Images) -> Option<String> {
    let rank = |size: &ImageSize| match size {
        ImageSize::XLARGE => 3,
        ImageSize::LARGE => 2,
        ImageSize::DEFAULT => 1,
        ImageSize::SMALL => 0,
    };
    images.iter().max_by_key(|i| rank(&i.size)).and_then(|i| image_url(&i.id))
}

pub fn track_uri(track_id: &str) -> Result<SpotifyUri, Error> {
    SpotifyUri::from_uri(&format!("spotify:track:{track_id}"))
}

pub fn to_track(track: &SpotifyTrack, liked: Option<bool>) -> Option<Track> {
    let id = track.id.to_id().ok()?;
    let cover_url = largest_cover(&track.album.covers);
    Some(Track {
        title: track.name.clone(),
        artists: track
            .artists
            .iter()
            .map(|a| Artist { id: a.id.to_id().unwrap_or_default(), name: a.name.clone() })
            .collect(),
        album: Some(Album {
            id: track.album.id.to_id().unwrap_or_default(),
            title: track.album.name.clone(),
            cover_url: cover_url.clone(),
        }),
        duration_ms: i64::from(track.duration),
        cover_url,
        explicit: Some(track.is_explicit),
        liked,
        web_url: Some(format!("https://open.spotify.com/track/{id}")),
        id,
        ..Default::default()
    })
}

/// Metadata of `uris` in as few requests as the batch allows; tracks
/// Spotify returns nothing for (pulled from the catalog) are left out.
pub async fn tracks(session: &Session, uris: &[SpotifyUri]) -> Result<Vec<SpotifyTrack>, Error> {
    let mut out = Vec::with_capacity(uris.len());
    for chunk in uris.chunks(PAGE_SIZE) {
        let request = BatchedEntityRequest {
            entity_request: chunk
                .iter()
                .filter_map(|uri| uri.to_uri().ok())
                .map(|entity_uri| EntityRequest {
                    entity_uri,
                    query: vec![ExtensionQuery {
                        extension_kind: EnumOrUnknown::new(ExtensionKind::TRACK_V4),
                        ..Default::default()
                    }],
                    ..Default::default()
                })
                .collect(),
            ..Default::default()
        };
        let response = session.spclient().get_extended_metadata(request).await?;
        for array in &response.extended_metadata {
            for data in &array.extension_data {
                let Some(any) = data.extension_data.as_ref() else { continue };
                let Ok(uri) = SpotifyUri::from_uri(&data.entity_uri) else { continue };
                let parsed = <SpotifyTrack as Metadata>::Message::parse_from_bytes(&any.value)
                    .map_err(Error::from)
                    .and_then(|msg| SpotifyTrack::parse(&msg, &uri));
                match parsed {
                    Ok(track) => out.push(track),
                    Err(e) => log::warn!("skipping {}: {e}", data.entity_uri),
                }
            }
        }
    }
    // The batch answers in no particular order.
    let position = |t: &SpotifyTrack| uris.iter().position(|u| *u == t.id).unwrap_or(usize::MAX);
    out.sort_by_key(position);
    Ok(out)
}

/// The account's liked track uris, newest first, kept for a short while:
/// listPlaylists needs their count and every track list their like state.
#[derive(Default)]
pub struct Liked {
    cached: Mutex<Option<(Vec<String>, Instant)>>,
}

impl Liked {
    pub async fn uris(&self, session: &Session, fresh: bool) -> Result<Vec<String>, Error> {
        let mut cached = self.cached.lock().await;
        if let Some((uris, at)) = cached.as_ref() {
            if !fresh && at.elapsed() < LIKED_MAX_AGE {
                return Ok(uris.clone());
            }
        }
        let mut uris = Vec::new();
        let mut token = String::new();
        loop {
            let page = collection::page(session, collection::TRACKS_SET, &token, LIKED_PAGE_LIMIT).await?;
            uris.extend(
                page.items.into_iter().filter(|i| !i.is_removed && i.uri.starts_with("spotify:track:")).map(|i| i.uri),
            );
            if page.next_page_token.is_empty() {
                break;
            }
            token = page.next_page_token;
        }
        *cached = Some((uris.clone(), Instant::now()));
        Ok(uris)
    }

    pub async fn set(&self, uri: &str, liked: bool) {
        if let Some((uris, _)) = self.cached.lock().await.as_mut() {
            uris.retain(|u| u != uri);
            if liked {
                uris.insert(0, uri.to_owned());
            }
        }
    }
}

fn parse_offset(cursor: Option<&str>) -> usize {
    cursor.and_then(|c| c.parse().ok()).unwrap_or(0)
}

/// One page of `uris` starting at the cursor, as protocol tracks.
async fn page_of(
    session: &Session,
    uris: &[SpotifyUri],
    cursor: Option<&str>,
    liked: &HashSet<String>,
) -> Result<ListTracksResult, Error> {
    let offset = parse_offset(cursor).min(uris.len());
    let end = (offset + PAGE_SIZE).min(uris.len());
    let metadata = tracks(session, &uris[offset..end]).await?;
    let tracks = metadata
        .iter()
        .filter_map(|t| {
            let uri = t.id.to_uri().ok()?;
            to_track(t, Some(liked.contains(&uri)))
        })
        .collect();
    Ok(ListTracksResult { tracks, next_cursor: (end < uris.len()).then(|| end.to_string()) })
}

pub async fn list_playlists(session: &Session, liked: &Liked) -> Result<Vec<Playlist>, Error> {
    let liked_count = liked.uris(session, true).await?.len();
    let mut playlists = vec![Playlist {
        id: LIKED_PLAYLIST_ID.into(),
        title: tr("Liked Songs"),
        track_count: liked_count as i64,
        kind: "liked".into(),
        ..Default::default()
    }];

    let bytes = session.spclient().get_rootlist(0, Some(ROOTLIST_LENGTH)).await?;
    let rootlist = SelectedListContent::parse_from_bytes(&bytes)?;
    let contents = rootlist.contents.as_ref();
    let items = contents.map(|c| c.items.as_slice()).unwrap_or_default();
    let meta = contents.map(|c| c.meta_items.as_slice()).unwrap_or_default();
    let username = session.username();
    for (index, item) in items.iter().enumerate() {
        // Folders come as start-group/end-group markers around their playlists.
        let Ok(SpotifyUri::Playlist { id, .. }) = SpotifyUri::from_uri(item.uri()) else { continue };
        let Ok(id) = id.to_base62() else { continue };
        let meta = meta.get(index);
        let attributes = meta.and_then(|m| m.attributes.as_ref());
        let picture = attributes.map(|a| a.picture()).filter(|p| !p.is_empty()).map(FileId::from_raw);
        playlists.push(Playlist {
            id,
            title: attributes
                .map(|a| a.name().to_owned())
                .filter(|n| !n.is_empty())
                .unwrap_or_else(|| tr("(untitled)")),
            description: attributes.map(|a| a.description().to_owned()).filter(|d| !d.is_empty()),
            cover_url: picture.as_ref().and_then(image_url),
            track_count: meta.map(|m| i64::from(m.length())).unwrap_or(0),
            kind: "playlist".into(),
            editable: Some(meta.is_some_and(|m| m.owner_username() == username)),
            ..Default::default()
        });
    }
    Ok(playlists)
}

pub async fn list_tracks(
    session: &Session,
    liked: &Liked,
    playlist_id: &str,
    cursor: Option<&str>,
) -> Result<ListTracksResult, Error> {
    if playlist_id == LIKED_PLAYLIST_ID {
        return list_liked(session, liked, cursor).await;
    }
    let uri = SpotifyUri::from_uri(&format!("spotify:playlist:{playlist_id}"))?;
    let playlist = SpotifyPlaylist::get(session, &uri).await?;
    // Episodes and local files share playlists with tracks; only tracks play here.
    let uris: Vec<SpotifyUri> = playlist.tracks().filter(|u| matches!(u, SpotifyUri::Track { .. })).cloned().collect();
    let liked: HashSet<String> = liked.uris(session, false).await?.into_iter().collect();
    page_of(session, &uris, cursor, &liked).await
}

pub async fn list_liked(session: &Session, liked: &Liked, cursor: Option<&str>) -> Result<ListTracksResult, Error> {
    // The first page re-reads the list: it's what the user opened.
    let all = liked.uris(session, cursor.is_none()).await?;
    let uris: Vec<SpotifyUri> = all.iter().filter_map(|u| SpotifyUri::from_uri(u).ok()).collect();
    let set: HashSet<String> = all.into_iter().collect();
    page_of(session, &uris, cursor, &set).await
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cursors_are_offsets() {
        assert_eq!(parse_offset(None), 0);
        assert_eq!(parse_offset(Some("200")), 200);
        assert_eq!(parse_offset(Some("junk")), 0);
    }

    #[test]
    fn cover_urls() {
        let id = FileId::from_raw(&[0xab; 20]);
        assert_eq!(image_url(&id).unwrap(), format!("https://i.scdn.co/image/{}", "ab".repeat(20)));
    }
}
