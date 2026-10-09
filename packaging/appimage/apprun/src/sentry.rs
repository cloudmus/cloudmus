//! Just enough of Sentry's wire format to send an event with attachments, or
//! to forward an envelope the app's own SDK left on disk.

use crate::log::debug;
use serde_json::{json, Value};
use std::time::Duration;

/// `scheme://key[:secret]@host[:port][/prefix]/project`
#[derive(Debug, PartialEq)]
pub struct Dsn {
    pub scheme: String,
    pub key: String,
    pub host: String,
    pub prefix: String,
    pub project: String,
}

impl Dsn {
    pub fn parse(text: &str) -> Option<Dsn> {
        let (scheme, rest) = text.trim().split_once("://")?;
        let (authority, path) = rest.split_once('/')?;
        let (userinfo, host) = authority.rsplit_once('@')?;
        let key = userinfo.split(':').next()?;
        let (prefix, project) = match path.trim_end_matches('/').rsplit_once('/') {
            Some((prefix, project)) => (format!("/{prefix}"), project),
            None => (String::new(), path.trim_end_matches('/')),
        };
        if !matches!(scheme, "http" | "https") || key.is_empty() || host.is_empty() || project.is_empty() {
            return None;
        }
        Some(Dsn {
            scheme: scheme.to_string(),
            key: key.to_string(),
            host: host.to_string(),
            prefix,
            project: project.to_string(),
        })
    }

    pub fn envelope_url(&self) -> String {
        format!("{}://{}{}/api/{}/envelope/", self.scheme, self.host, self.prefix, self.project)
    }

    fn auth_header(&self) -> String {
        format!(
            "Sentry sentry_version=7, sentry_key={}, sentry_client=cloudmus-apprun/{}",
            self.key,
            env!("CARGO_PKG_VERSION")
        )
    }
}

/// 32 hex digits, as Sentry wants an event id.
pub fn new_event_id() -> String {
    let mut bytes = [0u8; 16];
    let filled = std::fs::File::open("/dev/urandom").and_then(|mut f| std::io::Read::read_exact(&mut f, &mut bytes));
    if filled.is_err() {
        // Only needs to be unlikely to repeat.
        let nanos = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map_or(0, |d| d.as_nanos());
        bytes = (nanos ^ (u128::from(std::process::id()) << 96)).to_le_bytes();
    }
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

pub struct Item {
    header: Value,
    payload: Vec<u8>,
}

impl Item {
    pub fn event(event: &Value) -> Item {
        Item { header: json!({"type": "event"}), payload: event.to_string().into_bytes() }
    }

    pub fn attachment(filename: &str, payload: Vec<u8>) -> Item {
        Item {
            header: json!({"type": "attachment", "filename": filename, "content_type": "text/plain"}),
            payload,
        }
    }
}

/// The envelope format: a header line, then per item a header line (with the
/// payload's length), the payload, and a newline.
pub fn build_envelope(event_id: &str, dsn: &str, items: Vec<Item>) -> Vec<u8> {
    let mut out = json!({"event_id": event_id, "dsn": dsn}).to_string().into_bytes();
    out.push(b'\n');
    for mut item in items {
        item.header["length"] = json!(item.payload.len());
        out.extend(item.header.to_string().into_bytes());
        out.push(b'\n');
        out.extend(item.payload);
        out.push(b'\n');
    }
    out
}

/// Ok once Sentry accepted the envelope (any 2xx).
pub fn send(dsn: &Dsn, envelope: &[u8]) -> Result<(), String> {
    debug!("POST {} ({} bytes)", dsn.envelope_url(), envelope.len());
    let agent: ureq::Agent = ureq::Agent::config_builder()
        .timeout_global(Some(Duration::from_secs(10)))
        .http_status_as_error(false)
        .build()
        .into();
    let response = agent
        .post(&dsn.envelope_url())
        .header("Content-Type", "application/x-sentry-envelope")
        .header("X-Sentry-Auth", &dsn.auth_header())
        .send(envelope)
        .map_err(|e| e.to_string())?;
    let status = response.status();
    debug!("Sentry answered {status}");
    if status.is_success() {
        Ok(())
    } else {
        Err(format!("the server answered {status}"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_a_dsn() {
        let dsn = Dsn::parse("https://abc123@o1.ingest.sentry.io/456\n").unwrap();
        assert_eq!(dsn.key, "abc123");
        assert_eq!(dsn.host, "o1.ingest.sentry.io");
        assert_eq!(dsn.project, "456");
        assert_eq!(dsn.envelope_url(), "https://o1.ingest.sentry.io/api/456/envelope/");
    }

    #[test]
    fn parses_a_dsn_with_secret_port_and_prefix() {
        let dsn = Dsn::parse("http://k:s@127.0.0.1:9000/sub/7").unwrap();
        assert_eq!(dsn.key, "k");
        assert_eq!(dsn.envelope_url(), "http://127.0.0.1:9000/sub/api/7/envelope/");
    }

    #[test]
    fn rejects_a_broken_dsn() {
        for bad in ["", "garbage", "ftp://k@h/1", "https://h/1", "https://k@h/", "https://@h/1"] {
            assert_eq!(Dsn::parse(bad), None, "{bad}");
        }
    }

    #[test]
    fn envelope_item_lengths_match_payloads() {
        let envelope = build_envelope(
            "e",
            "https://k@h/1",
            vec![Item::event(&json!({"message": "x"})), Item::attachment("a.txt", b"line\nsecond\n".to_vec())],
        );
        let text = String::from_utf8(envelope).unwrap();
        let mut lines = text.split_inclusive('\n');
        let header: Value = serde_json::from_str(lines.next().unwrap()).unwrap();
        assert_eq!(header["event_id"], "e");
        let event_header: Value = serde_json::from_str(lines.next().unwrap()).unwrap();
        let event = lines.next().unwrap();
        assert_eq!(event_header["length"], event.trim_end().len());
        let attachment_header: Value = serde_json::from_str(lines.next().unwrap()).unwrap();
        assert_eq!(attachment_header["length"], 12);
        assert_eq!(lines.next().unwrap(), "line\n");
        assert_eq!(lines.next().unwrap(), "second\n");
        assert_eq!(lines.next().unwrap(), "\n");
    }

    #[test]
    fn event_ids_are_32_hex_digits_and_differ() {
        let (a, b) = (new_event_id(), new_event_id());
        assert_eq!(a.len(), 32);
        assert!(a.bytes().all(|c| c.is_ascii_hexdigit()));
        assert_ne!(a, b);
    }
}
