//! The account's collection — "Liked Songs" — through spclient's
//! `/collection/v2/*` endpoints.
//!
//! Their messages (collection2v2.proto) are not among those
//! librespot-protocol compiles, and three small ones don't justify a build
//! step of our own, so they're encoded and decoded by hand here: strings,
//! varints and one nested message are all they use.

use bytes::Bytes;
use http::{HeaderMap, HeaderValue, Method};
use librespot_core::{Error, Session};

const CONTENT_TYPE: &str = "application/vnd.collection-v2.spotify.proto";
/// The set liked tracks live in.
pub const TRACKS_SET: &str = "collection";

#[derive(Debug, Clone, PartialEq)]
pub struct CollectionItem {
    pub uri: String,
    pub added_at: i32,
    pub is_removed: bool,
}

#[derive(Debug, Default, PartialEq)]
pub struct Page {
    pub items: Vec<CollectionItem>,
    pub next_page_token: String,
}

// --- wire format ---

fn put_varint(out: &mut Vec<u8>, mut value: u64) {
    while value >= 0x80 {
        out.push((value as u8) | 0x80);
        value >>= 7;
    }
    out.push(value as u8);
}

fn put_bytes(out: &mut Vec<u8>, field: u32, bytes: &[u8]) {
    put_varint(out, u64::from(field << 3 | 2));
    put_varint(out, bytes.len() as u64);
    out.extend_from_slice(bytes);
}

fn put_string(out: &mut Vec<u8>, field: u32, value: &str) {
    // proto3: an empty string is the default and isn't written.
    if !value.is_empty() {
        put_bytes(out, field, value.as_bytes());
    }
}

fn put_int(out: &mut Vec<u8>, field: u32, value: i64) {
    if value != 0 {
        put_varint(out, u64::from(field << 3));
        put_varint(out, value as u64);
    }
}

struct Reader<'a> {
    data: &'a [u8],
}

enum WireValue<'a> {
    Varint(u64),
    Bytes(&'a [u8]),
}

impl<'a> Reader<'a> {
    fn varint(&mut self) -> Option<u64> {
        let mut value = 0u64;
        for shift in (0..64).step_by(7) {
            let (&byte, rest) = self.data.split_first()?;
            self.data = rest;
            value |= u64::from(byte & 0x7f) << shift;
            if byte & 0x80 == 0 {
                return Some(value);
            }
        }
        None
    }

    /// The next field, or None at the end. Fixed-width fields (unused by
    /// these messages) are skipped.
    fn field(&mut self) -> Option<Result<(u32, WireValue<'a>), ()>> {
        if self.data.is_empty() {
            return None;
        }
        let Some(key) = self.varint() else { return Some(Err(())) };
        let number = (key >> 3) as u32;
        let value = match key & 7 {
            0 => match self.varint() {
                Some(v) => WireValue::Varint(v),
                None => return Some(Err(())),
            },
            2 => {
                let Some(len) = self.varint().map(|l| l as usize) else { return Some(Err(())) };
                if len > self.data.len() {
                    return Some(Err(()));
                }
                let (bytes, rest) = self.data.split_at(len);
                self.data = rest;
                WireValue::Bytes(bytes)
            }
            1 | 5 => {
                let width = if key & 7 == 1 { 8 } else { 4 };
                if width > self.data.len() {
                    return Some(Err(()));
                }
                self.data = &self.data[width..];
                return self.field();
            }
            _ => return Some(Err(())),
        };
        Some(Ok((number, value)))
    }
}

fn decode_error() -> Error {
    Error::data_loss("malformed collection response")
}

fn encode_item(item: &CollectionItem) -> Vec<u8> {
    let mut out = Vec::new();
    put_string(&mut out, 1, &item.uri);
    put_int(&mut out, 2, i64::from(item.added_at));
    put_int(&mut out, 3, i64::from(item.is_removed));
    out
}

fn decode_item(data: &[u8]) -> Result<CollectionItem, Error> {
    let mut item = CollectionItem { uri: String::new(), added_at: 0, is_removed: false };
    let mut reader = Reader { data };
    while let Some(field) = reader.field() {
        match field.map_err(|_| decode_error())? {
            (1, WireValue::Bytes(b)) => item.uri = String::from_utf8_lossy(b).into_owned(),
            (2, WireValue::Varint(v)) => item.added_at = v as i32,
            (3, WireValue::Varint(v)) => item.is_removed = v != 0,
            _ => {}
        }
    }
    Ok(item)
}

pub fn encode_page_request(username: &str, set: &str, pagination_token: &str, limit: i32) -> Vec<u8> {
    let mut out = Vec::new();
    put_string(&mut out, 1, username);
    put_string(&mut out, 2, set);
    put_string(&mut out, 3, pagination_token);
    put_int(&mut out, 4, i64::from(limit));
    out
}

pub fn decode_page(data: &[u8]) -> Result<Page, Error> {
    let mut page = Page::default();
    let mut reader = Reader { data };
    while let Some(field) = reader.field() {
        match field.map_err(|_| decode_error())? {
            (1, WireValue::Bytes(b)) => page.items.push(decode_item(b)?),
            (2, WireValue::Bytes(b)) => page.next_page_token = String::from_utf8_lossy(b).into_owned(),
            _ => {}
        }
    }
    Ok(page)
}

pub fn encode_write_request(username: &str, set: &str, items: &[CollectionItem]) -> Vec<u8> {
    let mut out = Vec::new();
    put_string(&mut out, 1, username);
    put_string(&mut out, 2, set);
    for item in items {
        put_bytes(&mut out, 3, &encode_item(item));
    }
    out
}

// --- requests ---

fn headers() -> HeaderMap {
    let mut headers = HeaderMap::new();
    headers.insert(http::header::CONTENT_TYPE, HeaderValue::from_static(CONTENT_TYPE));
    headers.insert(http::header::ACCEPT, HeaderValue::from_static(CONTENT_TYPE));
    headers
}

/// One page of `set`, newest first.
pub async fn page(session: &Session, set: &str, token: &str, limit: i32) -> Result<Page, Error> {
    let body = encode_page_request(&session.username(), set, token, limit);
    let response: Bytes =
        session.spclient().request(&Method::POST, "/collection/v2/paging", Some(headers()), Some(&body)).await?;
    decode_page(&response)
}

/// Adds (or, with `remove`, removes) one item of `set`.
pub async fn write(session: &Session, set: &str, uri: &str, remove: bool) -> Result<(), Error> {
    let added_at =
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs() as i32).unwrap_or(0);
    let item = CollectionItem { uri: uri.to_owned(), added_at: if remove { 0 } else { added_at }, is_removed: remove };
    let body = encode_write_request(&session.username(), set, &[item]);
    session.spclient().request(&Method::POST, "/collection/v2/write", Some(headers()), Some(&body)).await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn page_request_bytes() {
        // username "u", set "collection", no token, limit 300
        let bytes = encode_page_request("u", "collection", "", 300);
        let mut expected = vec![0x0a, 1, b'u', 0x12, 10];
        expected.extend_from_slice(b"collection");
        expected.extend_from_slice(&[0x20, 0xac, 0x02]);
        assert_eq!(bytes, expected);
    }

    #[test]
    fn page_round_trip_through_hand_built_bytes() {
        let item = CollectionItem { uri: "spotify:track:abc".into(), added_at: 1_700_000_000, is_removed: false };
        let mut data = Vec::new();
        put_bytes(&mut data, 1, &encode_item(&item));
        put_bytes(&mut data, 1, &encode_item(&CollectionItem { is_removed: true, ..item.clone() }));
        put_string(&mut data, 2, "next");
        // an unknown fixed64 field and a sync token, both skipped
        data.extend_from_slice(&[0x29, 0, 0, 0, 0, 0, 0, 0, 0]);
        put_string(&mut data, 3, "sync");
        let page = decode_page(&data).unwrap();
        assert_eq!(page.items.len(), 2);
        assert_eq!(page.items[0], item);
        assert!(page.items[1].is_removed);
        assert_eq!(page.next_page_token, "next");
    }

    #[test]
    fn truncated_data_is_an_error() {
        assert!(decode_page(&[0x0a, 5, b'a']).is_err());
    }

    #[test]
    fn write_request_bytes() {
        let item = CollectionItem { uri: "x".into(), added_at: 0, is_removed: true };
        let bytes = encode_write_request("u", "collection", &[item]);
        assert!(bytes.ends_with(&[0x1a, 5, 0x0a, 1, b'x', 0x18, 1]));
    }
}
