//! This backend's translator (locales/*.json: English text -> translation),
//! shared by every module; the server switches its language.

use std::collections::{BTreeMap, HashMap};
use std::sync::{Arc, OnceLock};

use crate::rpc::i18n::Translator;

const CATALOGS: &[(&str, &str)] = &[
    ("be", include_str!("../locales/be.json")),
    ("de", include_str!("../locales/de.json")),
    ("es", include_str!("../locales/es.json")),
    ("fr", include_str!("../locales/fr.json")),
    ("it", include_str!("../locales/it.json")),
    ("ru", include_str!("../locales/ru.json")),
];

pub fn translator() -> Arc<Translator> {
    static TRANSLATOR: OnceLock<Arc<Translator>> = OnceLock::new();
    TRANSLATOR
        .get_or_init(|| {
            let catalogs: BTreeMap<String, HashMap<String, String>> = CATALOGS
                .iter()
                .map(|(lang, json)| (lang.to_string(), serde_json::from_str(json).expect("valid locale file")))
                .collect();
            Arc::new(Translator::new(catalogs))
        })
        .clone()
}

pub fn tr(text: &str) -> String {
    translator().tr(text)
}

pub fn trf(text: &str, args: &[(&str, &dyn std::fmt::Display)]) -> String {
    translator().trf(text, args)
}

#[cfg(test)]
mod tests {
    use std::collections::HashSet;

    /// Every catalog translates the same strings: one left behind would
    /// show in English to that language's users.
    #[test]
    fn catalogs_cover_the_same_keys() {
        let keys = |json: &str| -> HashSet<String> {
            serde_json::from_str::<std::collections::HashMap<String, String>>(json).unwrap().into_keys().collect()
        };
        let reference = keys(super::CATALOGS.iter().find(|(l, _)| *l == "ru").unwrap().1);
        for (lang, json) in super::CATALOGS {
            assert_eq!(keys(json), reference, "{lang}");
        }
    }
}
