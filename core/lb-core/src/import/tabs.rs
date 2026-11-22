// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A Chrome-family browser's open tabs, which join the current space (docs/ux.md §13). The
//! shell reads them from the browser's session file.

use super::is_web_url;

/// An open tab, as the session file has it.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct OpenTab {
    /// Its window, numbered from 0 in the session's order.
    pub window: usize,
    pub url: String,
    pub title: String,
    pub pinned: bool,
    /// Its tab group within the window; empty when it has none.
    pub group: String,
    pub group_title: String,
    /// Chromium's `TabGroupColorId`.
    pub group_color: i32,
}

/// The tabs in the order they join the space: every pinned tab first (window by window), then
/// the rest. Only web pages come along; pinned tabs lose their group (Chromium can't group them),
/// and group ids get their window's number, so groups of different windows stay apart.
pub fn place_open_tabs(tabs: Vec<OpenTab>) -> Vec<OpenTab> {
    let (pinned, today): (Vec<OpenTab>, Vec<OpenTab>) = tabs
        .into_iter()
        .filter(|t| is_web_url(&t.url))
        .map(|mut t| {
            if t.pinned || t.group.is_empty() {
                t.group.clear();
                t.group_title.clear();
                t.group_color = 0;
            } else {
                t.group = format!("{}:{}", t.window, t.group);
            }
            t
        })
        .partition(|t| t.pinned);
    let mut placed = pinned;
    placed.extend(today);
    placed
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tab(window: usize, url: &str, pinned: bool, group: &str) -> OpenTab {
        OpenTab {
            window,
            url: url.into(),
            title: url.into(),
            pinned,
            group: group.into(),
            group_title: if group.is_empty() { "" } else { "G" }.into(),
            group_color: if group.is_empty() { 0 } else { 3 },
        }
    }

    #[test]
    fn pinned_first_then_the_rest_in_order() {
        let placed = place_open_tabs(vec![
            tab(0, "https://a/", false, ""),
            tab(0, "https://p1/", true, ""),
            tab(1, "https://b/", false, "g"),
            tab(1, "https://p2/", true, "g"),
            tab(1, "chrome://newtab/", false, ""),
            tab(0, "https://c/", false, "g"),
        ]);
        let urls: Vec<&str> = placed.iter().map(|t| t.url.as_str()).collect();
        assert_eq!(
            urls,
            [
                "https://p1/",
                "https://p2/",
                "https://a/",
                "https://b/",
                "https://c/"
            ]
        );
        // Pinned tabs leave their group; groups of different windows stay apart.
        assert_eq!(placed[1].group, "");
        assert_eq!(
            (placed[1].group_title.as_str(), placed[1].group_color),
            ("", 0)
        );
        assert_eq!(placed[3].group, "1:g");
        assert_eq!(placed[3].group_title, "G");
        assert_eq!(placed[4].group, "0:g");
        assert!(place_open_tabs(vec![]).is_empty());
    }
}
