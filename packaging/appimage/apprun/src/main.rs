//! The AppImage's AppRun. A static binary, so that it runs (and can say what
//! went wrong) on a machine where bash, glibc or any other library the app
//! needs is missing or too old.
//!
//! It prepares the environment, runs `usr/bin/cloudmus-qt` as a child, and
//! cleans up after it. Why a child and not exec: the backend manifests it
//! writes hold this run's mount path, which is gone once the AppImage exits,
//! and would shadow a dev build's backends until removed.
//!
//! Failures reach Sentry from here in two cases the app can't cover itself:
//! it died before installing its crash reporter (the dynamic linker, Qt's
//! platform plugin, a library's constructor), and it crashed, but may not be
//! started again soon enough for the SDK to send the report it stored.

mod child;
mod env_setup;
mod report;
mod sentry;

use child::Exit;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

fn main() -> ExitCode {
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    let Some(appdir) = fs::read_link("/proc/self/exe").ok().and_then(|exe| exe.parent().map(Path::to_path_buf)) else {
        eprintln!("cloudmus-apprun: can't find out where the AppImage is mounted");
        return ExitCode::from(127);
    };
    let dirs = env_setup::Dirs::from_env();
    let appimage = std::env::var_os("APPIMAGE");

    let prepared = env_setup::prepare(&appdir, &dirs, appimage.as_ref());
    // The app removes it once its crash reporter is up; still there after the
    // app has exited, it never got that far. Without a marker we can't tell.
    let marker = create_marker();

    let outcome = child::run(&appdir.join("usr/bin/cloudmus-qt"), &args, &prepared.env, marker.as_deref());

    let reporter = report::Reporter::new(&appdir, &dirs);
    let failed = outcome.exit != Some(Exit::Code(0));
    let stopped_by_user = outcome.exit.is_some_and(report::is_user_stop);
    if failed && !stopped_by_user {
        if let Some(reporter) = &reporter {
            match marker.as_deref() {
                Some(marker) if marker.exists() => reporter.startup_failure(&appdir, &outcome),
                Some(_) => reporter.stored_crashes(&dirs.app_state().join("crashes/sentry")),
                None => {}
            }
        }
    }

    for manifest in &prepared.manifests {
        let _ = fs::remove_file(manifest);
    }
    if let Some(marker) = &marker {
        let _ = fs::remove_file(marker);
    }

    if let Some(e) = &outcome.spawn_error {
        eprintln!("cloudmus-apprun: can't run cloudmus-qt: {e}");
        return ExitCode::from(if e.kind() == std::io::ErrorKind::NotFound { 127 } else { 126 });
    }
    // The shell convention: 128 + the signal.
    ExitCode::from(outcome.exit.map_or(1, |e| e.shell_code()).clamp(0, 255) as u8)
}

fn create_marker() -> Option<PathBuf> {
    let dir = match std::env::var_os("XDG_RUNTIME_DIR") {
        Some(dir) if Path::new(&dir).is_dir() => PathBuf::from(dir),
        _ => std::env::temp_dir(),
    };
    let path = dir.join(format!("cloudmus-startup-{}", std::process::id()));
    let _ = fs::remove_file(&path);
    fs::OpenOptions::new().write(true).create_new(true).open(&path).ok()?;
    Some(path)
}
