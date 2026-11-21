//! The Rust counterpart of `backends/py-rpc-common`: what every CloudMus
//! backend written in Rust shares — the stdio JSON-RPC server, error codes,
//! translation, settings, the user's config and cache folders, and the
//! protocol's types and names generated from `protocol/`
//! (`python protocol/codegen/generate.py --lang rust`).

pub mod errors;
#[allow(clippy::all)]
pub mod generated;
pub mod i18n;
pub mod paths;
pub mod server;
pub mod settings;

pub use errors::RpcError;
pub use server::{Notifier, Server, SourceInfo};
