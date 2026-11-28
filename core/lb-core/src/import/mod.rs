// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Import from other browsers (docs/ux.md §13, docs/design.md §7.6).
//!
//! This module reads the formats and makes the decisions; it does no file or SQLite access. The
//! shell copies the other browser's files, passes in their text and the rows of its SQL queries,
//! and writes what comes out through its own engine's services.
//!
//! - [`sources`]: which browsers we import from, and where each keeps its data.
//! - [`profiles`]: a Chrome-family browser's profiles, from its `Local State`.
//! - [`bookmarks`]: a Chrome-family `Bookmarks` file, and the merge into existing bookmarks.
//! - [`history`], [`search`], [`addresses`], [`cookies`]: rows of `History`, `Web Data` and
//!   `Cookies`, and which of them come along.
//! - [`extensions`]: a profile's Chrome Web Store extensions, from its preferences.
//! - [`arc`]: Arc's spaces, tabs and Favourites, from `StorableSidebar.json`.
//! - [`tabs`]: a Chrome-family browser's open tabs, in the order they join the current space.
//! - [`safari`]: history from Safari's export file.
//! - [`plan`]: where imported spaces and tabs go.

pub mod addresses;
pub mod arc;
pub mod bookmarks;
pub mod bookmarks_html;
pub mod cookies;
pub mod extensions;
pub mod history;
pub mod plan;
pub mod profiles;
pub mod safari;
pub mod search;
pub mod sources;
pub mod tabs;

use std::fmt;

/// Why a file couldn't be read. Each item of an import fails on its own; the rest still imports.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ImportError {
    /// The file isn't valid JSON.
    Json(String),
    /// Valid JSON, but not laid out the way the format is.
    Format(String),
}

impl fmt::Display for ImportError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ImportError::Json(e) => write!(f, "not valid JSON: {e}"),
            ImportError::Format(e) => write!(f, "unexpected layout: {e}"),
        }
    }
}

impl std::error::Error for ImportError {}

/// A tab to open: its address and the title to show until it loads.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImportedTab {
    pub url: String,
    pub title: String,
}

/// Parses `text` as JSON.
pub(crate) fn parse_json(text: &str) -> Result<serde_json::Value, ImportError> {
    serde_json::from_str(text).map_err(|e| ImportError::Json(e.to_string()))
}

/// Whether `url` is a web page (`http` or `https`), the only addresses import brings along as
/// history and tabs (docs/ux.md §13).
pub fn is_web_url(url: &str) -> bool {
    let Some((scheme, rest)) = url.split_once(':') else {
        return false;
    };
    (scheme.eq_ignore_ascii_case("http") || scheme.eq_ignore_ascii_case("https"))
        && rest.starts_with("//")
        && rest.len() > 2
}

/// Microseconds between 1601-01-01 (Chromium's time base) and 1970-01-01.
const WINDOWS_TO_UNIX_EPOCH_MICROS: i64 = 11_644_473_600_000_000;

/// A Chromium timestamp (microseconds since 1601) as microseconds since the Unix epoch.
pub fn chromium_time_to_unix_micros(t: i64) -> i64 {
    t.saturating_sub(WINDOWS_TO_UNIX_EPOCH_MICROS)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn web_urls() {
        assert!(is_web_url("https://example.com/"));
        assert!(is_web_url("HTTP://example.com"));
        assert!(!is_web_url("chrome://newtab/"));
        assert!(!is_web_url("file:///tmp/a.html"));
        assert!(!is_web_url("javascript:alert(1)"));
        assert!(!is_web_url("https:"));
        assert!(!is_web_url("https://"));
        assert!(!is_web_url("about:blank"));
        assert!(!is_web_url(""));
    }

    #[test]
    fn chromium_times() {
        assert_eq!(chromium_time_to_unix_micros(11_644_473_600_000_000), 0);
        assert_eq!(
            chromium_time_to_unix_micros(13_000_000_000_000_000),
            1_355_526_400_000_000
        );
        assert_eq!(chromium_time_to_unix_micros(i64::MIN), i64::MIN);
    }

    #[test]
    fn errors_say_what_failed() {
        assert_eq!(
            parse_json("{").unwrap_err().to_string(),
            "not valid JSON: EOF while parsing an object at line 1 column 1"
        );
        assert_eq!(
            ImportError::Format("no roots".into()).to_string(),
            "unexpected layout: no roots"
        );
    }
}
