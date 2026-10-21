//! Signing in (docs/protocol.md §10.3, `oauthRedirect`) and the librespot
//! session that everything else goes through.
//!
//! The OAuth flow is the one librespot's own binary runs — authorization
//! code with PKCE against Spotify's desktop client id, redirected to a
//! listener on 127.0.0.1 — written out here rather than taken from
//! librespot-oauth: that crate prints the URL to stdout (our JSON-RPC
//! channel) and can't be cancelled. The token connects a session; the
//! session's reusable credentials are what's kept, so later starts sign in
//! without a browser.
//!
//! A saved sign-in counts as "authenticated" at once and the session is
//! connected on first use, like the Python backends' saved tokens: the
//! front would otherwise start a new sign-in while the old one restores.

use std::sync::Arc;
use std::time::Duration;

use base64::engine::general_purpose::URL_SAFE_NO_PAD;
use base64::Engine as _;
use bytes::Bytes;
use http::{Method, Request};
use librespot_core::authentication::Credentials;
use librespot_core::http_client::HttpClient;
use librespot_core::{Session, SessionConfig};
use librespot_protocol::authentication::AuthenticationType;
use rand::RngCore;
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpListener;
use tokio::sync::{watch, Mutex};
use url::Url;

use crate::config;
use crate::generated::methods::{AUTH_PROMPT, AUTH_STATUS_CHANGED};
use crate::generated::models::{GetStatusResult, StatusChangedParams};
use crate::i18n::{tr, trf};
use crate::rpc::errors::{app_error_data, AUTH_EXPIRED, AUTH_NOT_AUTHENTICATED, UPSTREAM_UNREACHABLE};
use crate::rpc::{Notifier, RpcError};

/// Spotify's desktop client — the one librespot signs in as. Its redirect
/// accepts any port on 127.0.0.1 (librespot's `--oauth-port`).
const CLIENT_ID: &str = "65b708073fc0480ea92a077233ca87bd";
const AUTHORIZE_URL: &str = "https://accounts.spotify.com/authorize";
const TOKEN_URL: &str = "https://accounts.spotify.com/api/token";
const ME_URL: &str = "https://api.spotify.com/v1/me";
/// librespot's own list (its main.rs `OAUTH_SCOPES`).
const SCOPES: &[&str] = &[
    "app-remote-control",
    "playlist-modify",
    "playlist-modify-private",
    "playlist-modify-public",
    "playlist-read",
    "playlist-read-collaborative",
    "playlist-read-private",
    "streaming",
    "ugc-image-upload",
    "user-follow-modify",
    "user-follow-read",
    "user-library-modify",
    "user-library-read",
    "user-modify",
    "user-modify-playback-state",
    "user-modify-private",
    "user-personalized",
    "user-read-birthdate",
    "user-read-currently-playing",
    "user-read-email",
    "user-read-play-history",
    "user-read-playback-position",
    "user-read-playback-state",
    "user-read-private",
    "user-read-recently-played",
    "user-top-read",
];

#[derive(Clone, PartialEq, Debug)]
enum Status {
    Unauthenticated,
    Pending,
    Authenticated,
    Error(String),
}

struct State {
    status: Status,
    session: Option<Session>,
    /// Set to true to cancel the running sign-in. Each run has a channel of
    /// its own, so a cancelled run that hasn't noticed yet can't disturb
    /// the next one.
    flow: Option<watch::Sender<bool>>,
}

pub struct Account {
    state: std::sync::Mutex<State>,
    /// Serializes connecting, so two requests on a cold start don't open
    /// two sessions.
    connecting: Mutex<()>,
    notifier: Notifier,
    proxy: Option<Url>,
}

/// The proxy the front set for this source (§4.3), as librespot is to use
/// it. librespot speaks HTTP proxies only, and without their credentials: a
/// proxy that has some is reached through the relay (proxy_relay.rs).
pub async fn proxy_from_env() -> Option<Url> {
    let proxy = env_proxy()?;
    let Some(authorization) = crate::proxy_relay::basic_authorization(&proxy) else { return Some(proxy) };
    if proxy.scheme() != "http" {
        log::warn!("an {} proxy with credentials isn't supported; connecting directly", proxy.scheme());
        return None;
    }
    match crate::proxy_relay::start(&proxy, authorization).await {
        Ok(relay) => Some(relay),
        Err(e) => {
            log::warn!("can't start the proxy relay: {e}; connecting directly");
            None
        }
    }
}

fn env_proxy() -> Option<Url> {
    let value = ["HTTPS_PROXY", "https_proxy", "ALL_PROXY", "all_proxy", "HTTP_PROXY", "http_proxy"]
        .iter()
        .find_map(|name| std::env::var(name).ok().filter(|v| !v.is_empty()))?;
    match Url::parse(&value) {
        Ok(url) if matches!(url.scheme(), "http" | "https") => Some(url),
        Ok(url) => {
            log::warn!("proxy scheme {} isn't supported by librespot; connecting directly", url.scheme());
            None
        }
        Err(e) => {
            log::warn!("unusable proxy {value:?}: {e}; connecting directly");
            None
        }
    }
}

fn random_token(bytes: usize) -> String {
    let mut buf = vec![0u8; bytes];
    rand::rng().fill_bytes(&mut buf);
    URL_SAFE_NO_PAD.encode(buf)
}

/// PKCE (RFC 7636): the verifier stays here, its S256 challenge goes in the URL.
pub fn pkce_challenge(verifier: &str) -> String {
    URL_SAFE_NO_PAD.encode(Sha256::digest(verifier.as_bytes()))
}

pub fn authorize_url(redirect_uri: &str, challenge: &str, state: &str) -> String {
    let mut url = Url::parse(AUTHORIZE_URL).expect("valid");
    url.query_pairs_mut()
        .append_pair("response_type", "code")
        .append_pair("client_id", CLIENT_ID)
        .append_pair("redirect_uri", redirect_uri)
        .append_pair("code_challenge_method", "S256")
        .append_pair("code_challenge", challenge)
        .append_pair("state", state)
        .append_pair("scope", &SCOPES.join(" "))
        // Spotify otherwise hands back whoever is signed in to it in the
        // browser, at once — the Free account again, with no way to pick
        // another. The dialog names the account and offers "Not you?".
        .append_pair("show_dialog", "true");
    url.into()
}

/// What the browser brought back to the listener.
#[derive(Debug, PartialEq)]
pub enum Callback {
    Code(String),
    /// Spotify's `error` (the user pressed Cancel: "access_denied").
    Denied(String),
    /// Not the callback: a favicon request, a stale tab of an older sign-in.
    Other,
}

/// Reads the request line of an HTTP request to the redirect listener.
pub fn parse_callback(request_line: &str, expected_state: &str) -> Callback {
    let Some(target) = request_line.split_whitespace().nth(1) else { return Callback::Other };
    let Ok(url) = Url::parse(&format!("http://127.0.0.1{target}")) else { return Callback::Other };
    if url.path() != "/login" {
        return Callback::Other;
    }
    let query = |name: &str| url.query_pairs().find(|(k, _)| k == name).map(|(_, v)| v.into_owned());
    if query("state").as_deref() != Some(expected_state) {
        return Callback::Other;
    }
    if let Some(error) = query("error") {
        return Callback::Denied(error);
    }
    query("code").map(Callback::Code).unwrap_or(Callback::Other)
}

fn page(title: &str, text: &str) -> String {
    let body = format!(
        "<!doctype html><meta charset=utf-8><title>{title}</title>\
         <body style=\"font-family:sans-serif;text-align:center;padding-top:20vh\">\
         <h2>{title}</h2><p>{text}</p></body>"
    );
    format!(
        "HTTP/1.1 200 OK\r\ncontent-type: text/html; charset=utf-8\r\ncontent-length: {}\r\nconnection: close\r\n\r\n{body}",
        body.len()
    )
}

enum FlowError {
    Cancelled,
    /// Shown to the user as is.
    Failed(String),
}

impl Account {
    pub fn new(notifier: Notifier, proxy: Option<Url>) -> Arc<Self> {
        let status = if config::load_credentials().is_some() { Status::Authenticated } else { Status::Unauthenticated };
        Arc::new(Account {
            state: std::sync::Mutex::new(State { status, session: None, flow: None }),
            connecting: Mutex::new(()),
            notifier,
            proxy,
        })
    }

    fn session_config(&self) -> SessionConfig {
        SessionConfig { proxy: self.proxy.clone(), ..SessionConfig::default() }
    }

    pub fn get_status(&self) -> GetStatusResult {
        let state = self.state.lock().unwrap();
        let (status, detail) = match &state.status {
            Status::Unauthenticated => ("unauthenticated", None),
            Status::Pending => ("pending", None),
            Status::Authenticated => ("authenticated", None),
            Status::Error(message) => ("error", Some(message.clone())),
        };
        GetStatusResult { status: status.into(), detail }
    }

    /// The connected session, connecting (or reconnecting a dropped one)
    /// from the saved credentials when needed.
    pub async fn session(&self) -> Result<Session, RpcError> {
        if let Some(session) = self.live_session() {
            return Ok(session);
        }
        let _guard = self.connecting.lock().await;
        if let Some(session) = self.live_session() {
            return Ok(session);
        }
        let Some(credentials) = config::load_credentials() else {
            return Err(RpcError::new(AUTH_NOT_AUTHENTICATED, tr("Not signed in to Spotify"))
                .with_data(app_error_data(false, None, None, None)));
        };
        let session = Session::new(self.session_config(), None);
        match session.connect(credentials, false).await {
            Ok(()) => {
                self.state.lock().unwrap().session = Some(session.clone());
                Ok(session)
            }
            Err(e) if is_login_failure(&e) => {
                log::warn!("saved credentials rejected: {e}");
                config::clear_credentials();
                let message = tr("Spotify has signed this session out. Sign in again.");
                self.set_status(Status::Error(message.clone()));
                self.notifier.notify(
                    AUTH_STATUS_CHANGED,
                    StatusChangedParams { status: "error".into(), message: Some(message.clone()) },
                );
                Err(RpcError::new(AUTH_EXPIRED, message).with_data(app_error_data(false, None, None, None)))
            }
            Err(e) => Err(upstream_error(&e)),
        }
    }

    fn live_session(&self) -> Option<Session> {
        let state = self.state.lock().unwrap();
        state.session.as_ref().filter(|s| !s.is_invalid()).cloned()
    }

    fn set_status(&self, status: Status) {
        self.state.lock().unwrap().status = status;
    }

    /// Starts a sign-in, unless one is already running (auth.start is
    /// idempotent). One cancelled a moment ago doesn't count as running.
    pub fn start(self: &Arc<Self>) {
        let mut state = self.state.lock().unwrap();
        if state.flow.as_ref().is_some_and(|f| !*f.borrow()) {
            return;
        }
        let (cancel_tx, cancel_rx) = watch::channel(false);
        state.flow = Some(cancel_tx);
        state.status = Status::Pending;
        drop(state);

        let account = self.clone();
        tokio::spawn(async move {
            let result = account.run_flow(cancel_rx.clone()).await;
            if *cancel_rx.borrow() {
                return; // cancelled: a newer run (or none) owns the state now
            }
            let mut state = account.state.lock().unwrap();
            state.flow = None;
            match result {
                Ok(session) => {
                    state.session = Some(session);
                    state.status = Status::Authenticated;
                    drop(state);
                    account.notifier.notify(
                        AUTH_STATUS_CHANGED,
                        StatusChangedParams { status: "authenticated".into(), message: None },
                    );
                }
                Err(FlowError::Cancelled) => {}
                Err(FlowError::Failed(message)) => {
                    state.status = Status::Error(message.clone());
                    drop(state);
                    account.notifier.notify(
                        AUTH_STATUS_CHANGED,
                        StatusChangedParams { status: "error".into(), message: Some(message) },
                    );
                }
            }
        });
    }

    pub fn cancel(&self) {
        let mut state = self.state.lock().unwrap();
        if let Some(flow) = state.flow.take() {
            let _ = flow.send(true);
        }
        if state.status == Status::Pending {
            state.status = Status::Unauthenticated;
        }
    }

    pub fn logout(&self) {
        self.cancel();
        let mut state = self.state.lock().unwrap();
        if let Some(session) = state.session.take() {
            session.shutdown();
        }
        state.status = Status::Unauthenticated;
        config::clear_credentials();
        config::clear_not_premium();
    }

    async fn run_flow(&self, mut cancel: watch::Receiver<bool>) -> Result<Session, FlowError> {
        let listener = TcpListener::bind(("127.0.0.1", 0))
            .await
            .map_err(|e| FlowError::Failed(trf("Can't start the sign-in: {error}", &[("error", &e)])))?;
        let port = listener.local_addr().map_err(|e| FlowError::Failed(e.to_string()))?.port();
        let redirect_uri = format!("http://127.0.0.1:{port}/login");
        let verifier = random_token(48);
        let state = random_token(16);
        let url = authorize_url(&redirect_uri, &pkce_challenge(&verifier), &state);
        log::info!("sign-in listening on 127.0.0.1:{port}");

        // Why the last sign-in ended is told once, in this prompt: the front
        // keeps showing it, and a later one starts afresh.
        let was_not_premium = config::was_not_premium();
        config::clear_not_premium();
        self.notifier.notify(
            AUTH_PROMPT,
            json!({
                "flow": "oauthRedirect",
                "url": url,
                "message": if was_not_premium {
                    tr("Spotify says the account you signed in with is on the Free plan, and CloudMus plays \
                        Spotify only with <b>Premium</b>. Sign in with a Premium account: Spotify's page \
                        offers <b>Not you?</b> to switch.")
                } else {
                    tr("Sign in with a Spotify <b>Premium</b> account.")
                },
            }),
        );

        let wait = async {
            loop {
                let (mut stream, _) = listener.accept().await.map_err(|e| FlowError::Failed(e.to_string()))?;
                let mut buf = vec![0u8; 8192];
                let n = match tokio::time::timeout(Duration::from_secs(10), stream.read(&mut buf)).await {
                    Ok(Ok(n)) => n,
                    _ => continue,
                };
                let request = String::from_utf8_lossy(&buf[..n]);
                let request_line = request.lines().next().unwrap_or("");
                match parse_callback(request_line, &state) {
                    Callback::Code(code) => {
                        let reply = page(&tr("Signed in"), &tr("You can close this tab and return to CloudMus."));
                        let _ = stream.write_all(reply.as_bytes()).await;
                        break Ok(code);
                    }
                    Callback::Denied(error) => {
                        let reply = page(&tr("Sign-in cancelled"), &tr("You can close this tab."));
                        let _ = stream.write_all(reply.as_bytes()).await;
                        break Err(FlowError::Failed(trf(
                            "Spotify refused the sign-in: {error}",
                            &[("error", &error)],
                        )));
                    }
                    Callback::Other => {
                        let _ = stream.write_all(b"HTTP/1.1 404 Not Found\r\ncontent-length: 0\r\n\r\n").await;
                    }
                }
            }
        };

        // No time limit: the link works for as long as this waits, and the
        // user signs in whenever they get to it (docs/protocol.md §10.3).
        let code = tokio::select! {
            code = wait => code?,
            _ = cancel.wait_for(|cancelled| *cancelled) => return Err(FlowError::Cancelled),
        };
        drop(listener);

        let http = HttpClient::new(self.proxy.as_ref());
        let token = exchange_code(&http, &code, &redirect_uri, &verifier).await?;
        // librespot ends the whole process when a session turns out not to be
        // Premium (Session::check_catalogue), so that's asked first, with the
        // token alone.
        match account_product(&http, &token).await? {
            Some(product) if product != "premium" => {
                return Err(FlowError::Failed(tr(
                    "Spotify Premium is required: librespot can't play on a free account.",
                )));
            }
            _ => {}
        }

        let session = Session::new(self.session_config(), None);
        session
            .connect(Credentials::with_access_token(token), false)
            .await
            .map_err(|e| FlowError::Failed(trf("Couldn't connect to Spotify: {error}", &[("error", &e)])))?;
        let reusable = Credentials {
            username: Some(session.username()),
            auth_type: AuthenticationType::AUTHENTICATION_STORED_SPOTIFY_CREDENTIALS,
            auth_data: session.auth_data(),
        };
        if *cancel.borrow() {
            session.shutdown();
            return Err(FlowError::Cancelled);
        }
        config::save_credentials(&reusable)
            .map_err(|e| FlowError::Failed(trf("Couldn't save the sign-in: {error}", &[("error", &e)])))?;
        config::clear_not_premium();
        log::info!("signed in as {}", session.username());
        Ok(session)
    }
}

async fn exchange_code(http: &HttpClient, code: &str, redirect_uri: &str, verifier: &str) -> Result<String, FlowError> {
    let body = url::form_urlencoded::Serializer::new(String::new())
        .append_pair("grant_type", "authorization_code")
        .append_pair("code", code)
        .append_pair("redirect_uri", redirect_uri)
        .append_pair("client_id", CLIENT_ID)
        .append_pair("code_verifier", verifier)
        .finish();
    let request = Request::builder()
        .method(Method::POST)
        .uri(TOKEN_URL)
        .header(http::header::CONTENT_TYPE, "application/x-www-form-urlencoded")
        .body(Bytes::from(body))
        .map_err(|e| FlowError::Failed(e.to_string()))?;
    let response = http
        .request_body(request)
        .await
        .map_err(|e| FlowError::Failed(trf("Couldn't finish the sign-in: {error}", &[("error", &e)])))?;
    let value: Value = serde_json::from_slice(&response).map_err(|e| FlowError::Failed(e.to_string()))?;
    value
        .get("access_token")
        .and_then(Value::as_str)
        .map(str::to_owned)
        .ok_or_else(|| FlowError::Failed(tr("Spotify sent no access token")))
}

/// "premium", "free", ...; None when Spotify didn't say (then the session's
/// own check is all there is).
async fn account_product(http: &HttpClient, token: &str) -> Result<Option<String>, FlowError> {
    let request = Request::builder()
        .uri(ME_URL)
        .header(http::header::AUTHORIZATION, format!("Bearer {token}"))
        .body(Bytes::new())
        .map_err(|e| FlowError::Failed(e.to_string()))?;
    match http.request_body(request).await {
        Ok(body) => Ok(serde_json::from_slice::<Value>(&body)
            .ok()
            .and_then(|v| v.get("product").and_then(Value::as_str).map(str::to_owned))),
        Err(e) => {
            // The Web API rate-limits this client id hard at times; not a
            // reason to fail a sign-in that can still work.
            log::warn!("couldn't ask for the account type: {e}");
            Ok(None)
        }
    }
}

/// Spotify refusing the credentials, as opposed to not being reachable:
/// librespot's AuthenticationError::LoginFailed (private) maps to this.
/// Set right before this backend ends the process itself (main.rs).
static OWN_EXIT: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

pub fn exiting_on_purpose() {
    OWN_EXIT.store(true, std::sync::atomic::Ordering::SeqCst);
}

/// librespot ends the process (exit(1), Session::check_catalogue) as soon
/// as Spotify says a session's account isn't Premium — the one exit in it,
/// and one that can't be caught. Its atexit handlers still run: one that
/// finds the exit isn't ours forgets the saved sign-in and leaves a mark,
/// so the restarted backend asks to sign in again and says why, instead of
/// signing in to the same Free account and ending again, over and over.
pub fn install_exit_guard() {
    extern "C" fn on_exit() {
        if OWN_EXIT.load(std::sync::atomic::Ordering::SeqCst) {
            return;
        }
        config::clear_credentials();
        config::mark_not_premium();
        eprintln!("the account isn't Premium (librespot ended the process): signed out");
    }
    // SAFETY: registers a plain extern "C" function with no arguments.
    unsafe {
        libc::atexit(on_exit);
    }
}

fn is_login_failure(e: &librespot_core::Error) -> bool {
    e.kind == librespot_core::error::ErrorKind::PermissionDenied && e.to_string().contains("Login failed")
}

/// A failure to reach Spotify, as an RPC error.
pub fn upstream_error(e: &librespot_core::Error) -> RpcError {
    let offline = crate::errors::is_offline(e);
    RpcError::new(UPSTREAM_UNREACHABLE, e.to_string()).with_data(app_error_data(
        true,
        None,
        None,
        offline.then_some(crate::rpc::errors::REASON_OFFLINE),
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pkce_matches_rfc_7636_example() {
        // RFC 7636, appendix B
        assert_eq!(
            pkce_challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
            "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"
        );
    }

    #[test]
    fn authorize_url_carries_the_flow() {
        let url = Url::parse(&authorize_url("http://127.0.0.1:5000/login", "ch", "st")).unwrap();
        let pairs: std::collections::HashMap<_, _> = url.query_pairs().into_owned().collect();
        assert_eq!(pairs["client_id"], CLIENT_ID);
        assert_eq!(pairs["redirect_uri"], "http://127.0.0.1:5000/login");
        assert_eq!(pairs["code_challenge"], "ch");
        assert_eq!(pairs["state"], "st");
        assert!(pairs["scope"].contains("streaming"));
        assert_eq!(pairs["show_dialog"], "true");
    }

    #[test]
    fn callback_parsing() {
        assert_eq!(parse_callback("GET /login?code=abc&state=st HTTP/1.1", "st"), Callback::Code("abc".into()));
        assert_eq!(
            parse_callback("GET /login?error=access_denied&state=st HTTP/1.1", "st"),
            Callback::Denied("access_denied".into())
        );
        // an older sign-in's tab, or anything that isn't the callback
        assert_eq!(parse_callback("GET /login?code=abc&state=old HTTP/1.1", "st"), Callback::Other);
        assert_eq!(parse_callback("GET /favicon.ico HTTP/1.1", "st"), Callback::Other);
        assert_eq!(parse_callback("", "st"), Callback::Other);
    }
}
