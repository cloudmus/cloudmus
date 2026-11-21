//! A proxy on 127.0.0.1 that hands every connection on to the source's own
//! proxy, signed in to it.
//!
//! librespot reaches Spotify through an HTTP proxy in two ways — its access
//! point over a CONNECT tunnel it writes itself (proxytunnel.rs), everything
//! else through hyper-proxy2 — and neither sends the credentials of a proxy
//! URL like `http://user:pass@host:port`: the proxy answers 407 and nothing
//! connects. So such a proxy is given to librespot as this relay instead,
//! which adds `Proxy-Authorization` to each request it passes on. Only the
//! request head is touched; after it, bytes go both ways as they are (a
//! CONNECT tunnel's TLS, mostly).

use std::net::SocketAddr;

use base64::engine::general_purpose::STANDARD as BASE64;
use base64::Engine as _;
use percent_encoding::percent_decode_str;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use url::Url;

/// A request head bigger than this isn't a proxy request.
const MAX_HEAD: usize = 16 * 1024;

/// `head` (a request line and headers, ending in a blank line) with the
/// proxy's credentials in place of any it had.
pub fn signed_head(head: &str, authorization: &str) -> String {
    let mut lines = head.split("\r\n");
    let request_line = lines.next().unwrap_or_default();
    let mut out = format!("{request_line}\r\nProxy-Authorization: {authorization}\r\n");
    for line in lines {
        if line.is_empty() {
            continue;
        }
        if line.split(':').next().is_some_and(|name| name.trim().eq_ignore_ascii_case("proxy-authorization")) {
            continue;
        }
        out.push_str(line);
        out.push_str("\r\n");
    }
    out.push_str("\r\n");
    out
}

/// `Basic …` for the credentials in `proxy` (percent-decoded, as curl and
/// requests take them), or None when it carries none.
pub fn basic_authorization(proxy: &Url) -> Option<String> {
    if proxy.username().is_empty() {
        return None;
    }
    let decode = |s: &str| percent_decode_str(s).decode_utf8_lossy().into_owned();
    let credentials = format!("{}:{}", decode(proxy.username()), decode(proxy.password().unwrap_or("")));
    Some(format!("Basic {}", BASE64.encode(credentials)))
}

/// Starts the relay for `upstream` (an http:// proxy with credentials) and
/// returns the proxy URL to give librespot in its place.
pub async fn start(upstream: &Url, authorization: String) -> std::io::Result<Url> {
    let host = upstream.host_str().unwrap_or_default().to_owned();
    let port = upstream.port_or_known_default().unwrap_or(80);
    let listener = TcpListener::bind(("127.0.0.1", 0)).await?;
    let local: SocketAddr = listener.local_addr()?;
    log::info!("relaying to the proxy {host}:{port} with its credentials, from {local}");
    tokio::spawn(async move {
        loop {
            let Ok((client, _)) = listener.accept().await else { continue };
            let (host, authorization) = (host.clone(), authorization.clone());
            tokio::spawn(async move {
                if let Err(e) = relay(client, &host, port, &authorization).await {
                    log::debug!("proxy relay connection: {e}");
                }
            });
        }
    });
    Ok(Url::parse(&format!("http://{local}")).expect("valid"))
}

async fn relay(mut client: TcpStream, host: &str, port: u16, authorization: &str) -> std::io::Result<()> {
    let mut buf = Vec::with_capacity(1024);
    let head_end = loop {
        let mut chunk = [0u8; 1024];
        let n = client.read(&mut chunk).await?;
        if n == 0 {
            return Ok(());
        }
        buf.extend_from_slice(&chunk[..n]);
        if let Some(end) = buf.windows(4).position(|w| w == b"\r\n\r\n") {
            break end + 4;
        }
        if buf.len() > MAX_HEAD {
            return Err(std::io::Error::other("request head too long"));
        }
    };
    let head = String::from_utf8_lossy(&buf[..head_end]);
    let mut upstream = TcpStream::connect((host, port)).await?;
    upstream.write_all(signed_head(&head, authorization).as_bytes()).await?;
    // Whatever came after the head (a client that didn't wait for the
    // tunnel's 200) goes on unchanged.
    upstream.write_all(&buf[head_end..]).await?;
    tokio::io::copy_bidirectional(&mut client, &mut upstream).await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn credentials_replace_any_the_request_had() {
        let head =
            "CONNECT ap.spotify.com:443 HTTP/1.1\r\nHost: ap.spotify.com:443\r\nproxy-authorization: Basic old\r\n\r\n";
        assert_eq!(
            signed_head(head, "Basic new"),
            "CONNECT ap.spotify.com:443 HTTP/1.1\r\nProxy-Authorization: Basic new\r\nHost: ap.spotify.com:443\r\n\r\n"
        );
        // librespot's own tunnel sends no headers at all
        assert_eq!(
            signed_head("CONNECT a:1 HTTP/1.1\r\n\r\n", "Basic x"),
            "CONNECT a:1 HTTP/1.1\r\nProxy-Authorization: Basic x\r\n\r\n"
        );
    }

    #[test]
    fn credentials_come_from_the_url_decoded() {
        let url = Url::parse("http://us%40er:p%3Ass@proxy.example:3128").unwrap();
        assert_eq!(basic_authorization(&url).unwrap(), format!("Basic {}", BASE64.encode("us@er:p:ss")));
        assert_eq!(basic_authorization(&Url::parse("http://proxy.example:3128").unwrap()), None);
    }

    /// A tunnel through the relay reaches the upstream proxy signed in, and
    /// carries bytes both ways after the proxy's 200.
    #[tokio::test]
    async fn tunnels_through_the_upstream_proxy() {
        let upstream = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let upstream_url = Url::parse(&format!("http://u:p@{}", upstream.local_addr().unwrap())).unwrap();
        let fake_proxy = tokio::spawn(async move {
            let (mut socket, _) = upstream.accept().await.unwrap();
            let mut buf = vec![0u8; 4096];
            let n = socket.read(&mut buf).await.unwrap();
            let head = String::from_utf8_lossy(&buf[..n]).into_owned();
            socket.write_all(b"HTTP/1.1 200 Connection established\r\n\r\n").await.unwrap();
            let n = socket.read(&mut buf).await.unwrap();
            socket.write_all(&buf[..n]).await.unwrap(); // echo
            head
        });

        let relay_url = start(&upstream_url, basic_authorization(&upstream_url).unwrap()).await.unwrap();
        let mut client = TcpStream::connect((relay_url.host_str().unwrap(), relay_url.port().unwrap())).await.unwrap();
        client.write_all(b"CONNECT ap.spotify.com:4070 HTTP/1.1\r\n\r\n").await.unwrap();
        let mut buf = vec![0u8; 256];
        let n = client.read(&mut buf).await.unwrap();
        assert!(String::from_utf8_lossy(&buf[..n]).starts_with("HTTP/1.1 200"));
        client.write_all(b"ping").await.unwrap();
        let n = client.read(&mut buf).await.unwrap();
        assert_eq!(&buf[..n], b"ping");

        let head = fake_proxy.await.unwrap();
        assert!(head.starts_with("CONNECT ap.spotify.com:4070 HTTP/1.1\r\n"));
        assert!(head.contains(&format!("Proxy-Authorization: Basic {}\r\n", BASE64.encode("u:p"))));
    }
}
