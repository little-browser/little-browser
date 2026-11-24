// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The command bar's ranking (docs/ux.md §4): how well typed text matches a result, how result
//! kinds are ordered among equal matches, and which results each mode lists. The shells keep
//! reading typed text as an address, which needs their URL parser and the public suffix list.

/// What the command bar opens: a new tab; a page in the current tab (a New Tab Group's blank
/// first tab); the current tab's address, to edit; the space's name, to rename; a page in a
/// new little window; a tab's name, to rename; or a pinned tab's link, to edit.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Mode {
    NewTab,
    OpenHere,
    EditAddress,
    RenameSpace,
    LittleWindow,
    RenameTab,
    EditPinnedUrl,
}

impl Mode {
    /// Whether the bar only edits a name or a link: it lists just what the typed text sets.
    pub fn is_editing(self) -> bool {
        matches!(
            self,
            Mode::RenameSpace | Mode::RenameTab | Mode::EditPinnedUrl
        )
    }
}

/// The kinds of result the command bar lists.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ResultKind {
    OpenTab,
    Command,
    Bookmark,
    History,
    Address,
    Search,
}

/// Whether the bar in `mode` lists results of `kind`. In the little-window mode everything
/// chosen opens a page in a new little window, so it lists no open tabs (they'd switch the window
/// behind) and no commands (they'd act on it). A bar editing a name or a link lists none.
pub fn offers_result(mode: Mode, kind: ResultKind) -> bool {
    if mode.is_editing() {
        return false;
    }
    mode != Mode::LittleWindow || !matches!(kind, ResultKind::OpenTab | ResultKind::Command)
}

/// What is added to a result's match score for its kind, so that among equally good matches
/// open tabs come first, then commands, then bookmarks, then history. The boost is small, so a
/// better match of any kind still comes first.
pub fn result_boost(kind: ResultKind) -> f64 {
    match kind {
        ResultKind::OpenTab => 0.3,
        ResultKind::Command => 0.2,
        ResultKind::Bookmark => 0.1,
        ResultKind::History | ResultKind::Address | ResultKind::Search => 0.0,
    }
}

/// How many bookmarks the bar lists at most, the best matches.
pub const MAX_BOOKMARK_RESULTS: usize = 6;

/// Which bookmark matches the bar lists, given their scores: the indexes of the best
/// [`MAX_BOOKMARK_RESULTS`], best first (equal scores keep their order). Only these count as
/// listed, so a page left out here can still come from history.
pub fn pick_bookmark_results(scores: &[f64]) -> Vec<usize> {
    let mut picked: Vec<usize> = (0..scores.len()).collect();
    // Stable: equal scores keep their order.
    picked.sort_by(|&a, &b| scores[b].total_cmp(&scores[a]));
    picked.truncate(MAX_BOOKMARK_RESULTS);
    picked
}

/// How well `query` matches `text`, case-insensitively (ASCII); 0 means no match. A substring
/// scores above 2 (more at the start of the text or of a word); letters in order with gaps
/// score below 1, less the bigger the gaps. Positions count UTF-16 code units, as the desktop
/// shell's strings do.
pub fn match_score(query: &str, text: &str) -> f64 {
    let lower = |s: &str| -> Vec<u16> {
        s.encode_utf16()
            .map(|u| {
                if (b'A' as u16..=b'Z' as u16).contains(&u) {
                    u + 32
                } else {
                    u
                }
            })
            .collect()
    };
    let q = lower(query);
    if q.is_empty() {
        return 0.0;
    }
    let t = lower(text);
    if let Some(i) = t.windows(q.len()).position(|w| w == q.as_slice()) {
        let is_alpha = |u: u16| u < 128 && (u as u8).is_ascii_alphabetic();
        let word_start = i == 0 || !is_alpha(t[i - 1]);
        return 2.0 + if i == 0 { 1.0 } else { 0.0 } + if word_start { 0.5 } else { 0.0 }
            - i.min(200) as f64 / 1000.0;
    }
    let mut ti = 0;
    let mut gaps = 0;
    for &ch in &q {
        let Some(j) = t[ti..].iter().position(|&u| u == ch).map(|k| ti + k) else {
            return 0.0;
        };
        gaps += j - ti;
        ti = j + 1;
    }
    if gaps > 25 {
        0.0
    } else {
        1.0 - gaps as f64 / 40.0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const MODES: [Mode; 7] = [
        Mode::NewTab,
        Mode::OpenHere,
        Mode::EditAddress,
        Mode::RenameSpace,
        Mode::LittleWindow,
        Mode::RenameTab,
        Mode::EditPinnedUrl,
    ];
    const KINDS: [ResultKind; 6] = [
        ResultKind::OpenTab,
        ResultKind::Command,
        ResultKind::Bookmark,
        ResultKind::History,
        ResultKind::Address,
        ResultKind::Search,
    ];

    #[test]
    fn little_window_lists_no_tabs_or_commands() {
        for kind in KINDS {
            let expected = !matches!(kind, ResultKind::OpenTab | ResultKind::Command);
            assert_eq!(
                offers_result(Mode::LittleWindow, kind),
                expected,
                "{kind:?}"
            );
        }
        for mode in MODES
            .into_iter()
            .filter(|&m| m != Mode::LittleWindow && !m.is_editing())
        {
            for kind in KINDS {
                assert!(offers_result(mode, kind), "{mode:?} {kind:?}");
            }
        }
    }

    #[test]
    fn editing_a_name_or_link_lists_no_results() {
        for mode in [Mode::RenameSpace, Mode::RenameTab, Mode::EditPinnedUrl] {
            assert!(mode.is_editing());
            for kind in KINDS {
                assert!(!offers_result(mode, kind), "{mode:?} {kind:?}");
            }
        }
        for mode in [
            Mode::NewTab,
            Mode::OpenHere,
            Mode::EditAddress,
            Mode::LittleWindow,
        ] {
            assert!(!mode.is_editing(), "{mode:?}");
        }
    }

    #[test]
    fn boosts_order_kinds_without_beating_a_better_match() {
        let b = result_boost;
        assert!(b(ResultKind::OpenTab) > b(ResultKind::Command));
        assert!(b(ResultKind::Command) > b(ResultKind::Bookmark));
        assert!(b(ResultKind::Bookmark) > b(ResultKind::History));
        assert_eq!(b(ResultKind::History), 0.0);
        assert_eq!(b(ResultKind::Address), 0.0);
        assert_eq!(b(ResultKind::Search), 0.0);
        // A substring match anywhere beats letters in order, whatever the kind.
        assert!(match_score("ab", "xab") + b(ResultKind::History) > 1.0 + b(ResultKind::OpenTab));
    }

    #[test]
    fn bookmark_picks() {
        assert_eq!(pick_bookmark_results(&[1.0, 3.0, 2.0]), [1, 2, 0]);
        // Equal scores keep their order.
        assert_eq!(pick_bookmark_results(&[2.0, 2.0, 1.0, 2.0]), [0, 1, 3, 2]);
        let many: Vec<f64> = (0..10).map(f64::from).collect();
        assert_eq!(pick_bookmark_results(&many), [9, 8, 7, 6, 5, 4]);
        assert!(pick_bookmark_results(&[]).is_empty());
    }

    #[test]
    fn substring_scores() {
        assert_eq!(match_score("git", "GitHub"), 3.5);
        // At a word start, not at the start of the text.
        assert!((match_score("hub", "Git hub") - (2.5 - 0.004)).abs() < 1e-9);
        // In the middle of a word.
        assert!((match_score("hub", "GitHub") - (2.0 - 0.003)).abs() < 1e-9);
        // Earlier is better, and the position penalty stops at 200.
        assert!(match_score("x", "ax") > match_score("x", "aax"));
        let far = format!("{}x", "a".repeat(500));
        assert!((match_score("x", &far) - (2.0 - 0.2)).abs() < 1e-9);
    }

    #[test]
    fn letters_in_order_score_below_one() {
        assert_eq!(match_score("gh", "GitHub"), 1.0 - 2.0 / 40.0);
        assert_eq!(match_score("ghb", "GitHub"), 1.0 - 3.0 / 40.0);
        assert_eq!(match_score("hg", "GitHub"), 0.0);
        let gappy = format!("a{}b", "x".repeat(26));
        assert_eq!(match_score("ab", &gappy), 0.0);
        let near = format!("a{}b", "x".repeat(25));
        assert_eq!(match_score("ab", &near), 1.0 - 25.0 / 40.0);
    }

    #[test]
    fn empty_and_non_ascii() {
        assert_eq!(match_score("", "anything"), 0.0);
        assert_eq!(match_score("a", ""), 0.0);
        // Only ASCII letters fold case.
        assert_eq!(match_score("é", "É"), 0.0);
        assert_eq!(match_score("é", "café"), 2.0 - 0.003);
        // A letter after a non-ASCII one starts a word; positions are UTF-16 units.
        assert_eq!(match_score("b", "\u{1F600}b"), 2.5 - 0.002);
    }
}
