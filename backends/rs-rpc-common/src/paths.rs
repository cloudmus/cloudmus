//! Where a backend keeps its files: the same places as
//! `rpc_common/platform_paths.py`, so a backend rewritten from Python finds
//! what its Python version wrote.

use std::io;
use std::path::{Path, PathBuf};

fn home() -> PathBuf {
    std::env::var_os(if cfg!(windows) { "USERPROFILE" } else { "HOME" }).map(PathBuf::from).unwrap_or_default()
}

fn from_env(name: &str) -> Option<PathBuf> {
    std::env::var_os(name).filter(|value| !value.is_empty()).map(PathBuf::from)
}

pub fn config_home() -> PathBuf {
    if cfg!(windows) {
        from_env("APPDATA").unwrap_or_else(|| home().join("AppData").join("Roaming"))
    } else {
        from_env("XDG_CONFIG_HOME").unwrap_or_else(|| home().join(".config"))
    }
}

pub fn cache_home() -> PathBuf {
    if cfg!(windows) {
        from_env("LOCALAPPDATA").unwrap_or_else(|| home().join("AppData").join("Local"))
    } else {
        from_env("XDG_CACHE_HOME").unwrap_or_else(|| home().join(".cache"))
    }
}

/// `<config>/cloudmus/backends/<id>`: the backend's config.json and the like.
pub fn backend_config_dir(id: &str) -> PathBuf {
    config_home().join("cloudmus").join("backends").join(id)
}

/// `<cache>/cloudmus/backends/<id>`: what can be made again (covers, say).
pub fn backend_cache_dir(id: &str) -> PathBuf {
    cache_home().join("cloudmus").join("backends").join(id)
}

/// Writes `contents` readable by the owner only (0600 on Unix): config
/// files may hold what signs in to an account. Replaced in one step, so a
/// crash mid-write never leaves half a file.
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
    std::fs::rename(&tmp, path)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn write_private_replaces_the_file_owner_only() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("nested/config.json");
        write_private(&path, "one").unwrap();
        write_private(&path, "two").unwrap();
        assert_eq!(std::fs::read_to_string(&path).unwrap(), "two");
        assert!(!path.with_extension("tmp").exists());
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            assert_eq!(std::fs::metadata(&path).unwrap().permissions().mode() & 0o777, 0o600);
        }
    }
}
