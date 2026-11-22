// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! History from Safari's export file (File → Export Browsing Data to File): the `History.json`
//! in the `.zip`. Its `history` list holds one entry per page, with `url`, `title`, `time_usec`
//! (microseconds since the Unix epoch) and `visit_count`. The export's bookmarks are an HTML
//! bookmarks file, which Chromium's own importer reads.

use super::history::{merge_page, HistoryPage};
use super::{is_web_url, parse_json, ImportError};
use serde_json::Value;
use std::collections::HashMap;

/// The pages of Safari's history files (one per Safari profile: `History.json`,
/// `History - Profile 2.json`, ...) that come along: web pages last visited at or after
/// `oldest_unix_micros`. A page listed more than once, in one file or several, comes once, with
/// its latest visit and title and its higher visit count. A file that can't be read is an error
/// in the second list; the others still come along.
pub fn safari_history(
    texts: &[&str],
    oldest_unix_micros: i64,
) -> (Vec<HistoryPage>, Vec<ImportError>) {
    let mut pages: Vec<HistoryPage> = Vec::new();
    let mut index: HashMap<String, usize> = HashMap::new();
    let mut errors = Vec::new();
    for text in texts {
        let entries = match parse_json(text).and_then(|root| {
            root.get("history")
                .and_then(Value::as_array)
                .cloned()
                .ok_or_else(|| ImportError::Format("no history list".into()))
        }) {
            Ok(entries) => entries,
            Err(e) => {
                errors.push(e);
                continue;
            }
        };
        for e in entries {
            let url = e.get("url").and_then(Value::as_str).unwrap_or_default();
            let time = e.get("time_usec").and_then(Value::as_i64).unwrap_or(0);
            if !is_web_url(url) || time <= 0 || time < oldest_unix_micros {
                continue;
            }
            let page = HistoryPage {
                url: url.to_owned(),
                title: e
                    .get("title")
                    .and_then(Value::as_str)
                    .unwrap_or_default()
                    .to_owned(),
                visit_count: e
                    .get("visit_count")
                    .and_then(Value::as_i64)
                    .unwrap_or(1)
                    .max(1),
                typed_count: 0,
                last_visit_unix_micros: time,
            };
            merge_page(&mut pages, &mut index, page);
        }
    }
    (pages, errors)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_web_pages_once_each() {
        let text = r#"{"metadata": {"browser_name": "Safari", "data_type": "history"},
          "history": [
            {"url": "https://a.example/", "title": "Old A", "time_usec": 100, "visit_count": 5},
            {"url": "https://b.example/", "time_usec": 300},
            {"url": "https://a.example/", "title": "A", "time_usec": 200, "visit_count": 2},
            {"url": "https://a.example/", "title": "Older A", "time_usec": 50, "visit_count": 1},
            {"url": "file:///x", "title": "File", "time_usec": 300},
            {"url": "https://undated.example/", "title": "U"},
            {"url": "https://ancient.example/", "title": "Z", "time_usec": 10}
          ]}"#;
        let (pages, errors) = safari_history(&[text], 50);
        assert!(errors.is_empty());
        assert_eq!(
            pages,
            [
                HistoryPage {
                    url: "https://a.example/".into(),
                    title: "A".into(),
                    visit_count: 5,
                    typed_count: 0,
                    last_visit_unix_micros: 200,
                },
                HistoryPage {
                    url: "https://b.example/".into(),
                    title: "".into(),
                    visit_count: 1,
                    typed_count: 0,
                    last_visit_unix_micros: 300,
                },
            ]
        );
    }

    #[test]
    fn every_profile_file_counts_and_bad_ones_are_errors() {
        let first =
            r#"{"history": [{"url": "https://a.example/", "title": "A", "time_usec": 10}]}"#;
        let second = r#"{"history": [
            {"url": "https://a.example/", "title": "A later", "time_usec": 20, "visit_count": 3},
            {"url": "https://b.example/", "title": "B", "time_usec": 5}]}"#;
        let (pages, errors) = safari_history(&[first, "[", second, r#"{"metadata": {}}"#], 0);
        assert_eq!(pages.len(), 2);
        assert_eq!(pages[0].title, "A later");
        assert_eq!(pages[0].visit_count, 3);
        assert_eq!(pages[1].url, "https://b.example/");
        assert_eq!(errors.len(), 2);
        assert!(matches!(errors[0], ImportError::Json(_)));
        assert_eq!(errors[1], ImportError::Format("no history list".into()));
    }
}
