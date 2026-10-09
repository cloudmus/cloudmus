//! What the AppImage has to arrange before cloudmus-qt starts: environment
//! variables for the bundled libraries, backend manifests with this run's
//! paths, and the desktop entry. Nothing here may stop the app from
//! starting: a step that fails is reported on stderr and skipped.

use serde_json::json;
use std::ffi::OsString;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

/// XDG base directories, with the spec's fallbacks.
pub struct Dirs {
    pub config: PathBuf,
    pub cache: PathBuf,
    pub data: PathBuf,
    pub state: PathBuf,
}

impl Dirs {
    pub fn from_env() -> Dirs {
        let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_default();
        let base = |var: &str, fallback: &str| match std::env::var_os(var) {
            Some(value) if Path::new(&value).is_absolute() => PathBuf::from(value),
            _ => home.join(fallback),
        };
        Dirs {
            config: base("XDG_CONFIG_HOME", ".config"),
            cache: base("XDG_CACHE_HOME", ".cache"),
            data: base("XDG_DATA_HOME", ".local/share"),
            state: base("XDG_STATE_HOME", ".local/state"),
        }
    }

    /// Where cloudmus-qt keeps logs and crash reports (`Config::stateDir()`).
    pub fn app_state(&self) -> PathBuf {
        self.state.join("cloudmus/fronts/qt")
    }

    /// `Settings::configFilePath()`.
    pub fn app_config_file(&self) -> PathBuf {
        self.config.join("cloudmus/fronts/qt/config.ini")
    }
}

pub struct Prepared {
    /// For the child only, so a user's own backend, which inherits the
    /// child's environment, is unaffected by anything but these.
    pub env: Vec<(&'static str, OsString)>,
    /// Backend manifests to remove once the app has exited.
    pub manifests: Vec<PathBuf>,
}

fn warn(what: &str, error: &dyn std::fmt::Display) {
    eprintln!("cloudmus-apprun: {what}: {error}");
}

fn unset(name: &str) -> bool {
    std::env::var_os(name).is_none_or(|v| v.is_empty())
}

pub fn prepare(appdir: &Path, dirs: &Dirs, appimage: Option<&OsString>) -> Prepared {
    let mut env: Vec<(&'static str, OsString)> = Vec::new();

    // Without this the app looks foreign on KDE/GNOME: the desktop-specific
    // Qt platform theme plugin is built against the host's Qt and can't load
    // into the Qt bundled here. xdgdesktopportal is Qt's own cross-desktop
    // answer: it reads the color scheme, accent and fonts from the running
    // xdg-desktop-portal over D-Bus. An existing choice is left alone.
    if unset("QT_QPA_PLATFORMTHEME") {
        env.push(("QT_QPA_PLATFORMTHEME", "xdgdesktopportal".into()));
    }

    // The ASan build (build-appimage.sh --asan): reports go next to the crash
    // reports, as asan.log.<pid>. Leak checking is off: Qt and the bundled
    // libraries keep plenty alive until exit on purpose, which would bury a
    // real report.
    if appdir.join("usr/share/cloudmus/asan").exists() {
        let log_dir = dirs.app_state();
        if let Err(e) = fs::create_dir_all(&log_dir) {
            warn("can't create the ASan log directory", &e);
        }
        if unset("ASAN_OPTIONS") {
            let options = format!(
                "detect_leaks=0:alloc_dealloc_mismatch=0:abort_on_error=1:log_path={}/asan.log",
                log_dir.display()
            );
            env.push(("ASAN_OPTIONS", options.into()));
        }
        eprintln!("cloudmus-qt: ASan build, reports go to {}/asan.log.<pid>", log_dir.display());
    }

    // Plasma's Wayland session doesn't export XCURSOR_THEME at all (its own
    // platform theme plugin hands the cursor theme to Qt, and that plugin
    // can't load here, see above). Without a theme name the bundled
    // QtWaylandClient falls back to a generic cursor instead of the user's.
    if unset("XCURSOR_THEME") {
        if let Some(theme) = cursor_theme(&dirs.config.join("kcminputrc"), Path::new("/usr/share/icons/breeze_cursors")) {
            env.push(("XCURSOR_THEME", theme.into()));
        }
    }

    // libavformat (mpv's network layer) links GnuTLS, built by Debian with
    // Debian's CA bundle path, which doesn't exist on every distro (openSUSE
    // has hashed symlinks instead). TLS verification then fails silently:
    // mpv reports an empty Mime-type and "No format found" for a normal
    // stream URL. AudioPlayer.cpp hands this file to mpv as --tls-ca-file;
    // it is the certifi bundle the backends already depend on.
    let certifi = appdir.join("usr/python-runtime/lib/python3.11/site-packages/certifi/cacert.pem");
    if certifi.is_file() {
        env.push(("CLOUDMUS_TLS_CA_FILE", certifi.into_os_string()));
    }

    // Earlier versions generated a youtube-dl wrapper for mpv's ytdl_hook
    // here.
    let _ = fs::remove_dir_all(dirs.cache.join("cloudmus/mpv-appimage"));
    let _ = fs::remove_dir_all(dirs.config.join("cloudmus/mpv-appimage"));

    let manifests = write_manifests(appdir, &dirs.config.join("cloudmus/backends.d"));

    if let Some(appimage) = appimage {
        install_desktop_entry(appdir, &dirs.data, appimage);
    }

    Prepared { env, manifests }
}

/// The last `cursorTheme=` in kcminputrc; else Breeze if it's installed, a
/// strong sign of Plasma. None: leave the choice to Qt.
fn cursor_theme(kcminputrc: &Path, breeze: &Path) -> Option<String> {
    if let Ok(text) = fs::read_to_string(kcminputrc) {
        let configured = text
            .lines()
            .filter_map(|line| line.strip_prefix("cursorTheme="))
            .next_back()
            .filter(|theme| !theme.is_empty());
        if let Some(theme) = configured {
            return Some(theme.to_string());
        }
    }
    breeze.is_dir().then(|| "breeze_cursors".to_string())
}

/// Manifests for the backends bundled here, with absolute paths into this
/// run's mount point (a new one every launch, so nothing fixed at build time
/// can be trusted). Only these three files are ever touched: a manifest the
/// user put in the same directory for their own backend stays as it is. They
/// are removed again on exit, or a dev build sharing the directory would be
/// pointed at a since-unmounted path.
fn write_manifests(appdir: &Path, dir: &Path) -> Vec<PathBuf> {
    if let Err(e) = fs::create_dir_all(dir) {
        warn("can't create the backend manifest directory", &e);
        return Vec::new();
    }
    // Not PATH: a user's own backend is spawned with the app's environment,
    // and its bare "python3" must stay their system Python, so the manifests
    // carry an absolute path instead.
    let python = appdir.join("usr/bin/python3");
    let mut written = Vec::new();
    for (id, module, name) in [
        ("local-folder", "cloudmus_backend_local", "Local Folder"),
        ("yandex-music", "cloudmus_backend_yandex", "Yandex Music"),
        ("youtube-music", "cloudmus_backend_ytmusic", "YouTube Music"),
    ] {
        let manifest = json!({
            "id": id,
            "name": name,
            "argv": [python.to_string_lossy(), "-m", module],
            "protocolVersion": "1.2",
            "icon": appdir.join(format!("usr/share/cloudmus/backend-icons/{id}.svg")).to_string_lossy(),
        });
        let path = dir.join(format!("{id}.json"));
        match fs::write(&path, format!("{manifest}\n")) {
            Ok(()) => written.push(path),
            Err(e) => warn(&format!("can't write {}", path.display()), &e),
        }
    }
    written
}

/// KWin resolves a window's icon by matching its app_id (set in main.cpp)
/// against an installed .desktop file's Icon=, like for any packaged app;
/// the bundled Qt predates the xdg-toplevel-icon protocol, the only way for
/// a client to hand the compositor an icon directly. Nothing installs the
/// entry on the host, so this does. $APPIMAGE (set by the AppImage runtime)
/// is the stable path to the file, unlike this run's mount; both files stay
/// valid after exit, like any installation, so they are not cleaned up.
fn install_desktop_entry(appdir: &Path, data: &Path, appimage: &OsString) {
    let icon_dir = data.join("icons/hicolor/512x512/apps");
    let apps_dir = data.join("applications");
    let result = (|| -> std::io::Result<()> {
        fs::create_dir_all(&icon_dir)?;
        fs::create_dir_all(&apps_dir)?;
        fs::copy(
            appdir.join("usr/share/icons/hicolor/512x512/apps/cloudmus-qt.png"),
            icon_dir.join("cloudmus-qt.png"),
        )?;
        let template = fs::read_to_string(appdir.join("usr/share/applications/cloudmus-qt.desktop"))?;
        fs::write(apps_dir.join("cloudmus-qt.desktop"), desktop_entry(&template, &appimage.to_string_lossy()))
    })();
    if let Err(e) = result {
        warn("can't install the desktop entry", &e);
        return;
    }
    // Best effort: KDE rescans on its own eventually, just maybe not before
    // this launch's window asks for its icon. Not every desktop has it.
    for tool in ["kbuildsycoca6", "kbuildsycoca5"] {
        let ran = Command::new(tool)
            .arg("--noincremental")
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .is_ok();
        if ran {
            break;
        }
    }
}

/// The template with its Exec= line pointing at the AppImage.
fn desktop_entry(template: &str, appimage: &str) -> String {
    let mut out = String::new();
    for line in template.lines() {
        if line.starts_with("Exec=") {
            out.push_str("Exec=");
            out.push_str(appimage);
        } else {
            out.push_str(line);
        }
        out.push('\n');
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("cloudmus-apprun-{name}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn cursor_theme_is_the_last_configured_one() {
        let dir = temp("cursor");
        let rc = dir.join("kcminputrc");
        fs::write(&rc, "[Mouse]\ncursorTheme=Old\ncursorSize=24\ncursorTheme=Bibata\n").unwrap();
        assert_eq!(cursor_theme(&rc, &dir.join("none")).as_deref(), Some("Bibata"));
    }

    #[test]
    fn cursor_theme_falls_back_to_breeze_only_when_installed() {
        let dir = temp("breeze");
        assert_eq!(cursor_theme(&dir.join("missing"), &dir.join("none")), None);
        assert_eq!(cursor_theme(&dir.join("missing"), &dir).as_deref(), Some("breeze_cursors"));
    }

    #[test]
    fn desktop_entry_gets_the_appimage_as_exec() {
        let entry = desktop_entry("[Desktop Entry]\nName=CloudMus\nExec=cloudmus-qt\nIcon=cloudmus-qt\n", "/a b/App.AppImage");
        assert_eq!(entry, "[Desktop Entry]\nName=CloudMus\nExec=/a b/App.AppImage\nIcon=cloudmus-qt\n");
    }

    #[test]
    fn manifests_use_absolute_paths_and_survive_odd_ones() {
        let dir = temp("manifests");
        let appdir = Path::new("/tmp/.mount_x\"y/AppDir");
        let written = write_manifests(appdir, &dir);
        assert_eq!(written.len(), 3);
        let manifest: serde_json::Value =
            serde_json::from_str(&fs::read_to_string(dir.join("yandex-music.json")).unwrap()).unwrap();
        assert_eq!(manifest["id"], "yandex-music");
        assert_eq!(manifest["argv"][0], "/tmp/.mount_x\"y/AppDir/usr/bin/python3");
        assert_eq!(manifest["argv"][2], "cloudmus_backend_yandex");
        assert_eq!(manifest["protocolVersion"], "1.2");
    }
}
