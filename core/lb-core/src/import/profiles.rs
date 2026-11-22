// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A Chrome-family browser's profiles, from the `Local State` file in its user data folder.

use super::{parse_json, ImportError};
use serde_json::Value;

/// One profile of the source browser.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SourceProfile {
    /// Its folder, relative to the user data folder ("Default", "Profile 1"). Empty when the
    /// browser keeps a single profile in the user data folder itself.
    pub dir: String,
    /// The name the browser shows for it; empty when it has none (the dialog then shows the
    /// browser's name alone).
    pub name: String,
}

/// The profiles listed in `Local State`, in the browser's own order (`profiles_order`, then any
/// others as the file lists them). Profiles the browser hides from its own list are left out. A
/// `Local State` without a profile list means one profile in the user data folder itself.
pub fn profiles(local_state: &str) -> Result<Vec<SourceProfile>, ImportError> {
    let root = parse_json(local_state)?;
    let Some(cache) = root.pointer("/profile/info_cache") else {
        return Ok(vec![SourceProfile {
            dir: String::new(),
            name: String::new(),
        }]);
    };
    let cache = cache
        .as_object()
        .ok_or_else(|| ImportError::Format("profile.info_cache is not an object".into()))?;

    let ordered = root
        .pointer("/profile/profiles_order")
        .and_then(Value::as_array)
        .map(|order| order.iter().filter_map(Value::as_str).collect::<Vec<_>>())
        .unwrap_or_default();
    let mut dirs: Vec<&str> = Vec::new();
    for dir in ordered.into_iter().chain(cache.keys().map(String::as_str)) {
        if cache.contains_key(dir) && !dirs.contains(&dir) {
            dirs.push(dir);
        }
    }

    Ok(dirs
        .into_iter()
        .filter(|dir| {
            let entry = &cache[*dir];
            !flag(entry, "is_omitted_from_profile_list") && !flag(entry, "is_ephemeral")
        })
        .map(|dir| SourceProfile {
            dir: dir.to_owned(),
            name: name(&cache[dir]),
        })
        .collect())
}

fn flag(entry: &Value, key: &str) -> bool {
    entry.get(key).and_then(Value::as_bool).unwrap_or(false)
}

/// The profile's own name, else the account's name, else nothing.
fn name(entry: &Value) -> String {
    ["name", "gaia_given_name", "gaia_name"]
        .iter()
        .filter_map(|key| entry.get(*key).and_then(Value::as_str))
        .map(str::trim)
        .find(|n| !n.is_empty())
        .unwrap_or_default()
        .to_owned()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn p(dir: &str, name: &str) -> SourceProfile {
        SourceProfile {
            dir: dir.into(),
            name: name.into(),
        }
    }

    #[test]
    fn lists_profiles_in_the_browsers_order() {
        let local_state = r#"{"profile": {
            "info_cache": {
                "Default": {"name": "Personal"},
                "Profile 1": {"name": "Work"},
                "Profile 2": {"name": "Side project"}
            },
            "profiles_order": ["Profile 1", "Default", "Profile 1", "Gone"]
        }}"#;
        assert_eq!(
            profiles(local_state).unwrap(),
            [
                p("Profile 1", "Work"),
                p("Default", "Personal"),
                p("Profile 2", "Side project")
            ]
        );
    }

    #[test]
    fn without_an_order_keeps_the_files_order() {
        let local_state = r#"{"profile": {"info_cache": {
            "Profile 3": {"name": "B"}, "Default": {"name": "A"}}}}"#;
        assert_eq!(
            profiles(local_state).unwrap(),
            [p("Profile 3", "B"), p("Default", "A")]
        );
    }

    #[test]
    fn hidden_profiles_are_left_out() {
        let local_state = r#"{"profile": {"info_cache": {
            "Default": {"name": "Me"},
            "Guest Profile": {"name": "Guest", "is_ephemeral": true},
            "System Profile": {"name": "System", "is_omitted_from_profile_list": true}}}}"#;
        assert_eq!(profiles(local_state).unwrap(), [p("Default", "Me")]);
    }

    #[test]
    fn names_fall_back_to_the_account() {
        let local_state = r#"{"profile": {"info_cache": {
            "Default": {"name": "  ", "gaia_given_name": "Ana", "gaia_name": "Ana Lee"},
            "Profile 1": {"gaia_name": "Bo"},
            "Profile 2": {"name": 5}}}}"#;
        assert_eq!(
            profiles(local_state).unwrap(),
            [
                p("Default", "Ana"),
                p("Profile 1", "Bo"),
                p("Profile 2", "")
            ]
        );
    }

    #[test]
    fn no_profile_list_means_one_profile_in_the_user_data_folder() {
        assert_eq!(profiles(r#"{"browser": {}}"#).unwrap(), [p("", "")]);
    }

    #[test]
    fn bad_files_are_errors() {
        assert!(matches!(profiles("nope"), Err(ImportError::Json(_))));
        assert_eq!(
            profiles(r#"{"profile": {"info_cache": []}}"#),
            Err(ImportError::Format(
                "profile.info_cache is not an object".into()
            ))
        );
    }
}
