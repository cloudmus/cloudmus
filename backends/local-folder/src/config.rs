use serde_json::{json, Map, Value};
use std::env;
use std::fs;
use std::io::{self, Write};
use std::path::{Path, PathBuf};

pub const MUSIC_ENV: &str = "CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR";
pub const DESCRIPTION: &str = "Scanned for music: each folder in it becomes a playlist. The CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR environment variable, if set, overrides it.";

fn env_path(key: &str) -> Option<PathBuf> {
    env::var_os(key)
        .filter(|value| !value.is_empty())
        .map(PathBuf::from)
}

fn home() -> PathBuf {
    env::home_dir().unwrap_or_default()
}

fn expand(path: &str) -> PathBuf {
    if path == "~" {
        home()
    } else if let Some(rest) = path.strip_prefix("~/").or_else(|| path.strip_prefix("~\\")) {
        home().join(rest)
    } else {
        PathBuf::from(path)
    }
}

pub fn config_file() -> PathBuf {
    let base = if cfg!(windows) {
        env_path("APPDATA").unwrap_or_else(|| home().join("AppData/Roaming"))
    } else {
        env_path("XDG_CONFIG_HOME").unwrap_or_else(|| home().join(".config"))
    };
    base.join("cloudmus/backends/local-folder/config.json")
}

pub fn cover_dir() -> PathBuf {
    let base = if cfg!(windows) {
        env_path("LOCALAPPDATA").unwrap_or_else(|| home().join("AppData/Local"))
    } else {
        env_path("XDG_CACHE_HOME").unwrap_or_else(|| home().join(".cache"))
    };
    base.join("cloudmus/backends/local-folder/covers")
}

#[cfg(windows)]
fn windows_music() -> Option<PathBuf> {
    use std::{ffi::OsString, os::windows::ffi::OsStringExt, ptr};
    use windows_sys::Win32::{
        System::Com::CoTaskMemFree,
        UI::Shell::{FOLDERID_Music, SHGetKnownFolderPath},
    };
    unsafe {
        let mut result = ptr::null_mut();
        if SHGetKnownFolderPath(&FOLDERID_Music, 0, ptr::null_mut(), &mut result) < 0
            || result.is_null()
        {
            return None;
        }
        let len = (0..).take_while(|&i| *result.add(i) != 0).count();
        let path = OsString::from_wide(std::slice::from_raw_parts(result, len));
        CoTaskMemFree(result.cast());
        Some(path.into())
    }
}

fn default_music() -> PathBuf {
    #[cfg(windows)]
    if let Some(path) = windows_music() {
        return path;
    }
    #[cfg(not(windows))]
    {
        let config = env_path("XDG_CONFIG_HOME").unwrap_or_else(|| home().join(".config"));
        if let Ok(contents) = fs::read_to_string(config.join("user-dirs.dirs")) {
            for value in contents
                .lines()
                .filter_map(|line| line.trim().strip_prefix("XDG_MUSIC_DIR="))
            {
                let value = value.trim().trim_matches('"');
                let path = match value.strip_prefix("$HOME/") {
                    Some(suffix) => home().join(suffix),
                    None => PathBuf::from(value),
                };
                if path.is_absolute() && path != home() {
                    return path;
                }
            }
        }
    }
    home().join("Music")
}

fn load(path: &Path) -> io::Result<Map<String, Value>> {
    match fs::read(path) {
        Ok(bytes) => Ok(serde_json::from_slice(&bytes)?),
        Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(Map::new()),
        Err(error) => Err(error),
    }
}

fn configured_music(config: &Map<String, Value>) -> Option<&str> {
    config
        .get("settings")
        .and_then(|settings| settings.get("musicDir"))
        .or_else(|| config.get("musicDir"))
        .and_then(Value::as_str)
}

pub fn android_index() -> Option<PathBuf> {
    env_path("CLOUDMUS_ANDROID_MUSIC_INDEX")
}

pub fn music_root() -> Option<PathBuf> {
    if let Some(value) = env_path(MUSIC_ENV) {
        return Some(expand(&value.to_string_lossy()));
    }
    let config = load(&config_file()).unwrap_or_default();
    if let Some(path) = configured_music(&config).filter(|path| !path.is_empty()) {
        return Some(expand(path));
    }
    android_index().is_none().then(default_music)
}

pub fn describe(tr: impl Fn(&str) -> String) -> Value {
    let android = android_index().is_some();
    let default = if android {
        String::new()
    } else {
        default_music().to_string_lossy().into_owned()
    };
    let config = load(&config_file()).unwrap_or_default();
    let label = if android {
        "Additional music folder"
    } else {
        "Music folder"
    };
    json!({
        "groups": [{"id": "library", "title": tr("Library")}],
        "fields": [{
            "key": "musicDir", "type": "path", "pathKind": "directory", "group": "library",
            "label": tr(label), "description": tr(if android { "Enter an absolute path to a readable folder." } else { DESCRIPTION }),
            "default": default, "value": configured_music(&config).unwrap_or(&default)
        }]
    })
}

pub fn update(
    values: &Map<String, Value>,
    tr: impl Fn(&str) -> String,
) -> Result<(), (String, String)> {
    let android = android_index().is_some();
    for (key, value) in values {
        if key != "musicDir" {
            return Err((key.clone(), tr("unknown setting")));
        }
        let text = value
            .as_str()
            .ok_or_else(|| (key.clone(), tr("expected text")))?;
        if !text.is_empty() {
            let path = expand(text);
            if android && !path.is_absolute() {
                return Err((
                    key.clone(),
                    tr("Enter an absolute path to a readable folder."),
                ));
            }
            if android {
                fs::read_dir(&path).map_err(|error| {
                    eprintln!("local-folder: read_dir {}: {error}", path.display());
                    (key.clone(), format!("{}: {error}", path.display()))
                })?;
            } else if !path.is_dir() {
                return Err((key.clone(), tr("no such folder")));
            }
        }
    }

    let path = config_file();
    let parent = path.parent().expect("config path has a parent");
    let mut config = load(&path).map_err(|error| ("musicDir".into(), error.to_string()))?;
    let mut settings = config
        .remove("settings")
        .and_then(|v| v.as_object().cloned())
        .unwrap_or_default();
    if let Some(legacy) = config.remove("musicDir") {
        settings.entry("musicDir").or_insert(legacy);
    }
    if let Some(value) = values.get("musicDir") {
        let default = if android {
            PathBuf::new()
        } else {
            default_music()
        };
        if value.as_str() == default.to_str() {
            settings.remove("musicDir");
        } else {
            settings.insert("musicDir".into(), value.clone());
        }
    }
    config.insert("settings".into(), Value::Object(settings));
    let write = || -> io::Result<()> {
        fs::create_dir_all(parent)?;
        let mut file = tempfile::NamedTempFile::new_in(parent)?;
        serde_json::to_writer_pretty(&mut file, &config)?;
        file.write_all(b"\n")?;
        file.persist(&path)?;
        Ok(())
    };
    write().map_err(|error| ("musicDir".into(), error.to_string()))
}
