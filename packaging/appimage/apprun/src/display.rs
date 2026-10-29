//! The display the app is to open its window on. A login launch can come
//! before the compositor is up, or before the session has handed
//! WAYLAND_DISPLAY to whatever starts the autostart entries; Qt then finds no
//! platform plugin to work with and aborts (CLOUDMUS-QT-G).

use crate::log::debug;
use std::ffi::OsString;
use std::fs;
use std::os::unix::fs::FileTypeExt;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

/// What the autostart entry passes: `kLaunchedAtLoginArgument` in
/// fronts/qt/src/Integration/Autostart.h.
const LAUNCHED_AT_LOGIN: &str = "--autostart";
/// Long enough for a slow login, short enough that a launch outside any
/// graphical session still ends with a report.
const WAIT: Duration = Duration::from_secs(30);
const POLL: Duration = Duration::from_millis(250);

pub fn launched_at_login(args: &[OsString]) -> bool {
    args.iter().any(|a| a == LAUNCHED_AT_LOGIN)
}

/// Whether the app is started with a display to connect to: set for us, or
/// added to its environment (`env`) by `wait_for_wayland`.
pub fn has_display(env: &[(&str, OsString)]) -> bool {
    ["WAYLAND_DISPLAY", "DISPLAY"].iter().any(|name| {
        std::env::var_os(name).is_some_and(|v| !v.is_empty()) || env.iter().any(|(k, v)| k == name && !v.is_empty())
    })
}

/// Where the compositor puts its socket; without XDG_RUNTIME_DIR, systemd's
/// usual place for it.
pub fn runtime_dir() -> PathBuf {
    match std::env::var_os("XDG_RUNTIME_DIR") {
        Some(dir) if !dir.is_empty() => PathBuf::from(dir),
        // SAFETY: getuid(2) can't fail.
        _ => PathBuf::from(format!("/run/user/{}", unsafe { libc::getuid() })),
    }
}

/// The compositor sockets (`wayland-0`, …) in `dir`, sorted; not their
/// `.lock` files, and not a leftover regular file.
pub fn wayland_sockets(dir: &Path) -> Vec<String> {
    let mut sockets: Vec<String> = fs::read_dir(dir)
        .into_iter()
        .flatten()
        .flatten()
        .filter(|e| e.file_type().is_ok_and(|t| t.is_socket()))
        .filter_map(|e| e.file_name().into_string().ok())
        .filter(|name| name.strip_prefix("wayland-").is_some_and(|n| !n.is_empty() && n.bytes().all(|b| b.is_ascii_digit())))
        .collect();
    sockets.sort();
    sockets
}

/// For a login launch with no display: waits for a Wayland compositor's
/// socket and hands it to the app as WAYLAND_DISPLAY (and XDG_RUNTIME_DIR, if
/// that was missing too). Libwayland would try `wayland-0` by itself, but
/// only once, at once, and KWin may well be on `wayland-1`. Any other launch is left as it is: started by hand
/// from a terminal without a session, it should fail now, not in half a
/// minute.
pub fn wait_for_wayland(args: &[OsString], env: &mut Vec<(&'static str, OsString)>) {
    let platform = std::env::var("QT_QPA_PLATFORM").unwrap_or_default();
    if !launched_at_login(args) || has_display(env) || !(platform.is_empty() || platform.contains("wayland")) {
        return;
    }
    let dir = runtime_dir();
    eprintln!("cloudmus-apprun: launched at login without a display, waiting for a Wayland compositor in {}", dir.display());
    let start = Instant::now();
    loop {
        if let Some(socket) = wayland_sockets(&dir).into_iter().next() {
            eprintln!("cloudmus-apprun: found {socket} after {:.1} s", start.elapsed().as_secs_f64());
            if std::env::var_os("XDG_RUNTIME_DIR").is_none_or(|v| v.is_empty()) {
                env.push(("XDG_RUNTIME_DIR", dir.into_os_string()));
            }
            env.push(("WAYLAND_DISPLAY", socket.into()));
            return;
        }
        if start.elapsed() >= WAIT {
            eprintln!("cloudmus-apprun: no Wayland compositor after {} s, starting anyway", WAIT.as_secs());
            return;
        }
        debug!("no wayland socket in {} yet", dir.display());
        std::thread::sleep(POLL);
    }
}

/// The process's cgroup in the unified hierarchy, from /proc/self/cgroup: it
/// names the systemd units that started it, `…/app-cloudmus\x2dqt@autostart.service`
/// for an autostart entry, a terminal's scope or `session-2.scope` otherwise.
pub fn cgroup(proc_cgroup: &str) -> Option<String> {
    let path = proc_cgroup.lines().find_map(|l| l.strip_prefix("0::"))?.trim();
    (path != "/" && !path.is_empty()).then(|| path.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::os::unix::net::UnixListener;

    #[test]
    fn only_compositor_sockets_count() {
        let dir = std::env::temp_dir().join(format!("cloudmus-apprun-wayland-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        assert!(wayland_sockets(&dir).is_empty());
        fs::write(dir.join("wayland-0"), "").unwrap();
        fs::write(dir.join("wayland-1.lock"), "").unwrap();
        let _bus = UnixListener::bind(dir.join("bus")).unwrap();
        assert!(wayland_sockets(&dir).is_empty());
        let _second = UnixListener::bind(dir.join("wayland-2")).unwrap();
        let _first = UnixListener::bind(dir.join("wayland-1")).unwrap();
        assert_eq!(wayland_sockets(&dir), ["wayland-1", "wayland-2"]);
        assert!(wayland_sockets(&dir.join("missing")).is_empty());
    }

    #[test]
    fn login_launch_is_told_by_its_argument() {
        assert!(launched_at_login(&["--debug".into(), "--autostart".into()]));
        assert!(!launched_at_login(&["--debug".into()]));
    }

    #[test]
    fn cgroup_from_the_unified_hierarchy() {
        let autostart = "0::/user.slice/user-1000.slice/user@1000.service/app.slice/app-cloudmus\\x2dqt@autostart.service\n";
        assert!(cgroup(autostart).unwrap().ends_with("/app-cloudmus\\x2dqt@autostart.service"));
        let hybrid = "1:name=systemd:/user.slice/x.scope\n0::/user.slice/user-1000.slice/session-2.scope\n";
        assert_eq!(cgroup(hybrid).as_deref(), Some("/user.slice/user-1000.slice/session-2.scope"));
        assert_eq!(cgroup("0::/\n"), None);
        assert_eq!(cgroup("12:pids:/x\n"), None);
    }
}
