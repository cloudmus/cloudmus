//! This backend's settings (docs/protocol.md §7.7), as in
//! `rpc_common/settings.py`, cut down to the one field type used here
//! (enum); another is a variant of `Kind` away. Values
//! live under "settings" in config.json; only those that differ from their
//! default are written, so a changed default reaches everyone who never
//! picked one.

use std::path::PathBuf;
use std::sync::Arc;

use serde_json::{json, Map, Value};

use super::errors::{RpcError, INVALID_PARAMS};
use super::i18n::Translator;
use crate::generated::models::{SettingField, SettingOption, SettingsDescription, SettingsGroup};

const SETTINGS_KEY: &str = "settings";

pub struct Group {
    pub id: &'static str,
    pub title: &'static str,
}

pub enum Kind {
    /// (value, English label) pairs.
    Enum(&'static [(&'static str, &'static str)]),
}

pub struct Field {
    pub key: &'static str,
    pub label: &'static str,
    pub kind: Kind,
    pub default: Value,
    pub description: Option<&'static str>,
    pub group: Option<&'static str>,
}

impl Field {
    /// None if acceptable, else a message for the user.
    fn validate(&self, value: &Value, tr: &Translator) -> Option<String> {
        match &self.kind {
            Kind::Enum(options) => match value.as_str() {
                None => Some(tr.tr("expected text")),
                Some(v) if options.iter().any(|(o, _)| *o == v) => None,
                Some(_) => {
                    let list = options.iter().map(|(o, _)| *o).collect::<Vec<_>>().join(", ");
                    Some(tr.trf("must be one of: {options}", &[("options", &list)]))
                }
            },
        }
    }

    fn type_name(&self) -> &'static str {
        match self.kind {
            Kind::Enum(_) => "enum",
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
            Some(v) if field.validate(v, &self.translator).is_none() => v.clone(),
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
            if let Some(problem) = field.validate(value, &self.translator) {
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
        crate::config::write_private(&self.config_file, &serde_json::to_string_pretty(&config).unwrap())
            .map_err(|e| RpcError::new(super::errors::INTERNAL_ERROR, format!("saving settings: {e}")))
    }
}
