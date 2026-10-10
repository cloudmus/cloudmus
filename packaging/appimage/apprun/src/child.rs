//! Runs cloudmus-qt as a child: output goes through to our own stdout/stderr
//! as it comes, while the start of each stream is kept for a report.

use crate::log::debug;
use signal_hook::consts::{SIGHUP, SIGINT, SIGTERM};
use signal_hook::iterator::Signals;
use std::ffi::OsString;
use std::io::{Read, Write};
use std::os::unix::process::ExitStatusExt;
use std::path::Path;
use std::process::{Command, Stdio};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

/// A crash report needs the start of the output (the linker's complaint, Qt's
/// platform plugin error), not a whole session's worth.
const KEPT_BYTES: usize = 64 * 1024;
/// How long to wait for the output to end once the child is gone: something
/// it started may still hold the pipes open.
const DRAIN_WAIT: Duration = Duration::from_secs(2);

pub struct Outcome {
    /// `None` if the program couldn't be started.
    pub exit: Option<Exit>,
    pub spawn_error: Option<std::io::Error>,
    pub stdout: Vec<u8>,
    pub stderr: Vec<u8>,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Exit {
    Code(i32),
    Signal(i32),
}

impl Exit {
    /// What a shell would report.
    pub fn shell_code(self) -> i32 {
        match self {
            Exit::Code(code) => code,
            Exit::Signal(signal) => 128 + signal,
        }
    }
}

pub fn run(program: &Path, args: &[OsString], env: &[(&str, OsString)], marker: Option<&Path>) -> Outcome {
    let mut command = Command::new(program);
    command
        .args(args)
        .envs(env.iter().map(|(k, v)| (*k, v)))
        .stdin(Stdio::inherit())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    if let Some(marker) = marker {
        command.env("CLOUDMUS_STARTUP_MARKER", marker);
    }
    debug!("starting {} {:?}", program.display(), args);
    let mut child = match command.spawn() {
        Ok(child) => child,
        Err(e) => return Outcome { exit: None, spawn_error: Some(e), stdout: Vec::new(), stderr: Vec::new() },
    };

    // A stop request is for the app, not for us: we have to outlive it to
    // clean up and report.
    let pid = child.id() as libc::pid_t;
    debug!("cloudmus-qt runs as pid {pid}");
    let signals = Signals::new([SIGINT, SIGTERM, SIGHUP]).ok();
    let signals_handle = signals.as_ref().map(|s| s.handle());
    let forwarder = signals.map(|mut signals| {
        thread::spawn(move || {
            for signal in signals.forever() {
                debug!("signal {signal} for the app, passing it on");
                // SAFETY: plain kill(2) on our own child.
                unsafe { libc::kill(pid, signal) };
            }
        })
    });

    let stdout = Relay::start(child.stdout.take().expect("piped"), Sink::Stdout);
    let stderr = Relay::start(child.stderr.take().expect("piped"), Sink::Stderr);

    let status = child.wait();
    if let Some(handle) = signals_handle {
        handle.close();
    }
    if let Some(forwarder) = forwarder {
        let _ = forwarder.join();
    }

    debug!("pid {pid} is gone: {status:?}");
    let deadline = Instant::now() + DRAIN_WAIT;
    let outcome = Outcome {
        exit: status.ok().map(|s| match (s.code(), s.signal()) {
            (Some(code), _) => Exit::Code(code),
            (None, Some(signal)) => Exit::Signal(signal),
            (None, None) => Exit::Code(1),
        }),
        spawn_error: None,
        stdout: stdout.finish(deadline),
        stderr: stderr.finish(deadline),
    };
    outcome
}

#[derive(Clone, Copy)]
enum Sink {
    Stdout,
    Stderr,
}

struct Relay {
    kept: Arc<Mutex<Vec<u8>>>,
    thread: JoinHandle<()>,
}

impl Relay {
    fn start<R: Read + Send + 'static>(mut from: R, sink: Sink) -> Relay {
        let kept = Arc::new(Mutex::new(Vec::new()));
        let kept_by_thread = Arc::clone(&kept);
        let thread = thread::spawn(move || {
            let mut buffer = [0u8; 8192];
            // Once our own output is gone (a closed terminal or pipe) we
            // still read on: the child must not be blocked or killed by it.
            let mut open = true;
            loop {
                let n = match from.read(&mut buffer) {
                    Ok(0) => break,
                    Ok(n) => n,
                    Err(e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
                    Err(_) => break,
                };
                let chunk = &buffer[..n];
                if open {
                    let written = match sink {
                        Sink::Stdout => write_through(&mut std::io::stdout().lock(), chunk),
                        Sink::Stderr => write_through(&mut std::io::stderr().lock(), chunk),
                    };
                    open = written.is_ok();
                }
                let mut kept = kept_by_thread.lock().unwrap();
                let room = KEPT_BYTES.saturating_sub(kept.len());
                kept.extend_from_slice(&chunk[..room.min(n)]);
            }
        });
        Relay { kept, thread }
    }

    /// What was kept, once the stream has ended or the deadline passed.
    fn finish(self, deadline: Instant) -> Vec<u8> {
        while !self.thread.is_finished() && Instant::now() < deadline {
            thread::sleep(Duration::from_millis(10));
        }
        let kept = self.kept.lock().unwrap().clone();
        kept
    }
}

fn write_through(to: &mut impl Write, chunk: &[u8]) -> std::io::Result<()> {
    to.write_all(chunk)?;
    to.flush()
}
