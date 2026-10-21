//! The stdio JSON-RPC server: the Rust counterpart of
//! `backends/py-rpc-common/rpc_common/server.py`, down to its log lines.
//!
//! Requests are handled concurrently, each answered when it is done
//! (docs/protocol.md §3); `initialize` and `shutdown` stay in line. One
//! writer task owns stdout, so responses and notifications never interleave
//! partial lines.

use std::collections::HashMap;
use std::future::Future;
use std::pin::Pin;
use std::sync::Arc;
use std::time::Instant;

use serde::Serialize;
use serde_json::{json, Value};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::sync::mpsc;

use super::errors::{RpcError, INTERNAL_ERROR, INVALID_PARAMS, METHOD_NOT_FOUND};
use super::i18n::Translator;
use crate::generated::methods::PROTOCOL_VERSION;

/// Full payloads are logged, but past this many characters they're cut:
/// a page of tracks would otherwise make one line megabytes long.
const MAX_LOGGED_VALUE_CHARS: usize = 2000;

/// Methods whose params may carry a secret: logged as "<redacted>".
const REDACTED_PARAMS_METHODS: &[&str] = &["auth.submit", "settings.update"];

pub type HandlerResult = Result<Value, RpcError>;
pub type HandlerFuture = Pin<Box<dyn Future<Output = HandlerResult> + Send>>;
type Handler = Arc<dyn Fn(Value, i64) -> HandlerFuture + Send + Sync>;

fn summarize(value: &Value) -> String {
    let text = value.to_string();
    match text.char_indices().nth(MAX_LOGGED_VALUE_CHARS) {
        Some((cut, _)) => {
            let omitted = text[cut..].chars().count();
            format!("{}... ({omitted} more chars)", &text[..cut])
        }
        None => text,
    }
}

/// Sends notifications (backend -> front). Cheap to clone; usable from any task.
#[derive(Clone)]
pub struct Notifier {
    // None marks the end: the writer stops there, see Server::run().
    tx: mpsc::UnboundedSender<Option<String>>,
}

impl Notifier {
    pub fn notify(&self, method: &str, params: impl Serialize) {
        let params = serde_json::to_value(params).unwrap_or_else(|_| json!({}));
        // <~, not <-: pushed unsolicited, not an answer to a request.
        log::info!("<~ {method} params={}", summarize(&params));
        self.send(json!({"jsonrpc": "2.0", "method": method, "params": params}));
    }

    fn send(&self, message: Value) {
        // Fails only once the writer is gone, at exit: nobody to tell.
        let _ = self.tx.send(Some(message.to_string()));
    }
}

pub struct SourceInfo {
    pub id: &'static str,
    pub name: &'static str,
    pub version: &'static str,
    /// English; translated through the server's translator.
    pub description: &'static str,
}

pub struct Server {
    info: SourceInfo,
    capabilities: Value,
    handlers: HashMap<String, Handler>,
    notifier: Notifier,
    rx: mpsc::UnboundedReceiver<Option<String>>,
    translator: Arc<Translator>,
}

impl Server {
    pub fn new(info: SourceInfo, mut capabilities: Value, translator: Arc<Translator>) -> Self {
        capabilities["localization"] = json!({"locales": translator.locales()});
        let (tx, rx) = mpsc::unbounded_channel();
        let mut server =
            Server { info, capabilities, handlers: HashMap::new(), notifier: Notifier { tx }, rx, translator };
        let translator = server.translator.clone();
        let description = server.info.description;
        server.method("localization.setLanguage", move |params, _| {
            let translator = translator.clone();
            async move {
                let Some(locale) = params.get("locale").and_then(Value::as_str) else {
                    return Err(RpcError::new(INVALID_PARAMS, "locale: expected text"));
                };
                let applied = translator.set_locale(Some(locale));
                Ok(json!({"locale": applied, "description": translator.tr(description)}))
            }
        });
        server
    }

    pub fn notifier(&self) -> Notifier {
        self.notifier.clone()
    }

    pub fn capabilities_mut(&mut self) -> &mut Value {
        &mut self.capabilities
    }

    /// Registers the handler for one `namespace.methodName` request. It gets
    /// the params and the request id, and answers with a result or an error.
    pub fn method<F, Fut>(&mut self, name: &str, handler: F)
    where
        F: Fn(Value, i64) -> Fut + Send + Sync + 'static,
        Fut: Future<Output = HandlerResult> + Send + 'static,
    {
        self.handlers.insert(name.to_owned(), Arc::new(move |params, id| Box::pin(handler(params, id))));
    }

    fn initialize(&self, params: &Value) -> Value {
        self.translator.set_locale(params.get("locale").and_then(Value::as_str));
        json!({
            "protocolVersion": PROTOCOL_VERSION,
            "source": {
                "id": self.info.id,
                "name": self.info.name,
                "version": self.info.version,
                "description": self.translator.tr(self.info.description),
            },
            "capabilities": self.capabilities,
        })
    }

    /// Serves stdin/stdout until shutdown or EOF; returns once every reply is written.
    pub async fn run(mut self) {
        let mut rx = std::mem::replace(&mut self.rx, mpsc::unbounded_channel().1);
        let writer = tokio::spawn(async move {
            let mut stdout = tokio::io::stdout();
            while let Some(Some(mut line)) = rx.recv().await {
                line.push('\n');
                if stdout.write_all(line.as_bytes()).await.is_err() || stdout.flush().await.is_err() {
                    break;
                }
            }
        });

        let mut lines = BufReader::new(tokio::io::stdin()).lines();
        loop {
            let line = match lines.next_line().await {
                Ok(Some(line)) => line,
                Ok(None) => break,
                Err(e) => {
                    log::error!("reading stdin: {e}");
                    break;
                }
            };
            let line = line.trim();
            if line.is_empty() {
                continue;
            }
            let message: Value = match serde_json::from_str(line) {
                Ok(v) => v,
                Err(e) => {
                    log::warn!("dropping a line that isn't JSON: {e}");
                    self.notifier.send(json!({
                        "jsonrpc": "2.0", "id": null,
                        "error": {"code": -32700, "message": format!("parse error: {e}")},
                    }));
                    continue;
                }
            };
            let Some(method) = message.get("method").and_then(Value::as_str).map(str::to_owned) else {
                continue; // a response to nothing we asked: ignored
            };
            let id = message.get("id").cloned();
            let params = message.get("params").cloned().unwrap_or_else(|| json!({}));

            match method.as_str() {
                "initialize" => {
                    let result = self.initialize(&params);
                    self.reply(id, Ok(result));
                }
                "shutdown" => {
                    self.reply(id, Ok(json!({})));
                    break;
                }
                _ => {
                    let handler = self.handlers.get(&method).cloned();
                    let notifier = self.notifier.clone();
                    tokio::spawn(dispatch(notifier, handler, method, id, params));
                }
            }
        }

        // Requests still running hold senders of their own, so the channel
        // never closes by itself: the writer stops at this marker, after
        // everything queued before it (the shutdown reply included).
        let _ = self.notifier.tx.send(None);
        let _ = tokio::time::timeout(std::time::Duration::from_secs(2), writer).await;
    }

    fn reply(&self, id: Option<Value>, result: HandlerResult) {
        if let Some(id) = id {
            self.notifier.send(response(id, result));
        }
    }
}

fn response(id: Value, result: HandlerResult) -> Value {
    match result {
        Ok(result) => json!({"jsonrpc": "2.0", "id": id, "result": result}),
        Err(e) => json!({"jsonrpc": "2.0", "id": id, "error": e.to_json()}),
    }
}

async fn dispatch(notifier: Notifier, handler: Option<Handler>, method: String, id: Option<Value>, params: Value) {
    let Some(handler) = handler else {
        if let Some(id) = id {
            notifier.send(response(id, Err(RpcError::new(METHOD_NOT_FOUND, format!("method not found: {method}")))));
        }
        return;
    };
    let logged_params =
        if REDACTED_PARAMS_METHODS.contains(&method.as_str()) { "<redacted>".to_owned() } else { summarize(&params) };
    let numeric_id = id.as_ref().and_then(Value::as_i64).unwrap_or(0);
    log::info!("-> {method} id={numeric_id} params={logged_params}");
    let started = Instant::now();

    // A panicking handler must not take the request down silently: its
    // task is joined so the panic becomes an internal error reply.
    let result = match tokio::spawn(handler(params, numeric_id)).await {
        Ok(result) => result,
        Err(e) => Err(RpcError::new(INTERNAL_ERROR, format!("handler failed: {e}"))),
    };
    let elapsed = started.elapsed().as_millis();
    match &result {
        Ok(value) => log::info!("<- {method} id={numeric_id} ({elapsed}ms) result={}", summarize(value)),
        Err(e) => log::warn!(
            "<- {method} id={numeric_id} failed ({elapsed}ms): code={} message={} data={}",
            e.code,
            e.message,
            e.data.as_ref().map(summarize).unwrap_or_else(|| "null".to_owned()),
        ),
    }
    if let Some(id) = id {
        notifier.send(response(id, result));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn summarize_cuts_long_values_on_a_char_boundary() {
        let long = Value::String("ж".repeat(3000));
        let text = summarize(&long);
        assert!(text.ends_with("more chars)"));
        assert!(text.starts_with("\"жж"));
        assert_eq!(summarize(&json!({"a": 1})), r#"{"a":1}"#);
    }

    #[test]
    fn response_shapes() {
        assert_eq!(response(json!(3), Ok(json!({}))), json!({"jsonrpc": "2.0", "id": 3, "result": {}}));
        let error = response(json!(4), Err(RpcError::new(1300, "gone")));
        assert_eq!(error["error"], json!({"code": 1300, "message": "gone"}));
    }
}
