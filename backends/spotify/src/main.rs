//! cloudmus backend for Spotify. See README.md.

mod account;
mod backend;
mod catalog;
mod collection;
mod config;
mod errors;
#[allow(clippy::all)]
mod generated;
mod i18n;
mod rpc;
mod stream;

fn main() {
    // stdout is the JSON-RPC channel: every log line goes to stderr, where
    // the front keeps the tail of it.
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info,librespot=warn"))
        .target(env_logger::Target::Stderr)
        .init();

    let runtime = tokio::runtime::Builder::new_multi_thread().enable_all().build().expect("tokio runtime");
    let code = runtime.block_on(async {
        match backend::build_server().await {
            Ok(server) => {
                server.run().await;
                0
            }
            Err(e) => {
                log::error!("can't start: {e}");
                1
            }
        }
    });
    // Not a runtime shutdown: a stream read or a sign-in still waiting on
    // the network has no one left to answer, and must not keep the process
    // alive after its front is gone.
    std::process::exit(code);
}
