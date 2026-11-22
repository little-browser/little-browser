// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The browsers we import from with our own readers: Chrome-family browsers and Arc
//! (docs/ux.md §13). Firefox and Safari are found by Chromium's own importer list.

/// How a browser's data is read.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SourceKind {
    /// Chromium's profile layout: `Local State` and one folder per profile.
    Chromium,
    /// Chromium's profile layout, plus Arc's sidebar file for spaces and tabs.
    Arc,
}

/// One browser and where it keeps its data.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Browser {
    /// Our id for it, stable across releases.
    pub id: &'static str,
    /// Its name, as the import dialog shows it.
    pub name: &'static str,
    pub kind: SourceKind,
    /// macOS: its bundle id, to tell whether it's the default browser.
    pub mac_bundle_id: &'static str,
    /// macOS: its user data folder, under `~/Library/Application Support`.
    pub mac_user_data: &'static str,
    /// macOS: the keychain item holding the key its cookies are encrypted with.
    pub mac_keychain_service: &'static str,
    pub mac_keychain_account: &'static str,
}

/// Arc's sidebar file (spaces, pinned and unpinned tabs, Favourites), under
/// `~/Library/Application Support`.
pub const ARC_SIDEBAR_FILE: &str = "Arc/StorableSidebar.json";

/// Every browser we read, by name.
pub const BROWSERS: &[Browser] = &[
    Browser {
        id: "arc",
        name: "Arc",
        kind: SourceKind::Arc,
        mac_bundle_id: "company.thebrowser.Browser",
        mac_user_data: "Arc/User Data",
        mac_keychain_service: "Arc Safe Storage",
        mac_keychain_account: "Arc",
    },
    Browser {
        id: "brave",
        name: "Brave",
        kind: SourceKind::Chromium,
        mac_bundle_id: "com.brave.Browser",
        mac_user_data: "BraveSoftware/Brave-Browser",
        mac_keychain_service: "Brave Safe Storage",
        mac_keychain_account: "Brave",
    },
    Browser {
        id: "chrome",
        name: "Chrome",
        kind: SourceKind::Chromium,
        mac_bundle_id: "com.google.Chrome",
        mac_user_data: "Google/Chrome",
        mac_keychain_service: "Chrome Safe Storage",
        mac_keychain_account: "Chrome",
    },
    Browser {
        id: "chromium",
        name: "Chromium",
        kind: SourceKind::Chromium,
        mac_bundle_id: "org.chromium.Chromium",
        mac_user_data: "Chromium",
        mac_keychain_service: "Chromium Safe Storage",
        mac_keychain_account: "Chromium",
    },
    Browser {
        id: "dia",
        name: "Dia",
        kind: SourceKind::Chromium,
        mac_bundle_id: "company.thebrowser.dia",
        mac_user_data: "Dia/User Data",
        mac_keychain_service: "Dia Safe Storage",
        mac_keychain_account: "Dia",
    },
    Browser {
        id: "edge",
        name: "Edge",
        kind: SourceKind::Chromium,
        mac_bundle_id: "com.microsoft.edgemac",
        mac_user_data: "Microsoft Edge",
        mac_keychain_service: "Microsoft Edge Safe Storage",
        mac_keychain_account: "Microsoft Edge",
    },
    Browser {
        id: "opera",
        name: "Opera",
        kind: SourceKind::Chromium,
        mac_bundle_id: "com.operasoftware.Opera",
        mac_user_data: "com.operasoftware.Opera",
        mac_keychain_service: "Opera Safe Storage",
        mac_keychain_account: "Opera",
    },
    Browser {
        id: "vivaldi",
        name: "Vivaldi",
        kind: SourceKind::Chromium,
        mac_bundle_id: "com.vivaldi.Vivaldi",
        mac_user_data: "Vivaldi",
        mac_keychain_service: "Vivaldi Safe Storage",
        mac_keychain_account: "Vivaldi",
    },
];

/// The browser with our id `id`.
pub fn browser(id: &str) -> Option<&'static Browser> {
    BROWSERS.iter().find(|b| b.id == id)
}

/// The order the import dialog lists the browsers in: the default browser first (matched by its
/// macOS bundle id, ignoring case), then the rest in `found`'s order, which is by name.
pub fn dialog_order<'a>(found: &[&'a str], default_bundle_id: Option<&str>) -> Vec<&'a str> {
    let entries: Vec<(&str, &str)> = found
        .iter()
        .map(|id| (*id, browser(id).map_or("", |b| b.mac_bundle_id)))
        .collect();
    default_first(&entries, default_bundle_id)
}

/// Firefox and Safari, which Chromium's own importers read: their macOS bundle ids, so they can
/// come first too when one of them is the default browser.
pub const FIREFOX_BUNDLE_ID: &str = "org.mozilla.firefox";
pub const SAFARI_BUNDLE_ID: &str = "com.apple.Safari";

/// The ids of `entries` (id, macOS bundle id) with those of the default browser first (bundle
/// ids compare ignoring case), the rest in their given order.
pub fn default_first<'a>(
    entries: &[(&'a str, &str)],
    default_bundle_id: Option<&str>,
) -> Vec<&'a str> {
    let is_default = |bundle: &str| {
        default_bundle_id.is_some_and(|d| !bundle.is_empty() && bundle.eq_ignore_ascii_case(d))
    };
    let mut ordered: Vec<&str> = entries
        .iter()
        .filter(|(_, bundle)| is_default(bundle))
        .map(|(id, _)| *id)
        .collect();
    ordered.extend(
        entries
            .iter()
            .filter(|(_, bundle)| !is_default(bundle))
            .map(|(id, _)| *id),
    );
    ordered
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn browsers_are_listed_by_name_with_unique_ids() {
        let names: Vec<&str> = BROWSERS.iter().map(|b| b.name).collect();
        let mut sorted = names.clone();
        sorted.sort();
        assert_eq!(names, sorted);
        for b in BROWSERS {
            assert_eq!(browser(b.id), Some(b));
            assert!(
                b.mac_keychain_service.ends_with(" Safe Storage"),
                "{}",
                b.id
            );
        }
        assert_eq!(browser("netscape"), None);
    }

    #[test]
    fn only_arc_has_a_sidebar() {
        let arcs: Vec<&str> = BROWSERS
            .iter()
            .filter(|b| b.kind == SourceKind::Arc)
            .map(|b| b.id)
            .collect();
        assert_eq!(arcs, ["arc"]);
        assert!(ARC_SIDEBAR_FILE.starts_with("Arc/"));
    }

    #[test]
    fn default_browser_comes_first() {
        let found = ["arc", "chrome", "dia"];
        assert_eq!(
            dialog_order(&found, Some("COM.GOOGLE.CHROME")),
            ["chrome", "arc", "dia"]
        );
        assert_eq!(
            dialog_order(&found, Some("org.mozilla.firefox")),
            ["arc", "chrome", "dia"]
        );
        assert_eq!(dialog_order(&found, None), ["arc", "chrome", "dia"]);
        assert_eq!(
            dialog_order(&["unknown", "dia"], Some("company.thebrowser.dia")),
            ["dia", "unknown"]
        );
        assert!(dialog_order(&[], Some("com.google.Chrome")).is_empty());
    }

    #[test]
    fn firefox_or_safari_as_default_come_first_too() {
        let entries = [
            ("arc", "company.thebrowser.Browser"),
            ("firefox:default", FIREFOX_BUNDLE_ID),
            ("safari", SAFARI_BUNDLE_ID),
            ("file", ""),
        ];
        assert_eq!(
            default_first(&entries, Some("COM.APPLE.SAFARI")),
            ["safari", "arc", "firefox:default", "file"]
        );
        assert_eq!(
            default_first(&entries, Some("org.mozilla.firefox")),
            ["firefox:default", "arc", "safari", "file"]
        );
        assert_eq!(
            default_first(&entries, Some("")),
            ["arc", "firefox:default", "safari", "file"]
        );
        assert_eq!(
            default_first(&entries, None),
            ["arc", "firefox:default", "safari", "file"]
        );
    }
}
