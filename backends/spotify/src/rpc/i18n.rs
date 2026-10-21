//! Translation of the strings this backend sends a front (docs/protocol.md
//! §7.8), as in `rpc_common/i18n.py`: the English text is the key, so a
//! missing entry or an unknown language falls back to English.

use std::collections::{BTreeMap, HashMap};
use std::sync::RwLock;

pub const DEFAULT_LOCALE: &str = "en";

/// "ru_RU.UTF-8" -> "ru-RU"; anything unusable -> "".
pub fn normalize_locale(locale: &str) -> String {
    let tag = locale.split('.').next().unwrap_or("").split('@').next().unwrap_or("").replace('_', "-");
    let parts: Vec<&str> = tag.trim().split('-').filter(|p| !p.is_empty()).collect();
    let Some(first) = parts.first() else { return String::new() };
    if !first.chars().all(char::is_alphabetic) {
        return String::new();
    }
    let mut out = vec![first.to_lowercase()];
    for part in &parts[1..] {
        out.push(if part.chars().count() == 2 {
            part.to_uppercase()
        } else {
            let mut chars = part.chars();
            match chars.next() {
                Some(c) => c.to_uppercase().chain(chars.flat_map(char::to_lowercase)).collect(),
                None => String::new(),
            }
        });
    }
    out.join("-")
}

pub struct Translator {
    catalogs: BTreeMap<String, HashMap<String, String>>,
    current: RwLock<String>,
}

impl Translator {
    pub fn new(catalogs: BTreeMap<String, HashMap<String, String>>) -> Self {
        let catalogs = catalogs.into_iter().map(|(lang, c)| (normalize_locale(&lang), c)).collect();
        Translator { catalogs, current: RwLock::new(DEFAULT_LOCALE.to_owned()) }
    }

    /// Languages this backend answers in, English included.
    pub fn locales(&self) -> Vec<String> {
        std::iter::once(DEFAULT_LOCALE.to_owned()).chain(self.catalogs.keys().cloned()).collect()
    }

    /// The language in effect: a catalog's key, or "en".
    pub fn locale(&self) -> String {
        self.current.read().unwrap().clone()
    }

    /// Picks the best catalog ("pt-BR" tries "pt-BR", then "pt") and
    /// returns the language now in effect.
    pub fn set_locale(&self, locale: Option<&str>) -> String {
        let tag = normalize_locale(locale.unwrap_or(""));
        let base = tag.split('-').next().unwrap_or("").to_owned();
        let applied = [tag, base]
            .into_iter()
            .find(|c| self.catalogs.contains_key(c))
            .unwrap_or_else(|| DEFAULT_LOCALE.to_owned());
        *self.current.write().unwrap() = applied.clone();
        applied
    }

    pub fn tr(&self, text: &str) -> String {
        let current = self.current.read().unwrap();
        self.catalogs.get(current.as_str()).and_then(|c| c.get(text)).cloned().unwrap_or_else(|| text.to_owned())
    }

    /// `tr`, then each `{name}` replaced by its value.
    pub fn trf(&self, text: &str, args: &[(&str, &dyn std::fmt::Display)]) -> String {
        let mut out = self.tr(text);
        for (name, value) in args {
            out = out.replace(&format!("{{{name}}}"), &value.to_string());
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn translator() -> Translator {
        let ru = HashMap::from([("Quality".to_owned(), "Качество".to_owned())]);
        Translator::new(BTreeMap::from([("ru".to_owned(), ru)]))
    }

    #[test]
    fn normalizes_locales() {
        assert_eq!(normalize_locale("ru_RU.UTF-8"), "ru-RU");
        assert_eq!(normalize_locale("zh-hant-tw"), "zh-Hant-TW");
        assert_eq!(normalize_locale("C"), "c");
        assert_eq!(normalize_locale("1x"), "");
    }

    #[test]
    fn falls_back_to_base_language_and_english() {
        let t = translator();
        assert_eq!(t.set_locale(Some("ru-RU")), "ru");
        assert_eq!(t.tr("Quality"), "Качество");
        assert_eq!(t.tr("Missing"), "Missing");
        assert_eq!(t.set_locale(Some("xx-YY")), "en");
        assert_eq!(t.tr("Quality"), "Quality");
        assert_eq!(t.locales(), vec!["en", "ru"]);
    }

    #[test]
    fn formats_placeholders() {
        let t = translator();
        assert_eq!(t.trf("must be at least {n}", &[("n", &3)]), "must be at least 3");
    }
}
