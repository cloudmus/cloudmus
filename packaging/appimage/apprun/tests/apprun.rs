//! The built AppRun against a fake AppDir: a shell script plays cloudmus-qt,
//! a local socket plays Sentry.

use serde_json::Value;
use std::fs;
use std::io::{Read, Write};
use std::net::TcpListener;
use std::os::unix::fs::PermissionsExt;
use std::os::fd::AsRawFd;
use std::path::{Path, PathBuf};
use std::process::{Command, Output, Stdio};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

struct Request {
    headers: String,
    body: Vec<u8>,
}

/// One test at a time: a test that has an executable open for writing while
/// another forks makes the first one's exec fail with "Text file busy".
static ONE_AT_A_TIME: Mutex<()> = Mutex::new(());

struct Harness {
    root: PathBuf,
    requests: Arc<Mutex<Vec<Request>>>,
    _turn: std::sync::MutexGuard<'static, ()>,
}

impl Harness {
    fn new(name: &str) -> Harness {
        let turn = ONE_AT_A_TIME.lock().unwrap_or_else(|e| e.into_inner());
        let root = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join(name);
        let _ = fs::remove_dir_all(&root);
        for dir in ["AppDir/usr/bin", "AppDir/usr/share/cloudmus", "home", "runtime"] {
            fs::create_dir_all(root.join(dir)).unwrap();
        }
        fs::copy(env!("CARGO_BIN_EXE_cloudmus-apprun"), root.join("AppDir/AppRun")).unwrap();

        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        let requests = Arc::new(Mutex::new(Vec::new()));
        let seen = Arc::clone(&requests);
        std::thread::spawn(move || {
            for stream in listener.incoming().flatten() {
                serve(stream, &seen);
            }
        });
        fs::write(root.join("AppDir/usr/share/cloudmus/sentry-dsn"), format!("http://key@127.0.0.1:{port}/7\n")).unwrap();
        fs::write(root.join("AppDir/usr/share/cloudmus/version"), "1.2.3\n").unwrap();
        Harness { root, requests, _turn: turn }
    }

    fn child(&self, script: &str) {
        let path = self.root.join("AppDir/usr/bin/cloudmus-qt");
        fs::write(&path, format!("#!/bin/sh\n{script}\n")).unwrap();
        fs::set_permissions(&path, fs::Permissions::from_mode(0o755)).unwrap();
    }

    fn state(&self) -> PathBuf {
        self.root.join("home/.local/state/cloudmus/fronts/qt")
    }

    fn command(&self) -> Command {
        let mut command = Command::new(self.root.join("AppDir/AppRun"));
        command
            .env_clear()
            .env("PATH", "/usr/bin:/bin")
            .env("HOME", self.root.join("home"))
            .env("XDG_RUNTIME_DIR", self.root.join("runtime"))
            .stdin(Stdio::null());
        command
    }

    fn run(&self) -> Output {
        self.command().output().unwrap()
    }

    /// Bodies of what Sentry received, once it has settled.
    fn bodies(&self) -> Vec<Vec<u8>> {
        std::thread::sleep(Duration::from_millis(200));
        self.requests.lock().unwrap().iter().map(|r| r.body.clone()).collect()
    }
}

fn serve(mut stream: std::net::TcpStream, seen: &Arc<Mutex<Vec<Request>>>) {
    let mut data = Vec::new();
    let mut buffer = [0u8; 4096];
    let split = loop {
        if let Some(i) = data.windows(4).position(|w| w == b"\r\n\r\n") {
            break i + 4;
        }
        match stream.read(&mut buffer) {
            Ok(0) | Err(_) => return,
            Ok(n) => data.extend_from_slice(&buffer[..n]),
        }
    };
    let headers = String::from_utf8_lossy(&data[..split]).to_string();
    let length: usize = headers
        .lines()
        .find_map(|l| l.to_ascii_lowercase().strip_prefix("content-length:").map(|v| v.trim().parse().unwrap()))
        .unwrap_or(0);
    while data.len() < split + length {
        match stream.read(&mut buffer) {
            Ok(0) | Err(_) => break,
            Ok(n) => data.extend_from_slice(&buffer[..n]),
        }
    }
    seen.lock().unwrap().push(Request { headers, body: data[split..].to_vec() });
    let _ = stream.write_all(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}");
}

/// The lines of an envelope: its header, then (item header, payload) pairs.
fn items(envelope: &[u8]) -> Vec<(Value, Vec<u8>)> {
    let first_end = envelope.iter().position(|&b| b == b'\n').unwrap();
    let mut rest = &envelope[first_end + 1..];
    let mut items = Vec::new();
    while !rest.is_empty() {
        let end = rest.iter().position(|&b| b == b'\n').unwrap();
        let header: Value = serde_json::from_slice(&rest[..end]).unwrap();
        let length = header["length"].as_u64().unwrap() as usize;
        items.push((header, rest[end + 1..end + 1 + length].to_vec()));
        rest = &rest[end + 1 + length + 1..];
    }
    items
}

fn event(envelope: &[u8]) -> Value {
    let (header, payload) = &items(envelope)[0];
    assert_eq!(header["type"], "event");
    serde_json::from_slice(payload).unwrap()
}

#[test]
fn passes_output_and_exit_code_through_and_reports_the_failed_start() {
    let h = Harness::new("failed-start");
    h.child("echo to-stdout; echo 'error while loading shared libraries: libfoo.so.1: cannot open shared object file' >&2; exit 127");
    let out = h.run();
    assert_eq!(out.status.code(), Some(127));
    assert_eq!(String::from_utf8_lossy(&out.stdout), "to-stdout\n");
    assert!(String::from_utf8_lossy(&out.stderr).contains("error while loading shared libraries"));

    let bodies = h.bodies();
    assert_eq!(bodies.len(), 1);
    let event = event(&bodies[0]);
    assert_eq!(event["level"], "fatal");
    assert_eq!(event["release"], "cloudmus-qt@1.2.3");
    assert_eq!(event["environment"], "production");
    assert_eq!(event["tags"]["kind"], "startup-failure");
    assert_eq!(event["tags"]["exit_code"], "127");
    assert!(event["message"].as_str().unwrap().contains("exit code 127"));
    assert!(event["message"].as_str().unwrap().contains("libfoo.so.1"));
    assert!(event["extra"]["stderr_tail"].as_str().unwrap().contains("libfoo.so.1"));
    let attachments = items(&bodies[0]);
    assert_eq!(attachments[1].0["filename"], "stdout.txt");
    assert_eq!(attachments[1].1, b"to-stdout\n");
    assert_eq!(attachments[2].0["filename"], "stderr.txt");
    let headers = h.requests.lock().unwrap()[0].headers.clone();
    assert!(headers.starts_with("POST /api/7/envelope/ "));
    assert!(headers.contains("sentry_key=key"));
}

#[test]
fn reports_the_signal_that_killed_an_app_that_never_started() {
    let h = Harness::new("abort");
    h.child("kill -ABRT $$");
    assert_eq!(h.run().status.code(), Some(134));
    let bodies = h.bodies();
    assert_eq!(bodies.len(), 1);
    let event = event(&bodies[0]);
    assert_eq!(event["tags"]["signal"], "SIGABRT");
    assert!(event["message"].as_str().unwrap().contains("signal 6"));
}

#[test]
fn a_development_build_is_not_production() {
    let h = Harness::new("development");
    fs::write(h.root.join("AppDir/usr/share/cloudmus/version"), "1.2.3-4-gabc\n").unwrap();
    h.child("exit 1");
    h.run();
    assert_eq!(event(&h.bodies()[0])["environment"], "development");
}

#[test]
fn says_nothing_for_success_or_a_user_stop() {
    for (name, script, code) in [("ok", "exit 0", 0), ("int", "kill -INT $$", 130), ("term", "kill -TERM $$", 143)] {
        let h = Harness::new(name);
        h.child(script);
        assert_eq!(h.run().status.code(), Some(code), "{name}");
        assert!(h.bodies().is_empty(), "{name}");
    }
}

#[test]
fn an_app_past_its_crash_reporter_is_not_a_startup_failure() {
    let h = Harness::new("started");
    h.child(r#"rm "$CLOUDMUS_STARTUP_MARKER"; exit 1"#);
    assert_eq!(h.run().status.code(), Some(1));
    assert!(h.bodies().is_empty());
}

#[test]
fn crash_reports_switched_off_in_settings_send_nothing() {
    let h = Harness::new("opted-out");
    let config = h.root.join("home/.config/cloudmus/fronts/qt");
    fs::create_dir_all(&config).unwrap();
    fs::write(config.join("config.ini"), "[crashReports]\nenabled=false\n").unwrap();
    h.child("exit 127");
    h.run();
    assert!(h.bodies().is_empty());
}

#[test]
fn a_build_without_a_dsn_sends_nothing() {
    let h = Harness::new("no-dsn");
    fs::remove_file(h.root.join("AppDir/usr/share/cloudmus/sentry-dsn")).unwrap();
    h.child("exit 127");
    h.run();
    assert!(h.bodies().is_empty());
}

fn store_crash(h: &Harness, uuid: &str, envelope: &[u8]) -> (PathBuf, PathBuf) {
    let database = h.state().join("crashes/sentry");
    let run = database.join(format!("{uuid}.run"));
    fs::create_dir_all(&run).unwrap();
    fs::write(run.join("e1.envelope"), envelope).unwrap();
    fs::write(run.join("session.json"), "{}").unwrap();
    let lock = database.join(format!("{uuid}.run.lock"));
    fs::write(&lock, "").unwrap();
    (run, lock)
}

#[test]
fn a_crash_the_sdk_stored_is_sent_at_once_and_only_once() {
    let h = Harness::new("stored-crash");
    let envelope = b"{\"event_id\":\"abc\"}\n{\"type\":\"event\",\"length\":2}\n{}\n";
    let (run, lock) = store_crash(&h, "11111111-2222-3333-4444-555555555555", envelope);
    h.child(r#"rm "$CLOUDMUS_STARTUP_MARKER"; kill -SEGV $$"#);
    assert_eq!(h.run().status.code(), Some(139));
    assert_eq!(h.bodies(), vec![envelope.to_vec()]);
    assert!(!run.exists() && !lock.exists());
}

#[test]
fn a_run_still_locked_belongs_to_a_live_process() {
    let h = Harness::new("locked-run");
    let (run, lock) = store_crash(&h, "11111111-2222-3333-4444-555555555555", b"{}\n");
    let held = fs::File::open(&lock).unwrap();
    // SAFETY: flock(2) on a descriptor we own.
    assert_eq!(unsafe { libc::flock(held.as_raw_fd(), libc::LOCK_EX | libc::LOCK_NB) }, 0);
    h.child(r#"rm "$CLOUDMUS_STARTUP_MARKER"; kill -SEGV $$"#);
    h.run();
    assert!(h.bodies().is_empty());
    assert!(run.join("e1.envelope").exists());
}

#[test]
fn sets_up_the_environment_and_cleans_up_after_the_app() {
    let h = Harness::new("environment");
    let seen = h.root.join("seen");
    fs::create_dir_all(&seen).unwrap();
    fs::create_dir_all(h.root.join("home/.config")).unwrap();
    fs::write(h.root.join("home/.config/kcminputrc"), "[Mouse]\ncursorTheme=Bibata\n").unwrap();
    let certifi = h.root.join("AppDir/usr/python-runtime/lib/python3.11/site-packages/certifi");
    fs::create_dir_all(&certifi).unwrap();
    fs::write(certifi.join("cacert.pem"), "").unwrap();
    fs::create_dir_all(h.root.join("AppDir/usr/share/icons/hicolor/512x512/apps")).unwrap();
    fs::create_dir_all(h.root.join("AppDir/usr/share/applications")).unwrap();
    fs::write(h.root.join("AppDir/usr/share/icons/hicolor/512x512/apps/cloudmus-qt.png"), "png").unwrap();
    fs::write(h.root.join("AppDir/usr/share/applications/cloudmus-qt.desktop"), "[Desktop Entry]\nExec=cloudmus-qt\nIcon=cloudmus-qt\n").unwrap();
    // A user's own manifest, and what an earlier version left behind.
    let backends = h.root.join("home/.config/cloudmus/backends.d");
    fs::create_dir_all(&backends).unwrap();
    fs::write(backends.join("mine.json"), "{}").unwrap();
    fs::create_dir_all(h.root.join("home/.cache/cloudmus/mpv-appimage")).unwrap();

    h.child(&format!(
        r#"env > "{seen}/env"; cp "{backends}/yandex-music.json" "{seen}/manifest"; echo "$@" > "{seen}/args"; rm "$CLOUDMUS_STARTUP_MARKER""#,
        seen = seen.display(),
        backends = backends.display(),
    ));
    let out = h.command().env("APPIMAGE", "/x/CloudMus.AppImage").args(["--debug", "two words"]).output().unwrap();
    assert_eq!(out.status.code(), Some(0));

    let env = fs::read_to_string(seen.join("env")).unwrap();
    assert!(env.contains("QT_QPA_PLATFORMTHEME=xdgdesktopportal\n"));
    assert!(env.contains("XCURSOR_THEME=Bibata\n"));
    assert!(env.contains("CLOUDMUS_TLS_CA_FILE="));
    assert!(!env.contains("CLOUDMUS_MPV_CONFIG_DIR"));
    assert_eq!(fs::read_to_string(seen.join("args")).unwrap(), "--debug two words\n");
    let manifest: Value = serde_json::from_slice(&fs::read(seen.join("manifest")).unwrap()).unwrap();
    assert!(manifest["argv"][0].as_str().unwrap().ends_with("/AppDir/usr/bin/python3"));

    assert!(!backends.join("yandex-music.json").exists(), "removed on exit");
    assert!(backends.join("mine.json").exists(), "a user's manifest is left alone");
    assert!(!h.root.join("home/.cache/cloudmus/mpv-appimage").exists());
    let desktop = fs::read_to_string(h.root.join("home/.local/share/applications/cloudmus-qt.desktop")).unwrap();
    assert!(desktop.contains("Exec=/x/CloudMus.AppImage\n"));
    assert!(h.root.join("home/.local/share/icons/hicolor/512x512/apps/cloudmus-qt.png").exists());
}

#[test]
fn a_missing_app_is_reported_as_not_started() {
    let h = Harness::new("no-binary");
    let out = h.run();
    assert_eq!(out.status.code(), Some(127));
    let bodies = h.bodies();
    assert_eq!(bodies.len(), 1);
    assert_eq!(event(&bodies[0])["tags"]["exit_code"], "127");
}

#[test]
fn a_stop_request_goes_to_the_app_and_the_cleanup_still_happens() {
    let h = Harness::new("sigterm");
    let ready = h.root.join("ready");
    h.child(&format!(
        r#"rm "$CLOUDMUS_STARTUP_MARKER"; trap 'exit 0' TERM; touch "{}"; while :; do sleep 0.05; done"#,
        ready.display()
    ));
    let child = h.command().stdout(Stdio::piped()).stderr(Stdio::piped()).spawn().unwrap();
    let deadline = Instant::now() + Duration::from_secs(10);
    while !ready.exists() {
        assert!(Instant::now() < deadline, "the app never started");
        std::thread::sleep(Duration::from_millis(20));
    }
    // SAFETY: plain kill(2) on the process we started.
    unsafe { libc::kill(child.id() as i32, libc::SIGTERM) };
    let out = child.wait_with_output().unwrap();
    assert_eq!(out.status.code(), Some(0));
    assert!(!Path::new(&h.root.join("home/.config/cloudmus/backends.d/yandex-music.json")).exists());
    assert!(h.bodies().is_empty());
}

fn stderr_of(out: &Output) -> String {
    String::from_utf8_lossy(&out.stderr).into_owned()
}

#[test]
fn waits_for_the_backends_of_a_crashed_app_to_let_go_of_the_lock() {
    let h = Harness::new("lock-released-late");
    let envelope = b"{\"event_id\":\"abc\"}\n{\"type\":\"event\",\"length\":2}\n{}\n";
    let (run, lock) = store_crash(&h, "11111111-2222-3333-4444-555555555555", envelope);
    // What the app's backends do: hold the descriptor a moment longer.
    let held = fs::File::open(&lock).unwrap();
    // SAFETY: flock(2) on a descriptor we own.
    assert_eq!(unsafe { libc::flock(held.as_raw_fd(), libc::LOCK_EX | libc::LOCK_NB) }, 0);
    std::thread::spawn(move || {
        std::thread::sleep(Duration::from_millis(1200));
        drop(held);
    });
    h.child(r#"rm "$CLOUDMUS_STARTUP_MARKER"; kill -SEGV $$"#);
    let out = h.run();
    assert_eq!(h.bodies(), vec![envelope.to_vec()]);
    assert!(!run.exists());
    let log = stderr_of(&out);
    assert!(log.contains("cloudmus-qt was killed by signal 11 (SIGSEGV)"), "{log}");
    assert!(log.contains("sent the crash report"), "{log}");
}

#[test]
fn says_why_nothing_was_sent() {
    let h = Harness::new("explains");
    fs::remove_file(h.root.join("AppDir/usr/share/cloudmus/sentry-dsn")).unwrap();
    h.child("exit 3");
    let log = stderr_of(&h.run());
    assert!(log.contains("cloudmus-qt exited with code 3"), "{log}");
    assert!(log.contains("not reporting: this build has no Sentry DSN"), "{log}");
    drop(h);

    let h = Harness::new("explains-opt-out");
    let config = h.root.join("home/.config/cloudmus/fronts/qt");
    fs::create_dir_all(&config).unwrap();
    fs::write(config.join("config.ini"), "[crashReports]\nenabled=false\n").unwrap();
    h.child("exit 3");
    assert!(stderr_of(&h.run()).contains("not reporting: crash reports are switched off in Settings"));
    drop(h);

    let h = Harness::new("explains-no-report");
    h.child(r#"rm "$CLOUDMUS_STARTUP_MARKER"; kill -SEGV $$"#);
    let log = stderr_of(&h.run());
    assert!(log.contains("the app left no crash report to send"), "{log}");
}

#[test]
fn a_run_still_locked_after_the_wait_is_left_for_the_next_start() {
    let h = Harness::new("lock-never-released");
    let (run, lock) = store_crash(&h, "11111111-2222-3333-4444-555555555555", b"{}\n");
    let held = fs::File::open(&lock).unwrap();
    // SAFETY: flock(2) on a descriptor we own.
    assert_eq!(unsafe { libc::flock(held.as_raw_fd(), libc::LOCK_EX | libc::LOCK_NB) }, 0);
    h.child(r#"rm "$CLOUDMUS_STARTUP_MARKER"; kill -SEGV $$"#);
    let log = stderr_of(&h.run());
    assert!(log.contains("is still in use by a running process"), "{log}");
    assert!(run.join("e1.envelope").exists());
    drop(held);
}

#[test]
fn debug_output_only_with_the_debug_flag() {
    let h = Harness::new("debug");
    h.child(r#"rm "$CLOUDMUS_STARTUP_MARKER"; echo "args: $@""#);
    let quiet = h.run();
    assert!(!stderr_of(&quiet).contains("debug:"));

    let out = h.command().arg("--debug").output().unwrap();
    assert!(String::from_utf8_lossy(&out.stdout).contains("args: --debug"), "the flag reaches the app");
    let log = stderr_of(&out);
    for expected in ["debug: version", "debug: child environment: QT_QPA_PLATFORMTHEME=", "debug: starting", "debug: cloudmus-qt finished"] {
        assert!(log.contains(expected), "{expected} missing in {log}");
    }

    let by_env = h.command().env("CLOUDMUS_QT_DEBUG", "1").output().unwrap();
    assert!(stderr_of(&by_env).contains("debug: version"));
}
