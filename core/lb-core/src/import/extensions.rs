// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A profile's extensions, from `extensions.settings` in its `Preferences` and
//! `Secure Preferences` files (macOS keeps them in the second). Only Chrome Web Store extensions
//! can be installed again (docs/ux.md §13).

use super::{parse_json, ImportError};
use serde_json::{Map, Value};

/// An extension to offer for installing from the Chrome Web Store.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StoreExtension {
    pub id: String,
    /// Its name from its manifest; empty when the manifest names a message instead (see
    /// `name_message`) or has no name.
    pub name: String,
    /// The message key its name is in (`appName` for `__MSG_appName__`): the shell looks it up
    /// with [`localized_message`] in `Extensions/<path>/_locales/<default_locale>/messages.json`.
    pub name_message: Option<String>,
    pub default_locale: Option<String>,
    /// Its folder under the profile's `Extensions` folder (`<id>/<version>`), if known.
    pub path: Option<String>,
    /// False when it was turned off in the source browser, by the user or by the browser (as for
    /// an update asking for more permissions): the dialog then offers it unticked.
    pub enabled: bool,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ExtensionList {
    /// Store extensions not installed here yet, by name (unnamed ones last, by id).
    pub store: Vec<StoreExtension>,
    /// The ids of extensions the user installed some other way, which can't come along, sorted.
    pub not_from_store: Vec<String>,
}

/// `Extension::FROM_WEBSTORE` in Chromium's creation flags.
const FROM_WEBSTORE: i64 = 1 << 3;
/// `ManifestLocation::kInternal`: installed by the user.
const LOCATION_INTERNAL: i64 = 1;
/// `ManifestLocation::kUnpacked`: loaded from a folder (developer mode).
const LOCATION_UNPACKED: i64 = 4;
/// `ManifestLocation::kExternalPrefDownload`: another program asked for it from the store.
const LOCATION_EXTERNAL_PREF_DOWNLOAD: i64 = 6;
/// `ManifestLocation::kCommandLine`.
const LOCATION_COMMAND_LINE: i64 = 8;

/// The extensions in `preferences` and `secure_preferences` (either may be missing: pass
/// `None`), leaving out those whose id is in `installed`. Extensions the browser installed by
/// itself, components and ones set by policy are left out altogether.
pub fn extensions(
    preferences: Option<&str>,
    secure_preferences: Option<&str>,
    installed: &[&str],
) -> Result<ExtensionList, ImportError> {
    let mut settings: Map<String, Value> = Map::new();
    for text in [preferences, secure_preferences].into_iter().flatten() {
        let root = parse_json(text)?;
        let Some(found) = root
            .pointer("/extensions/settings")
            .and_then(Value::as_object)
        else {
            continue;
        };
        for (id, entry) in found {
            let Some(entry) = entry.as_object() else {
                continue;
            };
            let merged = settings
                .entry(id.clone())
                .or_insert_with(|| Value::Object(Map::new()));
            if let Some(merged) = merged.as_object_mut() {
                merged.extend(entry.clone());
            }
        }
    }

    let mut list = ExtensionList::default();
    for (id, entry) in &settings {
        let int = |key: &str| entry.get(key).and_then(Value::as_i64).unwrap_or(0);
        let flag = |key: &str| entry.get(key).and_then(Value::as_bool).unwrap_or(false);
        let location = int("location");
        if flag("was_installed_by_default") || flag("was_installed_by_oem") || !is_extension_id(id)
        {
            continue;
        }
        if installed.contains(&id.as_str()) {
            continue;
        }
        let from_store = int("creation_flags") & FROM_WEBSTORE != 0 || flag("from_webstore");
        match location {
            LOCATION_INTERNAL | LOCATION_EXTERNAL_PREF_DOWNLOAD if from_store => {
                list.store.push(store_extension(id, entry))
            }
            LOCATION_INTERNAL | LOCATION_UNPACKED | LOCATION_COMMAND_LINE => {
                list.not_from_store.push(id.clone())
            }
            _ => {}
        }
    }
    sort_by_name(&mut list.store);
    list.not_from_store.sort();
    Ok(list)
}

/// Whether an extension's settings say it is on. Chromium keeps why it is off in
/// `disable_reasons`, a list of reasons (an older bitmask int), and once kept `state`: 0 for off.
fn is_enabled(entry: &Value) -> bool {
    let off_for_reasons = match entry.get("disable_reasons") {
        Some(Value::Array(reasons)) => !reasons.is_empty(),
        Some(Value::Number(bits)) => bits.as_i64().unwrap_or(0) != 0,
        _ => false,
    };
    let off_by_state = entry.get("state").and_then(Value::as_i64) == Some(0);
    !off_for_reasons && !off_by_state
}

/// Sorts extensions by name, ignoring case; unnamed ones last, by id. The shell sorts again
/// once it has read the names an extension keeps in its messages.
pub fn sort_by_name(list: &mut [StoreExtension]) {
    list.sort_by(|a, b| {
        (a.name.is_empty(), a.name.to_lowercase(), &a.id).cmp(&(
            b.name.is_empty(),
            b.name.to_lowercase(),
            &b.id,
        ))
    });
}

fn store_extension(id: &str, entry: &Value) -> StoreExtension {
    let manifest_str = |key: &str| {
        entry
            .pointer(&format!("/manifest/{key}"))
            .and_then(Value::as_str)
            .map(str::trim)
            .filter(|s| !s.is_empty())
    };
    let raw_name = manifest_str("name").unwrap_or_default();
    let name_message = raw_name
        .strip_prefix("__MSG_")
        .and_then(|rest| rest.strip_suffix("__"))
        .filter(|key| !key.is_empty())
        .map(str::to_owned);
    StoreExtension {
        id: id.to_owned(),
        name: if name_message.is_some() { "" } else { raw_name }.to_owned(),
        name_message,
        default_locale: manifest_str("default_locale").map(str::to_owned),
        path: entry
            .get("path")
            .and_then(Value::as_str)
            .filter(|p| !p.is_empty())
            .map(str::to_owned),
        enabled: is_enabled(entry),
    }
}

/// `text` without a leading byte-order mark and without `//` and `/* */` comments outside
/// strings.
fn without_comments(text: &str) -> String {
    let text = text.strip_prefix('\u{feff}').unwrap_or(text);
    let mut out = String::with_capacity(text.len());
    let mut chars = text.chars().peekable();
    let mut in_string = false;
    while let Some(c) = chars.next() {
        if in_string {
            out.push(c);
            if c == '\\' {
                if let Some(escaped) = chars.next() {
                    out.push(escaped);
                }
            } else if c == '"' {
                in_string = false;
            }
            continue;
        }
        match (c, chars.peek()) {
            ('"', _) => {
                in_string = true;
                out.push(c);
            }
            ('/', Some('/')) => {
                for skipped in chars.by_ref() {
                    if skipped == '\n' {
                        out.push('\n');
                        break;
                    }
                }
            }
            ('/', Some('*')) => {
                chars.next();
                let mut last = ' ';
                for skipped in chars.by_ref() {
                    if last == '*' && skipped == '/' {
                        break;
                    }
                    last = skipped;
                }
                out.push(' ');
            }
            _ => out.push(c),
        }
    }
    out
}

/// Whether `id` is an extension id: 32 letters from `a` to `p`.
pub fn is_extension_id(id: &str) -> bool {
    id.len() == 32 && id.bytes().all(|b| (b'a'..=b'p').contains(&b))
}

/// The message `key` in an extension's `messages.json` (keys ignore case), if it has one. The
/// file is read as Chrome reads it: a leading byte-order mark and comments are allowed.
pub fn localized_message(messages_json: &str, key: &str) -> Option<String> {
    let root = parse_json(&without_comments(messages_json)).ok()?;
    root.as_object()?
        .iter()
        .find(|(k, _)| k.eq_ignore_ascii_case(key))
        .and_then(|(_, v)| v.get("message"))
        .and_then(Value::as_str)
        .map(str::trim)
        .filter(|m| !m.is_empty())
        .map(str::to_owned)
}

#[cfg(test)]
mod tests {
    use super::*;

    const A: &str = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const B: &str = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    const C: &str = "cccccccccccccccccccccccccccccccc";
    const D: &str = "dddddddddddddddddddddddddddddddd";

    fn prefs(settings: &str) -> String {
        format!(r#"{{"extensions": {{"settings": {settings}}}}}"#)
    }

    #[test]
    fn store_extensions_by_name() {
        let secure = prefs(&format!(
            r#"{{
            "{A}": {{"location": 1, "creation_flags": 9, "path": "{A}/1.0",
                     "manifest": {{"name": "zeta"}}}},
            "{B}": {{"location": 6, "from_webstore": true,
                     "manifest": {{"name": "__MSG_appName__", "default_locale": "en"}}}},
            "{C}": {{"location": 1, "creation_flags": 8, "manifest": {{"name": "Alpha"}}}}
        }}"#
        ));
        let list = extensions(None, Some(&secure), &[]).unwrap();
        let ids: Vec<&str> = list.store.iter().map(|e| e.id.as_str()).collect();
        assert_eq!(ids, [C, A, B]);
        assert_eq!(list.store[1].path.as_deref(), Some(&*format!("{A}/1.0")));
        assert_eq!(
            list.store[2],
            StoreExtension {
                id: B.into(),
                name: String::new(),
                name_message: Some("appName".into()),
                default_locale: Some("en".into()),
                path: None,
                enabled: true,
            }
        );
        assert!(list.not_from_store.is_empty());
    }

    #[test]
    fn preferences_and_secure_preferences_combine() {
        let regular = prefs(&format!(
            r#"{{"{A}": {{"manifest": {{"name": "Split"}}}}, "{B}": 5}}"#
        ));
        let secure = prefs(&format!(
            r#"{{"{A}": {{"location": 1, "creation_flags": 8}}}}"#
        ));
        let list = extensions(Some(&regular), Some(&secure), &[]).unwrap();
        assert_eq!(list.store.len(), 1);
        assert_eq!(list.store[0].name, "Split");
    }

    #[test]
    fn leaves_out_installed_default_component_and_policy_extensions() {
        let secure = prefs(&format!(
            r#"{{
            "{A}": {{"location": 1, "creation_flags": 8}},
            "{B}": {{"location": 1, "creation_flags": 8, "was_installed_by_default": true}},
            "{C}": {{"location": 5}},
            "{D}": {{"location": 9, "creation_flags": 8, "was_installed_by_oem": false}},
            "not-an-id": {{"location": 1, "creation_flags": 8}}
        }}"#
        ));
        let list = extensions(None, Some(&secure), &[A]).unwrap();
        assert!(list.store.is_empty());
        assert!(list.not_from_store.is_empty());
    }

    #[test]
    fn counts_extensions_not_from_the_store() {
        let secure = prefs(&format!(
            r#"{{
            "{A}": {{"location": 4}},
            "{B}": {{"location": 1}},
            "{C}": {{"location": 8}},
            "{D}": {{"location": 6}}
        }}"#
        ));
        let list = extensions(None, Some(&secure), &[]).unwrap();
        assert!(list.store.is_empty());
        // Their ids, so the same one in two profiles can be counted once.
        assert_eq!(list.not_from_store, [A, B, C]);
    }

    #[test]
    fn extensions_turned_off_are_marked() {
        let secure = prefs(&format!(
            r#"{{
            "{A}": {{"location": 1, "creation_flags": 8, "manifest": {{"name": "On"}}}},
            "{B}": {{"location": 1, "creation_flags": 8, "disable_reasons": [1],
                     "manifest": {{"name": "Off by the user"}}}},
            "{C}": {{"location": 1, "creation_flags": 8, "disable_reasons": 4,
                     "manifest": {{"name": "Off for permissions"}}}},
            "{D}": {{"location": 1, "creation_flags": 8, "state": 0, "disable_reasons": [],
                     "manifest": {{"name": "Off, older"}}}}
        }}"#
        ));
        let list = extensions(None, Some(&secure), &[]).unwrap();
        let enabled: Vec<(&str, bool)> = list
            .store
            .iter()
            .map(|e| (e.name.as_str(), e.enabled))
            .collect();
        assert_eq!(
            enabled,
            [
                ("Off by the user", false),
                ("Off for permissions", false),
                ("Off, older", false),
                ("On", true)
            ]
        );
    }

    #[test]
    fn missing_settings_mean_no_extensions() {
        assert_eq!(
            extensions(Some("{}"), None, &[]).unwrap(),
            ExtensionList::default()
        );
        assert_eq!(
            extensions(None, None, &[]).unwrap(),
            ExtensionList::default()
        );
        assert!(matches!(
            extensions(Some("{"), None, &[]),
            Err(ImportError::Json(_))
        ));
    }

    #[test]
    fn names_without_text() {
        let secure = prefs(&format!(
            r#"{{"{A}": {{"location": 1, "creation_flags": 8, "path": "",
                         "manifest": {{"name": "__MSG___", "default_locale": " "}}}},
                "{B}": {{"location": 1, "creation_flags": 8, "manifest": {{"name": "Named"}}}}}}"#
        ));
        let list = extensions(None, Some(&secure), &[]).unwrap();
        assert_eq!(list.store[1].name, "Named");
        assert_eq!(
            list.store[0],
            StoreExtension {
                id: A.into(),
                name: "__MSG___".into(),
                name_message: None,
                default_locale: None,
                path: None,
                enabled: true,
            }
        );
    }

    #[test]
    fn sorting_by_name_after_names_are_read() {
        let named = |id: &str, name: &str| StoreExtension {
            id: id.into(),
            name: name.into(),
            name_message: None,
            default_locale: None,
            path: None,
            enabled: true,
        };
        let mut list = vec![
            named(B, "zeta"),
            named(C, ""),
            named(A, "Alpha"),
            named(D, ""),
        ];
        sort_by_name(&mut list);
        let ids: Vec<&str> = list.iter().map(|e| e.id.as_str()).collect();
        assert_eq!(ids, [A, B, C, D]);
    }

    #[test]
    fn extension_ids() {
        assert!(is_extension_id(A));
        assert!(!is_extension_id("qaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
        assert!(!is_extension_id("aaaa"));
    }

    #[test]
    fn messages_as_chrome_reads_them() {
        let json = "\u{feff}// The name\n{\"appName\": {\"message\": \"Dark // Reader\"}, /* note */\n \"x\": {\"message\": \"a\\\"b\"}}";
        assert_eq!(
            localized_message(json, "appName").as_deref(),
            Some("Dark // Reader")
        );
        assert_eq!(localized_message(json, "x").as_deref(), Some("a\"b"));
        assert_eq!(without_comments("/* unterminated"), " ");
        assert_eq!(without_comments("1 // end"), "1 ");
    }

    #[test]
    fn messages() {
        let json = r#"{"AppName": {"message": " Dark Reader "}, "empty": {"message": ""}}"#;
        assert_eq!(
            localized_message(json, "appName").as_deref(),
            Some("Dark Reader")
        );
        assert_eq!(localized_message(json, "empty"), None);
        assert_eq!(localized_message(json, "missing"), None);
        assert_eq!(localized_message("[]", "appName"), None);
        assert_eq!(localized_message("nope", "appName"), None);
    }
}
