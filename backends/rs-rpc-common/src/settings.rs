//! A backend's settings (docs/protocol.md §7.7), as in
//! `rpc_common/settings.py`, for the field types Rust backends use so far
//! (enum, path); another is a variant of `Kind` away. Values live under
//! "settings" in config.json; only those that differ from their default
//! are written, so a changed default reaches everyone who never picked one.
//!
//! The messages it sends ("expected text", "no such folder", ...) are
//! translated through the backend's own catalogs, which should carry them.

use std::path::{Path, PathBuf};
use std::sync::Arc;

use serde_json::{json, Map, Value};

use crate::errors::{RpcError, INTERNAL_ERROR, INVALID_PARAMS};
use crate::generated::models::{SettingField, SettingOption, SettingsDescription, SettingsGroup};
use crate::i18n::Translator;
use crate::server::Server;

const SETTINGS_KEY: &str = "settings";

pub struct Group {
    pub id: &'static str,
    pub title: &'static str,
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum PathKind {
    Directory,
    File,
}

pub enum Kind {
    /// (value, English label) pairs.
    Enum(&'static [(&'static str, &'static str)]),
    /// Text naming a folder or a file; empty for none. `~` stands for home.
    Path(PathKind),
}

pub struct Field {
    pub key: &'static str,
    pub label: &'static str,
    pub kind: Kind,
    pub default: Value,
    pub description: Option<&'static str>,
    pub group: Option<&'static str>,
}

/// `~` and `~/...` as the user's home; anything else as is.
pub fn expand_home(path: &str) -> PathBuf {
    let home = || std::env::var_os(if cfg!(windows) { "USERPROFILE" } else { "HOME" }).map(PathBuf::from);
    match path.strip_prefix('~') {
        Some(rest) if rest.is_empty() || rest.starts_with(['/', '\\']) => match home() {
            Some(home) => home.join(rest.trim_start_matches(['/', '\\'])),
            None => PathBuf::from(path),
        },
        _ => PathBuf::from(path),
    }
}

impl Field {
    /// None if acceptable, else a message for the user. `check_path`: whether
    /// a path must point at something — not when reading back what was once
    /// accepted, as a folder on a drive that isn't mounted right now is still
    /// the user's choice.
    fn validate(&self, value: &Value, check_path: bool, tr: &Translator) -> Option<String> {
        let Some(text) = value.as_str() else {
            return Some(tr.tr("expected text"));
        };
        match &self.kind {
            Kind::Enum(options) if options.iter().any(|(o, _)| *o == text) => None,
            Kind::Enum(options) => {
                let list = options.iter().map(|(o, _)| *o).collect::<Vec<_>>().join(", ");
                Some(tr.trf("must be one of: {options}", &[("options", &list)]))
            }
            Kind::Path(_) if text.is_empty() || !check_path => None,
            Kind::Path(PathKind::Directory) => (!expand_home(text).is_dir()).then(|| tr.tr("no such folder")),
            Kind::Path(PathKind::File) => (!expand_home(text).is_file()).then(|| tr.tr("no such file")),
        }
    }

    fn type_name(&self) -> &'static str {
        match self.kind {
            Kind::Enum(_) => "enum",
            Kind::Path(_) => "path",
        }
    }
}

pub struct SettingsStore {
    config_file: PathBuf,
    groups: Vec<Group>,
    fields: Vec<Field>,
    translator: Arc<Translator>,
}

impl SettingsStore {
    pub fn new(config_file: PathBuf, groups: Vec<Group>, fields: Vec<Field>, translator: Arc<Translator>) -> Self {
        SettingsStore { config_file, groups, fields, translator }
    }

    fn load_config(&self) -> Map<String, Value> {
        std::fs::read_to_string(&self.config_file)
            .ok()
            .and_then(|text| serde_json::from_str::<Value>(&text).ok())
            .and_then(|v| v.as_object().cloned())
            .unwrap_or_default()
    }

    fn stored(&self) -> Map<String, Value> {
        self.load_config().get(SETTINGS_KEY).and_then(Value::as_object).cloned().unwrap_or_default()
    }

    fn field(&self, key: &str) -> Option<&Field> {
        self.fields.iter().find(|f| f.key == key)
    }

    /// The stored value, or the default when there's none or it no longer
    /// validates (an option since removed, a hand edit).
    pub fn get(&self, key: &str) -> Value {
        let field = self.field(key).expect("known setting");
        match self.stored().get(key) {
            Some(v) if field.validate(v, false, &self.translator).is_none() => v.clone(),
            _ => field.default.clone(),
        }
    }

    pub fn get_str(&self, key: &str) -> String {
        self.get(key).as_str().unwrap_or_default().to_owned()
    }

    pub fn describe(&self) -> SettingsDescription {
        let tr = |s: &str| self.translator.tr(s);
        SettingsDescription {
            groups: self
                .groups
                .iter()
                .map(|g| SettingsGroup { id: g.id.into(), title: tr(g.title), ..Default::default() })
                .collect(),
            fields: self
                .fields
                .iter()
                .map(|f| SettingField {
                    key: f.key.into(),
                    group: f.group.map(Into::into),
                    label: tr(f.label),
                    description: f.description.map(tr),
                    type_: f.type_name().into(),
                    default: f.default.clone(),
                    value: self.get(f.key),
                    options: match &f.kind {
                        Kind::Enum(options) => Some(
                            options
                                .iter()
                                .map(|(value, label)| SettingOption { value: (*value).into(), label: tr(label) })
                                .collect(),
                        ),
                        Kind::Path(_) => None,
                    },
                    path_kind: match &f.kind {
                        Kind::Path(PathKind::Directory) => Some("directory".into()),
                        Kind::Path(PathKind::File) => Some("file".into()),
                        Kind::Enum(_) => None,
                    },
                    ..Default::default()
                })
                .collect(),
        }
    }

    /// Validates every value, then saves them all — or fails naming the
    /// first bad key and saves nothing.
    pub fn update(&self, values: &Map<String, Value>) -> Result<(), RpcError> {
        for (key, value) in values {
            let Some(field) = self.field(key) else {
                return Err(RpcError::new(
                    INVALID_PARAMS,
                    self.translator.trf("unknown setting: {key}", &[("key", key)]),
                )
                .with_data(json!({"key": key, "message": self.translator.tr("unknown setting")})));
            };
            if let Some(problem) = field.validate(value, true, &self.translator) {
                return Err(RpcError::new(INVALID_PARAMS, format!("{}: {problem}", self.translator.tr(field.label)))
                    .with_data(json!({"key": key, "message": problem})));
            }
        }
        let mut config = self.load_config();
        let mut stored = self.stored();
        for (key, value) in values {
            if *value == self.field(key).expect("validated").default {
                stored.remove(key);
            } else {
                stored.insert(key.clone(), value.clone());
            }
        }
        config.insert(SETTINGS_KEY.into(), Value::Object(stored));
        crate::paths::write_private(&self.config_file, &serde_json::to_string_pretty(&config).unwrap())
            .map_err(|e| RpcError::new(INTERNAL_ERROR, format!("saving settings: {e}")))
    }

    /// The config file this store keeps its section in.
    pub fn config_file(&self) -> &Path {
        &self.config_file
    }

    /// Answers `settings.describe` and `settings.update` on `server` from
    /// this store, and declares the capability.
    pub fn register(self: Arc<Self>, server: &mut Server) {
        server.capabilities_mut()["settings"] = json!(true);
        let store = self.clone();
        server.method("settings.describe", move |_, _| {
            let store = store.clone();
            async move { crate::errors::to_result(store.describe()) }
        });
        server.method("settings.update", move |params, _| {
            let store = self.clone();
            async move {
                let Some(values) = params.get("values").and_then(Value::as_object) else {
                    return Err(RpcError::new(INVALID_PARAMS, store.translator.tr("values: expected an object")));
                };
                store.update(values)?;
                Ok(json!({}))
            }
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;

    fn store(config_file: PathBuf) -> SettingsStore {
        SettingsStore::new(
            config_file,
            vec![Group { id: "library", title: "Library" }],
            vec![
                Field {
                    key: "quality",
                    label: "Quality",
                    kind: Kind::Enum(&[("high", "High"), ("low", "Low")]),
                    default: json!("high"),
                    description: None,
                    group: None,
                },
                Field {
                    key: "musicDir",
                    label: "Music folder",
                    kind: Kind::Path(PathKind::Directory),
                    default: json!(""),
                    description: Some("Scanned for music"),
                    group: Some("library"),
                },
            ],
            Arc::new(Translator::new(BTreeMap::new())),
        )
    }

    fn values(value: Value) -> Map<String, Value> {
        value.as_object().unwrap().clone()
    }

    #[test]
    fn describes_enum_and_path_fields() {
        let dir = tempfile::tempdir().unwrap();
        let described = serde_json::to_value(store(dir.path().join("config.json")).describe()).unwrap();
        assert_eq!(described["groups"], json!([{"id": "library", "title": "Library"}]));
        assert_eq!(described["fields"][0]["type"], "enum");
        assert_eq!(described["fields"][0]["options"][1], json!({"value": "low", "label": "Low"}));
        assert_eq!(described["fields"][1]["type"], "path");
        assert_eq!(described["fields"][1]["pathKind"], "directory");
        assert_eq!(described["fields"][1]["value"], "");
        assert!(described["fields"][1].get("options").is_none());
    }

    #[test]
    fn saves_only_values_off_their_default_and_keeps_the_rest_of_the_file() {
        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("config.json");
        std::fs::write(&file, r#"{"other": 1, "settings": {"quality": "low"}}"#).unwrap();
        let settings = store(file.clone());
        let music = dir.path().to_str().unwrap();
        settings.update(&values(json!({"quality": "high", "musicDir": music}))).unwrap();
        let saved: Value = serde_json::from_str(&std::fs::read_to_string(&file).unwrap()).unwrap();
        assert_eq!(saved, json!({"other": 1, "settings": {"musicDir": music}}));
        assert_eq!(settings.get_str("quality"), "high");
    }

    #[test]
    fn refuses_a_missing_folder_but_keeps_one_saved_earlier() {
        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("config.json");
        let gone = dir.path().join("unplugged-drive");
        let settings = store(file.clone());
        let error = settings.update(&values(json!({"musicDir": gone.to_str().unwrap()}))).unwrap_err();
        assert_eq!(error.code, INVALID_PARAMS);
        assert_eq!(error.data.unwrap()["key"], "musicDir");
        assert!(!file.exists(), "nothing saved after a refusal");

        std::fs::write(&file, json!({"settings": {"musicDir": gone}}).to_string()).unwrap();
        assert_eq!(settings.get_str("musicDir"), gone.to_str().unwrap());
    }

    #[test]
    fn refuses_unknown_keys_and_bad_options() {
        let dir = tempfile::tempdir().unwrap();
        let settings = store(dir.path().join("config.json"));
        let unknown = settings.update(&values(json!({"nope": "x"}))).unwrap_err();
        assert_eq!(unknown.data.unwrap()["key"], "nope");
        let bad = settings.update(&values(json!({"quality": "best"}))).unwrap_err();
        assert!(bad.message.contains("high, low"), "{}", bad.message);
    }

    #[test]
    fn expands_home() {
        let home = std::env::var_os(if cfg!(windows) { "USERPROFILE" } else { "HOME" }).map(PathBuf::from).unwrap();
        assert_eq!(expand_home("~"), home);
        assert_eq!(expand_home("~/Music"), home.join("Music"));
        assert_eq!(expand_home("~other/Music"), PathBuf::from("~other/Music"));
        assert_eq!(expand_home("/srv/music"), PathBuf::from("/srv/music"));
    }
}
