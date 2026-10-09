//! Sending to Sentry: an event for an app that never started, and the crash
//! report sentry-native left on disk for an app that did.

use crate::child::{Exit, Outcome};
use crate::env_setup::Dirs;
use crate::sentry::{self, Dsn, Item};
use serde_json::{json, Map, Value};
use std::ffi::OsStr;
use std::fs;
use std::os::fd::AsRawFd;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

const SIGHUP: i32 = 1;
const SIGINT: i32 = 2;
const SIGTERM: i32 = 15;

/// Where to report to, if reporting is on at all: the build has a DSN
/// (build-in-docker.sh writes it) and the user hasn't switched crash reports
/// off in Settings. The same two conditions as in the app.
pub struct Reporter {
    dsn_text: String,
    dsn: Dsn,
    release: String,
}

impl Reporter {
    pub fn new(appdir: &Path, dirs: &Dirs) -> Option<Reporter> {
        let share = appdir.join("usr/share/cloudmus");
        let dsn_text = fs::read_to_string(share.join("sentry-dsn")).ok()?.trim().to_string();
        let dsn = Dsn::parse(&dsn_text)?;
        let release = fs::read_to_string(share.join("version")).map_or_else(|_| "unknown".into(), |v| v.trim().into());
        let enabled = fs::read_to_string(dirs.app_config_file()).map_or(true, |ini| crash_reports_enabled(&ini));
        enabled.then_some(Reporter { dsn_text, dsn, release })
    }

    /// The app was started but died before it installed its own crash
    /// reporter: nothing else will ever know.
    pub fn startup_failure(&self, appdir: &Path, outcome: &Outcome) {
        let event_id = sentry::new_event_id();
        let event = self.startup_event(&event_id, appdir, outcome);
        let envelope = sentry::build_envelope(
            &event_id,
            &self.dsn_text,
            vec![
                Item::event(&event),
                Item::attachment("stdout.txt", outcome.stdout.clone()),
                Item::attachment("stderr.txt", outcome.stderr.clone()),
            ],
        );
        match sentry::send(&self.dsn, &envelope) {
            Ok(()) => eprintln!("cloudmus-apprun: sent a report of the failed start to Sentry ({event_id})"),
            Err(e) => eprintln!("cloudmus-apprun: couldn't send a report of the failed start: {e}"),
        }
    }

    /// sentry-native stores a crash and sends it at the next start; the user
    /// may not start the app again. A run directory is `<uuid>.run` with
    /// `<uuid>.run.lock` next to it, which its process holds locked while it
    /// lives, so a locked one is still in use and left alone. Deleted only
    /// once Sentry has it, as the SDK does, so a failed send is retried by
    /// the next start.
    pub fn stored_crashes(&self, database: &Path) {
        let Ok(entries) = fs::read_dir(database) else { return };
        for run in entries.flatten().map(|e| e.path()) {
            if run.extension() != Some(OsStr::new("run")) || !run.is_dir() {
                continue;
            }
            let lock_path = PathBuf::from(format!("{}.lock", run.display()));
            let Some(_lock) = try_lock(&lock_path) else { continue };
            let mut envelopes: Vec<PathBuf> = fs::read_dir(&run)
                .into_iter()
                .flatten()
                .flatten()
                .map(|e| e.path())
                .filter(|p| p.extension() == Some(OsStr::new("envelope")))
                .collect();
            envelopes.sort();
            let mut all_sent = true;
            for path in &envelopes {
                let Ok(bytes) = fs::read(path) else { continue };
                match sentry::send(&self.dsn, &bytes) {
                    Ok(()) => eprintln!("cloudmus-apprun: sent the crash report {} to Sentry", path.display()),
                    Err(e) => {
                        eprintln!("cloudmus-apprun: couldn't send the crash report {}: {e}", path.display());
                        all_sent = false;
                    }
                }
            }
            if all_sent {
                let _ = fs::remove_dir_all(&run);
                let _ = fs::remove_file(&lock_path);
            }
        }
    }

    fn startup_event(&self, event_id: &str, appdir: &Path, outcome: &Outcome) -> Value {
        let exit = outcome.exit.map(Exit::shell_code);
        let (title, tag_name, tag_value) = describe(outcome);
        let stderr = String::from_utf8_lossy(&outcome.stderr);
        let reason = telling_line(&stderr);
        let message = match &reason {
            Some(line) => format!("{title} ({line})"),
            None => title.clone(),
        };
        let environment = if is_release_tag(&self.release) { "production" } else { "development" };

        let mut tags = Map::new();
        tags.insert("kind".into(), json!("startup-failure"));
        tags.insert(tag_name.into(), json!(tag_value));
        for (tag, var) in [("session_type", "XDG_SESSION_TYPE"), ("desktop", "XDG_CURRENT_DESKTOP")] {
            if let Ok(value) = std::env::var(var) {
                tags.insert(tag.into(), json!(value));
            }
        }

        let tail: Vec<&str> = stderr.lines().rev().take(30).collect::<Vec<_>>().into_iter().rev().collect();
        let mut fingerprint = vec!["startup-failure".to_string(), tag_value.clone()];
        fingerprint.extend(reason);
        json!({
            "event_id": event_id,
            "timestamp": SystemTime::now().duration_since(UNIX_EPOCH).map_or(0.0, |d| d.as_secs_f64()),
            "platform": "native",
            "level": "fatal",
            "release": format!("cloudmus-qt@{}", self.release),
            "environment": environment,
            "message": message,
            "fingerprint": fingerprint,
            "tags": tags,
            "contexts": {
                "os": os_context(),
                "startup": startup_context(appdir, exit),
            },
            "extra": { "stderr_tail": tail.join("\n") },
        })
    }
}

/// `enabled` under `[crashReports]` in the app's config.ini (a QSettings
/// file), on by default: `Settings::crashReportsEnabledOnDisk()`.
fn crash_reports_enabled(ini: &str) -> bool {
    let mut in_section = false;
    let mut enabled = true;
    for line in ini.lines().map(str::trim) {
        if let Some(section) = line.strip_prefix('[').and_then(|l| l.strip_suffix(']')) {
            in_section = section == "crashReports";
        } else if in_section {
            if let Some(value) = line.strip_prefix("enabled=") {
                enabled = !matches!(value.trim(), "false" | "0");
            }
        }
    }
    enabled
}

/// A tagged release is `x.y.z`; anything `git describe` adds is a dev build
/// (`sentryEnvironment()` in CrashReporter.cpp).
fn is_release_tag(version: &str) -> bool {
    let parts: Vec<&str> = version.split('.').collect();
    parts.len() == 3 && parts.iter().all(|p| !p.is_empty() && p.bytes().all(|b| b.is_ascii_digit()))
}

/// The message, and the tag that names the cause: exit code or signal.
fn describe(outcome: &Outcome) -> (String, &'static str, String) {
    match (outcome.exit, &outcome.spawn_error) {
        (Some(Exit::Code(code)), _) => (format!("cloudmus-qt did not start: exit code {code}"), "exit_code", code.to_string()),
        (Some(Exit::Signal(n)), _) => {
            (format!("cloudmus-qt did not start: killed by signal {n} ({})", signal_name(n)), "signal", signal_name(n).into())
        }
        (None, Some(e)) => {
            let code = if e.kind() == std::io::ErrorKind::NotFound { 127 } else { 126 };
            (format!("cloudmus-qt did not start: {e}"), "exit_code", code.to_string())
        }
        (None, None) => ("cloudmus-qt did not start".into(), "exit_code", "unknown".into()),
    }
}

pub fn signal_name(signal: i32) -> &'static str {
    match signal {
        1 => "SIGHUP",
        2 => "SIGINT",
        3 => "SIGQUIT",
        4 => "SIGILL",
        5 => "SIGTRAP",
        6 => "SIGABRT",
        7 => "SIGBUS",
        8 => "SIGFPE",
        9 => "SIGKILL",
        11 => "SIGSEGV",
        13 => "SIGPIPE",
        14 => "SIGALRM",
        15 => "SIGTERM",
        _ => "unknown signal",
    }
}

/// The user stopping the app is not a failure.
pub fn is_user_stop(exit: Exit) -> bool {
    match exit {
        Exit::Signal(s) => matches!(s, SIGHUP | SIGINT | SIGTERM),
        Exit::Code(c) => matches!(c, 129 | 130 | 143),
    }
}

/// The line that says why, for the cases that differ per machine; it goes
/// into the message and the grouping, where an ordinary log line (with its
/// timestamp) would split every report into its own issue.
fn telling_line(stderr: &str) -> Option<String> {
    const PATTERNS: [&str; 7] = [
        "error while loading shared libraries",
        "cannot open shared object file",
        "version `GLIBC",
        "version `GLIBCXX",
        "undefined symbol",
        // Qt's platform plugin: "Could not load…" when its own library is
        // missing (libxcb-cursor), and the generic line after either failure.
        "Could not load the Qt platform plugin",
        "no Qt platform plugin could be initialized",
    ];
    let line = stderr.lines().find(|l| PATTERNS.iter().any(|p| l.contains(p)))?;
    Some(line.trim().chars().take(200).collect())
}

fn os_context() -> Value {
    let release = fs::read_to_string("/etc/os-release")
        .or_else(|_| fs::read_to_string("/usr/lib/os-release"))
        .unwrap_or_default();
    let field = |name: &str| {
        release
            .lines()
            .find_map(|l| l.strip_prefix(&format!("{name}=")))
            .map(|v| v.trim().trim_matches('"').to_string())
    };
    json!({
        "name": field("NAME"),
        "version": field("VERSION_ID"),
        "pretty_name": field("PRETTY_NAME"),
        "kernel_version": fs::read_to_string("/proc/sys/kernel/osrelease").ok().map(|k| k.trim().to_string()),
    })
}

fn startup_context(appdir: &Path, exit: Option<i32>) -> Value {
    let var = |name: &str| std::env::var(name).ok();
    let present = |name: &str| std::env::var_os(name).is_some_and(|v| !v.is_empty());
    json!({
        "exit": exit,
        "host_glibc": host_glibc(),
        "session_type": var("XDG_SESSION_TYPE"),
        "desktop": var("XDG_CURRENT_DESKTOP"),
        "wayland_display_set": present("WAYLAND_DISPLAY"),
        "x11_display_set": present("DISPLAY"),
        "qt_qpa_platform": var("QT_QPA_PLATFORM"),
        "lang": var("LANG"),
        "missing_libraries": missing_libraries(&appdir.join("usr/bin/cloudmus-qt")),
    })
}

/// Running libc.so.6 prints its banner, with the version. No ldd: that is a
/// shell script, and the shell may be what is broken.
fn host_glibc() -> Option<String> {
    ["/lib64/libc.so.6", "/lib/x86_64-linux-gnu/libc.so.6", "/usr/lib64/libc.so.6", "/usr/lib/libc.so.6"]
        .iter()
        .find_map(|path| {
            let text = run_for_text(Command::new(path), Duration::from_secs(3))?;
            text.lines().next().map(str::to_string)
        })
}

/// The libraries the dynamic linker can't find for the binary: it lists them
/// itself when asked to trace instead of run, with the binary's own RUNPATH.
fn missing_libraries(binary: &Path) -> Vec<String> {
    let mut command = Command::new(binary);
    command.env("LD_TRACE_LOADED_OBJECTS", "1");
    run_for_text(command, Duration::from_secs(5))
        .unwrap_or_default()
        .lines()
        .filter(|l| l.contains("not found"))
        .map(|l| l.trim().to_string())
        .collect()
}

/// Stdout and stderr of a short-lived command, or None if it can't be run;
/// killed if it takes longer than `limit`.
fn run_for_text(mut command: Command, limit: Duration) -> Option<String> {
    let mut child = command.stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::piped()).spawn().ok()?;
    let mut stdout = child.stdout.take()?;
    let mut stderr = child.stderr.take()?;
    let reader = std::thread::spawn(move || {
        let mut text = Vec::new();
        let _ = std::io::Read::read_to_end(&mut stdout, &mut text);
        let _ = std::io::Read::read_to_end(&mut stderr, &mut text);
        text
    });
    let deadline = std::time::Instant::now() + limit;
    loop {
        match child.try_wait() {
            Ok(Some(_)) => break,
            Ok(None) if std::time::Instant::now() < deadline => std::thread::sleep(Duration::from_millis(10)),
            _ => {
                let _ = child.kill();
                let _ = child.wait();
                break;
            }
        }
    }
    Some(String::from_utf8_lossy(&reader.join().ok()?).into_owned())
}

/// Holds the flock for as long as it lives; None if somebody else holds it
/// (or there is no lock file: not a run directory of this SDK).
#[allow(dead_code)] // held for its Drop
struct Lock(fs::File);

fn try_lock(path: &Path) -> Option<Lock> {
    let file = fs::OpenOptions::new().read(true).write(true).open(path).ok()?;
    // SAFETY: flock(2) on a file descriptor we own.
    let locked = unsafe { libc::flock(file.as_raw_fd(), libc::LOCK_EX | libc::LOCK_NB) } == 0;
    locked.then_some(Lock(file))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crash_reports_are_on_unless_switched_off() {
        assert!(crash_reports_enabled(""));
        assert!(crash_reports_enabled("[crashReports]\nenabled=true\n"));
        assert!(!crash_reports_enabled("[General]\nx=1\n\n[crashReports]\nenabled=false\n"));
        assert!(crash_reports_enabled("[analytics]\nenabled=false\n"));
    }

    #[test]
    fn release_tags_and_development_builds() {
        assert!(is_release_tag("1.4.0"));
        assert!(!is_release_tag("1.4.0-3-gabc123"));
        assert!(!is_release_tag("0.1.0+gabc"));
        assert!(!is_release_tag("unknown"));
    }

    #[test]
    fn user_stops_are_not_failures() {
        assert!(is_user_stop(Exit::Signal(2)));
        assert!(is_user_stop(Exit::Code(143)));
        assert!(!is_user_stop(Exit::Signal(6)));
        assert!(!is_user_stop(Exit::Code(127)));
    }

    #[test]
    fn the_telling_line_ignores_ordinary_log_lines() {
        let stderr = "2026-01-01T00:00:00 [Warning] noise\n./cloudmus-qt: error while loading shared libraries: libfoo.so.1: cannot open shared object file\n";
        assert_eq!(
            telling_line(stderr).as_deref(),
            Some("./cloudmus-qt: error while loading shared libraries: libfoo.so.1: cannot open shared object file")
        );
        assert_eq!(telling_line("just noise\n"), None);
        let qt = "qt.qpa.plugin: Could not find the Qt platform plugin \"x\" in \"\"\nThis application failed to start because no Qt platform plugin could be initialized. Reinstalling may fix this.\n";
        assert!(telling_line(qt).unwrap().starts_with("This application failed to start because no Qt platform plugin"));
    }

    #[test]
    fn signals_have_names() {
        assert_eq!(signal_name(6), "SIGABRT");
        assert_eq!(signal_name(11), "SIGSEGV");
    }
}
