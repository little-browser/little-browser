// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Search shortcuts, from the `keywords` table of a Chrome-family `Web Data` file. The shell runs
//! `SELECT short_name, keyword, url, safe_for_autoreplace, prepopulate_id, created_by_policy,
//! is_active, starter_pack_id FROM keywords` (a missing column reads as 0).

use super::is_web_url;

/// One row of the `keywords` table.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct KeywordRow {
    pub short_name: String,
    pub keyword: String,
    pub url: String,
    /// Set for engines the browser added by itself (from sites it saw); 0 for ones the user
    /// added or edited.
    pub safe_for_autoreplace: i64,
    /// Non-zero for the browser's built-in engines.
    pub prepopulate_id: i64,
    pub created_by_policy: i64,
    /// 0 unspecified, 1 active, 2 turned off by the user.
    pub is_active: i64,
    /// Non-zero for Chrome's "@bookmarks"-style shortcuts.
    pub starter_pack_id: i64,
}

/// A site search the user added: typing `keyword` and a query searches `url`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SearchShortcut {
    pub name: String,
    pub keyword: String,
    /// The search address, with `{searchTerms}` where the query goes.
    pub url: String,
}

const IS_ACTIVE_FALSE: i64 = 2;

/// The site searches the user added (docs/ux.md §13): not the browser's built-in engines, not
/// ones it added by itself or a policy set, not ones the user turned off. Each needs a keyword
/// and a web address with `{searchTerms}` in it. A keyword comes once, the first time (ignoring
/// case). The default search engine isn't one of these: it stays the user's own choice.
pub fn search_shortcuts(rows: Vec<KeywordRow>) -> Vec<SearchShortcut> {
    let mut keywords: Vec<String> = Vec::new();
    let mut shortcuts = Vec::new();
    for r in rows {
        let keyword = r.keyword.trim();
        let user_added = r.safe_for_autoreplace == 0
            && r.prepopulate_id == 0
            && r.created_by_policy == 0
            && r.starter_pack_id == 0
            && r.is_active != IS_ACTIVE_FALSE;
        let usable = !keyword.is_empty() && is_web_url(&r.url) && r.url.contains("{searchTerms}");
        let lower = keyword.to_lowercase();
        if user_added && usable && !keywords.contains(&lower) {
            keywords.push(lower);
            let name = r.short_name.trim();
            shortcuts.push(SearchShortcut {
                name: if name.is_empty() { keyword } else { name }.to_owned(),
                keyword: keyword.to_owned(),
                url: r.url,
            });
        }
    }
    shortcuts
}

#[cfg(test)]
mod tests {
    use super::*;

    fn user(name: &str, keyword: &str, url: &str) -> KeywordRow {
        KeywordRow {
            short_name: name.into(),
            keyword: keyword.into(),
            url: url.into(),
            ..Default::default()
        }
    }

    const WIKI: &str = "https://en.wikipedia.org/w/index.php?search={searchTerms}";

    #[test]
    fn keeps_what_the_user_added() {
        assert_eq!(
            search_shortcuts(vec![
                user(" Wikipedia ", " w ", WIKI),
                user("", "gh", "https://github.com/search?q={searchTerms}")
            ]),
            [
                SearchShortcut {
                    name: "Wikipedia".into(),
                    keyword: "w".into(),
                    url: WIKI.into()
                },
                SearchShortcut {
                    name: "gh".into(),
                    keyword: "gh".into(),
                    url: "https://github.com/search?q={searchTerms}".into()
                },
            ]
        );
    }

    #[test]
    fn leaves_out_engines_the_user_didnt_add() {
        let rows = vec![
            KeywordRow {
                prepopulate_id: 1,
                ..user("Google", "google.com", WIKI)
            },
            KeywordRow {
                safe_for_autoreplace: 1,
                ..user("Seen", "seen.example", WIKI)
            },
            KeywordRow {
                created_by_policy: 1,
                ..user("Policy", "p", WIKI)
            },
            KeywordRow {
                starter_pack_id: 2,
                ..user("Bookmarks", "@bookmarks", WIKI)
            },
            KeywordRow {
                is_active: 2,
                ..user("Off", "off", WIKI)
            },
        ];
        assert!(search_shortcuts(rows).is_empty());
        let active = KeywordRow {
            is_active: 1,
            ..user("On", "on", WIKI)
        };
        assert_eq!(search_shortcuts(vec![active]).len(), 1);
    }

    #[test]
    fn needs_a_keyword_and_a_web_search_address() {
        assert!(search_shortcuts(vec![
            user("No keyword", "  ", WIKI),
            user("No query", "nq", "https://example.com/"),
            user("Script", "js", "javascript:{searchTerms}"),
        ])
        .is_empty());
    }

    #[test]
    fn a_keyword_comes_once() {
        let shortcuts = search_shortcuts(vec![
            user("First", "W", WIKI),
            user("Second", "w", "https://other.example/?q={searchTerms}"),
        ]);
        assert_eq!(shortcuts.len(), 1);
        assert_eq!(shortcuts[0].name, "First");
    }
}
