// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! History, from the `urls` table of a Chrome-family `History` file. The shell runs
//! `SELECT url, title, visit_count, typed_count, last_visit_time, hidden FROM urls`.

use super::{chromium_time_to_unix_micros, is_web_url};
use std::collections::HashMap;

/// One row of the `urls` table.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HistoryRow {
    pub url: String,
    pub title: String,
    pub visit_count: i64,
    pub typed_count: i64,
    /// Microseconds since 1601, as Chromium stores it.
    pub last_visit_time: i64,
    pub hidden: bool,
}

/// A page to add to history.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HistoryPage {
    pub url: String,
    pub title: String,
    pub visit_count: i64,
    pub typed_count: i64,
    /// Microseconds since the Unix epoch.
    pub last_visit_unix_micros: i64,
}

/// The pages that come along: web pages (`http`, `https`) the browser shows in its history (not
/// hidden ones such as subframes), last visited at or after `oldest_unix_micros` (history here
/// keeps nothing older, so nothing older is counted). A page comes once, in the order it first
/// appears, with its latest visit and that visit's title and the highest counts any row gives
/// (Firefox gives one row per visit). Counts below zero read as zero, and a page counts at least
/// one visit.
pub fn history_pages(rows: Vec<HistoryRow>, oldest_unix_micros: i64) -> Vec<HistoryPage> {
    let mut pages: Vec<HistoryPage> = Vec::new();
    let mut index: HashMap<String, usize> = HashMap::new();
    for r in rows {
        if r.hidden
            || r.last_visit_time <= 0
            || chromium_time_to_unix_micros(r.last_visit_time) < oldest_unix_micros
            || !is_web_url(&r.url)
        {
            continue;
        }
        let page = HistoryPage {
            last_visit_unix_micros: chromium_time_to_unix_micros(r.last_visit_time),
            visit_count: r.visit_count.max(1),
            typed_count: r.typed_count.max(0),
            url: r.url,
            title: r.title,
        };
        merge_page(&mut pages, &mut index, page);
    }
    pages
}

/// Adds `page` to `pages`, or merges it into the page with the same address: the later visit
/// (and its title) wins, the counts are the higher of the two.
pub(crate) fn merge_page(
    pages: &mut Vec<HistoryPage>,
    index: &mut HashMap<String, usize>,
    page: HistoryPage,
) {
    match index.get(&page.url) {
        Some(&i) => {
            let known = &mut pages[i];
            let visits = known.visit_count.max(page.visit_count);
            let typed = known.typed_count.max(page.typed_count);
            if page.last_visit_unix_micros > known.last_visit_unix_micros {
                known.last_visit_unix_micros = page.last_visit_unix_micros;
                known.title = page.title;
            }
            known.visit_count = visits;
            known.typed_count = typed;
        }
        None => {
            index.insert(page.url.clone(), pages.len());
            pages.push(page);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn row(url: &str, visits: i64, typed: i64, time: i64, hidden: bool) -> HistoryRow {
        HistoryRow {
            url: url.into(),
            title: format!("title of {url}"),
            visit_count: visits,
            typed_count: typed,
            last_visit_time: time,
            hidden,
        }
    }

    #[test]
    fn keeps_visited_web_pages_with_unix_times() {
        let pages = history_pages(
            vec![
                row("https://a.example/", 3, 1, 13_000_000_000_000_000, false),
                row("http://b.example/", 0, -2, 11_644_473_600_000_001, false),
            ],
            0,
        );
        assert_eq!(
            pages,
            [
                HistoryPage {
                    url: "https://a.example/".into(),
                    title: "title of https://a.example/".into(),
                    visit_count: 3,
                    typed_count: 1,
                    last_visit_unix_micros: 1_355_526_400_000_000,
                },
                HistoryPage {
                    url: "http://b.example/".into(),
                    title: "title of http://b.example/".into(),
                    visit_count: 1,
                    typed_count: 0,
                    last_visit_unix_micros: 1,
                },
            ]
        );
    }

    #[test]
    fn leaves_out_hidden_undated_and_non_web_pages() {
        assert!(history_pages(
            vec![
                row(
                    "https://hidden.example/",
                    1,
                    0,
                    13_000_000_000_000_000,
                    true
                ),
                row("https://undated.example/", 1, 0, 0, false),
                row("chrome://settings/", 1, 0, 13_000_000_000_000_000, false),
                row(
                    "file:///Users/me/a.pdf",
                    1,
                    0,
                    13_000_000_000_000_000,
                    false
                ),
            ],
            0
        )
        .is_empty());
    }

    #[test]
    fn leaves_out_pages_older_than_history_keeps() {
        let oldest = 1_355_526_400_000_000; // 13_000_000_000_000_000 in Chromium's time base
        let pages = history_pages(
            vec![
                row("https://old.example/", 1, 0, 12_999_999_999_999_999, false),
                row("https://edge.example/", 1, 0, 13_000_000_000_000_000, false),
            ],
            oldest,
        );
        assert_eq!(pages.len(), 1);
        assert_eq!(pages[0].url, "https://edge.example/");
    }

    #[test]
    fn a_page_comes_once_with_its_latest_visit() {
        // Firefox: one row per visit.
        let mut late = row("https://a.example/", 3, 0, 13_000_000_000_000_200, false);
        late.title = "Late".into();
        let mut early = row("https://a.example/", 5, 2, 13_000_000_000_000_100, false);
        early.title = "Early".into();
        let pages = history_pages(
            vec![
                early,
                row("https://b.example/", 1, 0, 13_000_000_000_000_000, false),
                late,
            ],
            0,
        );
        assert_eq!(pages.len(), 2);
        assert_eq!(pages[0].url, "https://a.example/");
        assert_eq!(pages[0].title, "Late");
        assert_eq!(pages[0].visit_count, 5);
        assert_eq!(pages[0].typed_count, 2);
        assert_eq!(pages[0].last_visit_unix_micros, 1_355_526_400_000_200);
        // An older visit after a newer one keeps the newer title.
        let mut older = row("https://a.example/", 1, 0, 13_000_000_000_000_050, false);
        older.title = "Older".into();
        let mut newer = row("https://a.example/", 1, 0, 13_000_000_000_000_060, false);
        newer.title = "Newer".into();
        assert_eq!(history_pages(vec![newer, older], 0)[0].title, "Newer");
    }
}
