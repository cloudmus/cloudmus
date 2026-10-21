//! Files of this backend, in `<config>/cloudmus/backends/spotify/`:
//!
//! - `credentials.json`: the reusable credentials Spotify hands a session
//!   after its first sign-in (username + an opaque blob, not the password,
//!   which the backend never sees). Delete it to sign out.
//! - `config.json`: the settings, under "settings".
//!
//! Both are written with mode 0600.

use std::io;
use std::path::{Path, PathBuf};

use librespot_core::authentication::Credentials;

/// Same places as `rpc_common/platform_paths.py`.
pub fn config_home() -> PathBuf {
    if cfg!(windows) {
        std::env::var_os("APPDATA").map(PathBuf::from).unwrap_or_else(|| home().join("AppData").join("Roaming"))
    } else {
        std::env::var_os("XDG_CONFIG_HOME")
            .filter(|v| !v.is_empty())
            .map(PathBuf::from)
            .unwrap_or_else(|| home().join(".config"))
    }
}

fn home() -> PathBuf {
    std::env::var_os(if cfg!(windows) { "USERPROFILE" } else { "HOME" }).map(PathBuf::from).unwrap_or_default()
}

pub fn config_dir() -> PathBuf {
    config_home().join("cloudmus").join("backends").join("spotify")
}

pub fn config_file() -> PathBuf {
    config_dir().join("config.json")
}

fn credentials_file() -> PathBuf {
    config_dir().join("credentials.json")
}

/// Writes `contents` readable by the owner only: these files hold what
/// signs in to the account.
pub fn write_private(path: &Path, contents: &str) -> io::Result<()> {
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir)?;
    }
    let tmp = path.with_extension("tmp");
    {
        let mut options = std::fs::OpenOptions::new();
        options.write(true).create(true).truncate(true);
        #[cfg(unix)]
        std::os::unix::fs::OpenOptionsExt::mode(&mut options, 0o600);
        let mut file = options.open(&tmp)?;
        io::Write::write_all(&mut file, contents.as_bytes())?;
    }
    // Replaced in one step, so a crash mid-write never leaves half a file.
    std::fs::rename(&tmp, path)
}

pub fn load_credentials() -> Option<Credentials> {
    let text = std::fs::read_to_string(credentials_file()).ok()?;
    match serde_json::from_str(&text) {
        Ok(credentials) => Some(credentials),
        Err(e) => {
            log::warn!("ignoring an unreadable credentials.json: {e}");
            None
        }
    }
}

pub fn save_credentials(credentials: &Credentials) -> io::Result<()> {
    let text = serde_json::to_string(credentials).map_err(io::Error::other)?;
    write_private(&credentials_file(), &text)
}

pub fn clear_credentials() {
    if let Err(e) = std::fs::remove_file(credentials_file()) {
        if e.kind() != io::ErrorKind::NotFound {
            log::warn!("can't remove credentials.json: {e}");
        }
    }
}
