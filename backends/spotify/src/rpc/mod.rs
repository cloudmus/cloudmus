//! The JSON-RPC runtime: transport, dispatch, errors, settings and
//! translation — the parts of `backends/py-rpc-common` this backend needs.

pub mod errors;
pub mod i18n;
pub mod server;
pub mod settings;

pub use errors::RpcError;
pub use server::{Notifier, Server, SourceInfo};
