//! Error codes (docs/protocol.md §9), as in `rpc_common/errors.py`.

use serde_json::{json, Map, Value};

pub const INVALID_PARAMS: i64 = -32602;
pub const METHOD_NOT_FOUND: i64 = -32601;
pub const INTERNAL_ERROR: i64 = -32603;

pub const AUTH_NOT_AUTHENTICATED: i64 = 1000;
pub const AUTH_EXPIRED: i64 = 1001;
pub const UPSTREAM_UNREACHABLE: i64 = 1200;
pub const RESOURCE_NOT_FOUND: i64 = 1300;
pub const STATE_INVALID: i64 = 1400;

/// data.reason of a track that will not play (§9.1).
pub const REASON_UNAVAILABLE: &str = "unavailable";
pub const REASON_OFFLINE: &str = "offline";

#[derive(Debug, Clone)]
pub struct RpcError {
    pub code: i64,
    pub message: String,
    pub data: Option<Value>,
}

impl RpcError {
    pub fn new(code: i64, message: impl Into<String>) -> Self {
        RpcError { code, message: message.into(), data: None }
    }

    pub fn with_data(mut self, data: Value) -> Self {
        self.data = Some(data);
        self
    }

    pub fn invalid_params(message: impl Into<String>) -> Self {
        Self::new(INVALID_PARAMS, message)
    }

    pub fn to_json(&self) -> Value {
        let mut error = json!({"code": self.code, "message": self.message});
        if let Some(data) = &self.data {
            error["data"] = data.clone();
        }
        error
    }
}

/// The `data` of an application error: whether retrying may help, plus
/// what the front shows or acts on.
pub fn app_error_data(retryable: bool, detail: Option<&str>, track_id: Option<&str>, reason: Option<&str>) -> Value {
    let mut data = Map::new();
    data.insert("retryable".into(), retryable.into());
    if let Some(detail) = detail {
        data.insert("detail".into(), detail.into());
    }
    if let Some(track_id) = track_id {
        data.insert("trackId".into(), track_id.into());
    }
    if let Some(reason) = reason {
        data.insert("reason".into(), reason.into());
    }
    Value::Object(data)
}

/// Params of a request, typed by the generated models; a mismatch is the
/// caller's fault (-32602), not ours.
pub fn parse_params<T: serde::de::DeserializeOwned>(params: serde_json::Value) -> Result<T, RpcError> {
    serde_json::from_value(params).map_err(|e| RpcError::invalid_params(format!("invalid params: {e}")))
}

/// A result built from a generated model.
pub fn to_result(value: impl serde::Serialize) -> Result<Value, RpcError> {
    serde_json::to_value(value).map_err(|e| RpcError::new(INTERNAL_ERROR, e.to_string()))
}
