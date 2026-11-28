// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Tab rules every shell shares (docs/ux.md §2, §6): what closing a pinned tab does, which tab to
//! show when the shown one goes to sleep, the order session restore adds a window's tabs in, and
//! tab names and pinned links. Links are URL specs that the shell has already parsed: an empty
//! string is no link (or an invalid one).

/// A tab as seen when something asks to close it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PinnedCloseState {
    /// The user closed it (not the browser, an extension or a window close).
    pub user_gesture: bool,
    /// The whole window is closing.
    pub closing_all: bool,
    pub pinned: bool,
    /// In a split view.
    pub split: bool,
    /// Its page isn't loaded.
    pub asleep: bool,
    /// The window's active tab.
    pub shown: bool,
    /// An earlier close only hid it ([`PinnedCloseOutcome::Hide`]), or let its page ask before
    /// leaving ([`PinnedCloseOutcome::CloseAskingFirst`]).
    pub hidden_by_earlier_close: bool,
    /// Closed already, and its sleep hasn't run yet.
    pub sleep_pending: bool,
    /// Its page has a beforeunload handler. Many pages register one and never ask, so this alone
    /// doesn't mean the page holds unsaved input.
    pub asks_before_leaving: bool,
    /// The user has typed into the page (forms or contenteditable).
    pub had_user_edits: bool,
}

impl Default for PinnedCloseState {
    /// The user closing the shown, awake pinned tab, with nothing typed into it.
    fn default() -> Self {
        Self {
            user_gesture: true,
            closing_all: false,
            pinned: true,
            split: false,
            asleep: false,
            shown: true,
            hidden_by_earlier_close: false,
            sleep_pending: false,
            asks_before_leaving: false,
            had_user_edits: false,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PinnedCloseOutcome {
    /// Closes as usual.
    Close,
    /// Closes as usual, the page asks first; its follow-up close isn't stopped.
    CloseAskingFirst,
    /// Stays in place and unloads.
    Sleep,
    /// Stays in place, loaded: unloading could lose unsaved input.
    Hide,
    /// Closed again before its sleep ran: nothing more to do.
    AlreadySleeping,
}

/// Whether the page may hold unsaved input: it asks before it is left and the user has typed into
/// it (as Chromium's memory saver checks before it discards a tab).
pub fn may_hold_unsaved_input(state: &PinnedCloseState) -> bool {
    state.asks_before_leaving && state.had_user_edits
}

/// What closing a tab does (docs/ux.md §6, Pinned). The user closing an awake pinned tab puts it
/// to sleep; one that may hold unsaved input is only hidden, and closing it again, or while it
/// isn't shown, really closes it. Anything else closes as usual.
pub fn decide_pinned_close(state: &PinnedCloseState) -> PinnedCloseOutcome {
    // A sleeping pinned tab closes for good (its "−" button). So does one in a split view, which
    // closes out of the split, and one being closed by the browser itself.
    if !state.user_gesture || state.closing_all || !state.pinned || state.split || state.asleep {
        return PinnedCloseOutcome::Close;
    }
    // A close already hid it: this one is real.
    if state.hidden_by_earlier_close {
        return PinnedCloseOutcome::CloseAskingFirst;
    }
    let unsaved = may_hold_unsaved_input(state);
    // Not shown, so there is nothing to hide, and it can't be unloaded.
    if !state.shown && unsaved {
        return PinnedCloseOutcome::CloseAskingFirst;
    }
    if state.sleep_pending {
        return PinnedCloseOutcome::AlreadySleeping;
    }
    if unsaved {
        PinnedCloseOutcome::Hide
    } else {
        PinnedCloseOutcome::Sleep
    }
}

/// One of the window's other tabs, as seen when the shown tab goes to sleep.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct TabToShowCandidate {
    pub asleep: bool,
    /// When it was last focused, in any unit where later is larger.
    pub last_focused: i64,
    /// In a collapsed group: showing it would expand the group again.
    pub in_collapsed_group: bool,
}

/// Which of `others` to show when the shown pinned tab goes to sleep: the one focused last,
/// preferring tabs that aren't in a collapsed group, then tabs that are awake so nothing has to
/// reload. Ties go to the earlier tab. None when there are no other tabs.
pub fn choose_tab_to_show(others: &[TabToShowCandidate]) -> Option<usize> {
    // Higher is better: visible before hidden in a collapsed group, awake before asleep, then the
    // one focused last.
    let rank = |tab: &TabToShowCandidate| (!tab.in_collapsed_group, !tab.asleep, tab.last_focused);
    let mut best: Option<usize> = None;
    for (i, tab) in others.iter().enumerate() {
        if best.is_none_or(|b| rank(tab) > rank(&others[b])) {
            best = Some(i);
        }
    }
    best
}

/// The order in which session restore adds a saved window's `tab_count` tabs: the tab to select
/// first, then the others in their saved order. The first tab added to an empty window becomes its
/// active tab, and showing a tab loads it, so restoring in saved order would wake the first tab
/// (often a sleeping pinned tab) even when another one is selected. Adding each of the others at
/// its saved index keeps the saved order. `selected` is None when the window already has tabs
/// (restore selects none then); out of range, it is ignored.
pub fn tab_restore_order(tab_count: usize, selected: Option<usize>) -> Vec<usize> {
    let first = selected.filter(|&s| s < tab_count);
    first
        .into_iter()
        .chain((0..tab_count).filter(|&i| Some(i) != first))
        .collect()
}

/// A typed tab name as the tab keeps it: runs of whitespace and line breaks become one space, and
/// the ends are trimmed. Empty means the page's own title again.
pub fn clean_tab_name(typed: &str) -> String {
    typed.split_whitespace().collect::<Vec<_>>().join(" ")
}

/// The name a tab keeps when `typed` is entered while its page's title is `page_title`: the
/// cleaned text, or none (empty) when that is the page's own title (unchanged, it would stop
/// following the page's title).
pub fn name_from_input(typed: &str, page_title: &str) -> String {
    let name = clean_tab_name(typed);
    if name == clean_tab_name(page_title) {
        String::new()
    } else {
        name
    }
}

/// The link's title a pinned tab's row shows instead of its page's: only while it is `asleep` and
/// wakes at its link (it was closed away from it, docs/ux.md §6). Empty for none, also before the
/// link's page has a title. Awake, even woken in the background with `wakes_at_link` still set,
/// its row follows the page it shows.
fn sleeping_link_title(pinned_link: &PinnedLink, asleep: bool) -> &str {
    if asleep && pinned_link.wakes_at_link && !pinned_link.link.is_empty() {
        &pinned_link.title
    } else {
        ""
    }
}

/// The title a tab row shows: the user's name for it; for a pinned tab `asleep` away from its
/// link, its link's page title, the page it goes back to; otherwise the page's title.
pub fn tab_title_to_show(
    name: &str,
    pinned_link: &PinnedLink,
    page_title: &str,
    asleep: bool,
) -> String {
    let link_title = sleeping_link_title(pinned_link, asleep);
    if !name.is_empty() {
        name.to_owned()
    } else if !link_title.is_empty() {
        link_title.to_owned()
    } else {
        page_title.to_owned()
    }
}

/// Whether the row's title is the tab's own (a name, or a sleeping pinned tab's link's title), not
/// the page's: the row then shows no "Loading…" title.
pub fn has_own_title(name: &str, pinned_link: &PinnedLink, asleep: bool) -> bool {
    !name.is_empty() || !sleeping_link_title(pinned_link, asleep).is_empty()
}

fn without_fragment(url: &str) -> &str {
    url.split_once('#').map_or(url, |(before, _)| before)
}

/// Whether a pinned tab is showing a page other than its link (docs/ux.md §6: closing it then takes
/// it back to its link). The part after "#" doesn't count. False without a link or a page.
pub fn has_left_pinned_link(link: &str, page: &str) -> bool {
    !link.is_empty() && !page.is_empty() && without_fragment(link) != without_fragment(page)
}

/// What a pinned tab keeps with its link. Empty strings mean none.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct PinnedLink {
    /// The page the tab goes back to.
    pub link: String,
    /// The tab was closed (put to sleep) while it showed another page than its link: it loads its
    /// link, not that page, when it is shown again.
    pub wakes_at_link: bool,
    /// The title of the page at its link, which its row keeps while it shows other pages.
    pub title: String,
}

/// A tab's pinned link once its pin state or its page has settled: an unpinned tab has none (and
/// doesn't wake at one or keep its title); a pinned tab without one takes the page it has
/// committed (`committed`, empty for none), unless that is blank (`page_is_blank`: a new tab page
/// or about:blank; it takes the first real page it commits). A link it has is kept.
pub fn settle_pinned_link(
    pinned_link: PinnedLink,
    pinned: bool,
    committed: &str,
    page_is_blank: bool,
) -> PinnedLink {
    if !pinned {
        PinnedLink::default()
    } else if pinned_link.link.is_empty() && !committed.is_empty() && !page_is_blank {
        PinnedLink {
            link: committed.to_owned(),
            ..pinned_link
        }
    } else {
        pinned_link
    }
}

/// A restored tab's pinned link (from the session or the space's saved tabs) once it is in its
/// window: it settles ([`settle_pinned_link`]), and a tab already shown, which the user sees as it
/// was, no longer wakes at its link.
pub fn restored_pinned_link(
    pinned_link: PinnedLink,
    pinned: bool,
    committed: &str,
    page_is_blank: bool,
    shown: bool,
) -> PinnedLink {
    let mut settled = settle_pinned_link(pinned_link, pinned, committed, page_is_blank);
    if shown {
        settled.wakes_at_link = false;
    }
    settled
}

/// A pinned tab's link when its page has `page_title` (empty: none set yet) at `committed`: on its
/// link, the link's title becomes that title; on another page, the link's title stays.
pub fn take_pinned_title(pinned_link: PinnedLink, committed: &str, page_title: &str) -> PinnedLink {
    if !pinned_link.link.is_empty()
        && !committed.is_empty()
        && !has_left_pinned_link(&pinned_link.link, committed)
        && !page_title.is_empty()
    {
        PinnedLink {
            title: page_title.to_owned(),
            ..pinned_link
        }
    } else {
        pinned_link
    }
}

/// A pinned tab with `link` as its new link: a link to another page drops the old link's title
/// (the new page's comes with it), and the tab no longer wakes at its link (it is on the new one,
/// or loads it).
pub fn with_pinned_link(pinned_link: PinnedLink, link: &str) -> PinnedLink {
    let title = if pinned_link.link.is_empty() || has_left_pinned_link(&pinned_link.link, link) {
        String::new()
    } else {
        pinned_link.title
    };
    PinnedLink {
        link: link.to_owned(),
        wakes_at_link: false,
        title,
    }
}

/// A pinned tab's link once a close has put it to sleep (docs/ux.md §6): it wakes at its link if it
/// was showing another page (`current`), so closing a pinned tab takes it back to its link. A tab
/// without a link is left as it is.
pub fn after_pinned_sleep(pinned_link: PinnedLink, current: &str) -> PinnedLink {
    if pinned_link.link.is_empty() {
        return pinned_link;
    }
    PinnedLink {
        wakes_at_link: has_left_pinned_link(&pinned_link.link, current),
        ..pinned_link
    }
}

/// What showing a tab does with its link: whether to load the link (it was closed away from it),
/// and the link it keeps, which no longer wakes at it.
pub fn on_tab_shown(pinned_link: PinnedLink) -> (bool, PinnedLink) {
    let load_link = pinned_link.wakes_at_link && !pinned_link.link.is_empty();
    (
        load_link,
        PinnedLink {
            wakes_at_link: false,
            ..pinned_link
        },
    )
}

/// The tab menu's items for names and links, after Pin/Unpin. Each shell gives them its labels.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TabNameCommand {
    /// "Rename Tab…"
    Rename,
    /// "Edit Link…"
    EditPinnedLink,
    /// "Use Current Page as Link"
    UsePageAsPinnedLink,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TabNameMenuItem {
    pub command: TabNameCommand,
    pub enabled: bool,
}

/// Which items the menu of one tab shows: Rename for every tab; for a pinned tab also Edit Link and
/// Use Current Page as Link, the last enabled only once it has left its link (closing it goes back
/// to its link). A menu for several tabs shows none: each acts on one tab.
pub fn tab_name_menu_items(
    tabs: usize,
    pinned: bool,
    left_pinned_link: bool,
) -> Vec<TabNameMenuItem> {
    if tabs != 1 {
        return Vec::new();
    }
    let item = |command, enabled| TabNameMenuItem { command, enabled };
    let mut items = vec![item(TabNameCommand::Rename, true)];
    if pinned {
        items.push(item(TabNameCommand::EditPinnedLink, true));
        items.push(item(TabNameCommand::UsePageAsPinnedLink, left_pinned_link));
    }
    items
}

/// How many tabs a tab menu acts on, for [`tab_name_menu_items`]: one when the tab the menu is for
/// isn't selected; otherwise the selection, counting the tab's split view, which is always
/// selected whole, as one. `selected` is the selection's size, `selected_in_split` how many of it
/// are in that tab's split view (0 if it isn't in one).
pub fn tab_menu_targets(
    menu_tab_selected: bool,
    selected: usize,
    selected_in_split: usize,
) -> usize {
    if !menu_tab_selected {
        1
    } else if selected_in_split > 1 {
        selected.saturating_sub(selected_in_split) + 1
    } else {
        selected
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn close(f: impl FnOnce(&mut PinnedCloseState)) -> PinnedCloseOutcome {
        let mut state = PinnedCloseState::default();
        f(&mut state);
        decide_pinned_close(&state)
    }

    #[test]
    fn closing_an_awake_pinned_tab_sleeps_it() {
        assert_eq!(close(|_| {}), PinnedCloseOutcome::Sleep);
        assert_eq!(close(|s| s.shown = false), PinnedCloseOutcome::Sleep);
    }

    #[test]
    fn closes_as_usual_unless_the_user_closes_an_awake_pinned_tab_on_its_own() {
        assert_eq!(close(|s| s.user_gesture = false), PinnedCloseOutcome::Close);
        assert_eq!(close(|s| s.closing_all = true), PinnedCloseOutcome::Close);
        assert_eq!(close(|s| s.pinned = false), PinnedCloseOutcome::Close);
        assert_eq!(close(|s| s.split = true), PinnedCloseOutcome::Close);
        assert_eq!(close(|s| s.asleep = true), PinnedCloseOutcome::Close);
    }

    #[test]
    fn unsaved_input_needs_both_a_handler_and_typing() {
        let mut state = PinnedCloseState::default();
        assert!(!may_hold_unsaved_input(&state));
        state.asks_before_leaving = true;
        assert!(!may_hold_unsaved_input(&state));
        state.asks_before_leaving = false;
        state.had_user_edits = true;
        assert!(!may_hold_unsaved_input(&state));
        state.asks_before_leaving = true;
        assert!(may_hold_unsaved_input(&state));
    }

    #[test]
    fn a_tab_that_may_hold_unsaved_input_is_hidden_then_closed() {
        let unsaved = |s: &mut PinnedCloseState| {
            s.asks_before_leaving = true;
            s.had_user_edits = true;
        };
        assert_eq!(close(unsaved), PinnedCloseOutcome::Hide);
        assert_eq!(
            close(|s| {
                unsaved(s);
                s.hidden_by_earlier_close = true;
            }),
            PinnedCloseOutcome::CloseAskingFirst
        );
        assert_eq!(
            close(|s| {
                unsaved(s);
                s.shown = false;
            }),
            PinnedCloseOutcome::CloseAskingFirst
        );
    }

    #[test]
    fn an_earlier_hide_makes_the_next_close_real_even_without_input() {
        assert_eq!(
            close(|s| s.hidden_by_earlier_close = true),
            PinnedCloseOutcome::CloseAskingFirst
        );
    }

    #[test]
    fn closing_again_before_the_sleep_runs_does_nothing_more() {
        assert_eq!(
            close(|s| s.sleep_pending = true),
            PinnedCloseOutcome::AlreadySleeping
        );
        assert_eq!(
            close(|s| {
                s.sleep_pending = true;
                s.asks_before_leaving = true;
                s.had_user_edits = true;
            }),
            PinnedCloseOutcome::AlreadySleeping
        );
    }

    fn tab(asleep: bool, last_focused: i64, in_collapsed_group: bool) -> TabToShowCandidate {
        TabToShowCandidate {
            asleep,
            last_focused,
            in_collapsed_group,
        }
    }

    #[test]
    fn no_other_tabs_no_tab_to_show() {
        assert_eq!(choose_tab_to_show(&[]), None);
    }

    #[test]
    fn shows_the_tab_focused_last() {
        assert_eq!(
            choose_tab_to_show(&[
                tab(false, 1, false),
                tab(false, 3, false),
                tab(false, 2, false)
            ]),
            Some(1)
        );
    }

    #[test]
    fn prefers_awake_tabs_then_tabs_outside_collapsed_groups() {
        assert_eq!(
            choose_tab_to_show(&[tab(true, 9, false), tab(false, 1, false)]),
            Some(1)
        );
        assert_eq!(
            choose_tab_to_show(&[tab(false, 9, true), tab(true, 1, false)]),
            Some(1)
        );
        assert_eq!(choose_tab_to_show(&[tab(true, 1, true)]), Some(0));
    }

    #[test]
    fn ties_go_to_the_earlier_tab() {
        assert_eq!(
            choose_tab_to_show(&[tab(false, 5, false), tab(false, 5, false)]),
            Some(0)
        );
    }

    #[test]
    fn restore_adds_the_selected_tab_first() {
        assert_eq!(tab_restore_order(4, Some(2)), vec![2, 0, 1, 3]);
        assert_eq!(tab_restore_order(3, Some(0)), vec![0, 1, 2]);
        assert_eq!(tab_restore_order(3, Some(2)), vec![2, 0, 1]);
    }

    #[test]
    fn restore_keeps_the_saved_order_without_a_selection() {
        assert_eq!(tab_restore_order(3, None), vec![0, 1, 2]);
        assert_eq!(tab_restore_order(3, Some(3)), vec![0, 1, 2]);
        assert!(tab_restore_order(0, Some(0)).is_empty());
        assert!(tab_restore_order(0, None).is_empty());
    }

    #[test]
    fn names_are_cleaned() {
        assert_eq!(clean_tab_name("  Work \n\t mail  "), "Work mail");
        assert_eq!(clean_tab_name("a\u{00a0}\u{3000}b"), "a b");
        assert_eq!(clean_tab_name(" \n "), "");
        assert_eq!(clean_tab_name("Mail"), "Mail");
    }

    #[test]
    fn a_name_equal_to_the_page_title_follows_the_page_again() {
        assert_eq!(name_from_input(" Inbox ", "Inbox"), "");
        assert_eq!(name_from_input("Inbox", "  Inbox\n"), "");
        assert_eq!(name_from_input("Mail", "Inbox"), "Mail");
        assert_eq!(name_from_input("  ", "Inbox"), "");
    }

    fn link_titled(title: &str, wakes_at_link: bool) -> PinnedLink {
        PinnedLink {
            link: "https://mail.example.com/".into(),
            wakes_at_link,
            title: title.into(),
        }
    }

    #[test]
    fn a_sleeping_pinned_tab_shows_its_links_title_an_awake_one_its_pages() {
        let asleep = link_titled("Inbox (3)", true);
        assert_eq!(tab_title_to_show("Mail", &asleep, "Sent", true), "Mail");
        assert_eq!(tab_title_to_show("", &asleep, "Sent", true), "Inbox (3)");
        // Awake (browsing away from its link, or back on it): the page's title.
        assert_eq!(
            tab_title_to_show("", &link_titled("Inbox (3)", false), "Sent", false),
            "Sent"
        );
        // No title for the link yet, or no link: the page's title.
        assert_eq!(
            tab_title_to_show("", &link_titled("", true), "Sent", true),
            "Sent"
        );
        let no_link = PinnedLink {
            wakes_at_link: true,
            title: "Inbox".into(),
            ..PinnedLink::default()
        };
        assert_eq!(tab_title_to_show("", &no_link, "Sent", true), "Sent");
    }

    #[test]
    fn a_pinned_tab_woken_in_the_background_follows_its_page() {
        // Reloaded without being shown (tab menu Reload): awake on the page it was closed on, it
        // still wakes at its link when closed, but its row shows that page and its loading title.
        let woken = link_titled("Inbox", true);
        assert_eq!(tab_title_to_show("", &woken, "News", false), "News");
        assert!(!has_own_title("", &woken, false));
        assert_eq!(tab_title_to_show("Mail", &woken, "News", false), "Mail");
        assert!(has_own_title("Mail", &woken, false));
    }

    #[test]
    fn a_name_or_a_sleeping_links_title_is_the_tabs_own_title() {
        assert!(has_own_title("Mail", &PinnedLink::default(), false));
        assert!(has_own_title("", &link_titled("Inbox", true), true));
        assert!(!has_own_title("", &link_titled("Inbox", false), true));
        assert!(!has_own_title("", &link_titled("Inbox", false), false));
        assert!(!has_own_title("", &link_titled("", true), true));
        assert!(!has_own_title("", &PinnedLink::default(), true));
    }

    #[test]
    fn closing_a_pinned_tab_away_from_its_link_brings_back_the_links_title() {
        let on_link =
            take_pinned_title(link_titled("", false), "https://mail.example.com/", "Inbox");
        // Browsing elsewhere: the row follows the page; the link keeps its title.
        let browsing = take_pinned_title(on_link, "https://news.example.com/", "News");
        assert_eq!(tab_title_to_show("", &browsing, "News", false), "News");
        // Closed there: asleep, it shows its link's title again.
        let asleep = after_pinned_sleep(browsing, "https://news.example.com/");
        assert_eq!(tab_title_to_show("", &asleep, "News", true), "Inbox");
        // Shown again, it loads its link and follows that page.
        let (load_link, shown) = on_tab_shown(asleep);
        assert!(load_link);
        assert_eq!(tab_title_to_show("", &shown, "Inbox", false), "Inbox");
    }

    #[test]
    fn leaving_the_pinned_link_ignores_the_fragment() {
        let link = "https://mail.example/inbox";
        assert!(!has_left_pinned_link(link, link));
        assert!(!has_left_pinned_link(
            link,
            "https://mail.example/inbox#msg"
        ));
        assert!(!has_left_pinned_link(
            "https://mail.example/inbox#a",
            "https://mail.example/inbox#b"
        ));
        assert!(has_left_pinned_link(link, "https://mail.example/sent"));
        assert!(!has_left_pinned_link("", link));
        assert!(!has_left_pinned_link(link, ""));
    }

    const LINK: &str = "https://mail.example/inbox";
    const OTHER: &str = "https://mail.example/sent";

    fn linked(wakes_at_link: bool, title: &str) -> PinnedLink {
        PinnedLink {
            link: LINK.to_owned(),
            wakes_at_link,
            title: title.to_owned(),
        }
    }

    #[test]
    fn settling_a_link() {
        let page = "https://example.com/";
        // Unpinned: no link, and nothing that comes with one.
        assert_eq!(
            settle_pinned_link(linked(true, "Inbox"), false, page, false),
            PinnedLink::default()
        );
        // Pinned without one: takes the committed page, unless it is blank or missing.
        assert_eq!(
            settle_pinned_link(PinnedLink::default(), true, page, false).link,
            page
        );
        assert_eq!(
            settle_pinned_link(PinnedLink::default(), true, "about:blank", true),
            PinnedLink::default()
        );
        assert_eq!(
            settle_pinned_link(PinnedLink::default(), true, "", false),
            PinnedLink::default()
        );
        // A link it has is kept, with what comes with it.
        assert_eq!(
            settle_pinned_link(linked(true, "Inbox"), true, page, false),
            linked(true, "Inbox")
        );
    }

    #[test]
    fn a_restored_tab_already_shown_no_longer_wakes_at_its_link() {
        assert_eq!(
            restored_pinned_link(linked(true, "Inbox"), true, OTHER, false, true),
            linked(false, "Inbox")
        );
        assert_eq!(
            restored_pinned_link(linked(true, "Inbox"), true, OTHER, false, false),
            linked(true, "Inbox")
        );
        // It settles too: unpinned, nothing is left.
        assert_eq!(
            restored_pinned_link(linked(true, "Inbox"), false, OTHER, false, false),
            PinnedLink::default()
        );
    }

    #[test]
    fn the_links_title_is_taken_only_on_the_link() {
        assert_eq!(
            take_pinned_title(linked(false, ""), LINK, "Inbox (3)").title,
            "Inbox (3)"
        );
        // The part after "#" doesn't count.
        assert_eq!(
            take_pinned_title(linked(false, ""), "https://mail.example/inbox#m", "Inbox").title,
            "Inbox"
        );
        // On another page, with no title yet, without a page or without a link: unchanged.
        assert_eq!(
            take_pinned_title(linked(false, "Inbox"), OTHER, "Sent"),
            linked(false, "Inbox")
        );
        assert_eq!(
            take_pinned_title(linked(false, "Inbox"), LINK, ""),
            linked(false, "Inbox")
        );
        assert_eq!(
            take_pinned_title(linked(false, "Inbox"), "", "Sent"),
            linked(false, "Inbox")
        );
        assert_eq!(
            take_pinned_title(PinnedLink::default(), LINK, "Inbox"),
            PinnedLink::default()
        );
    }

    #[test]
    fn a_new_link_drops_the_old_links_title_and_waking() {
        assert_eq!(
            with_pinned_link(linked(true, "Inbox"), OTHER),
            PinnedLink {
                link: OTHER.to_owned(),
                wakes_at_link: false,
                title: String::new(),
            }
        );
        // The same page (the part after "#" doesn't count) keeps its title.
        assert_eq!(
            with_pinned_link(linked(true, "Inbox"), "https://mail.example/inbox#m").title,
            "Inbox"
        );
        // A first link has no title yet.
        assert_eq!(
            with_pinned_link(
                PinnedLink {
                    title: "Stale".to_owned(),
                    ..PinnedLink::default()
                },
                LINK
            ),
            linked(false, "")
        );
    }

    #[test]
    fn a_pinned_tab_closed_away_from_its_link_wakes_at_it() {
        assert_eq!(
            after_pinned_sleep(linked(false, "Inbox"), OTHER),
            linked(true, "Inbox")
        );
        assert_eq!(
            after_pinned_sleep(linked(true, "Inbox"), LINK),
            linked(false, "Inbox")
        );
        // Without a link it is left as it is.
        assert_eq!(
            after_pinned_sleep(PinnedLink::default(), OTHER),
            PinnedLink::default()
        );
    }

    #[test]
    fn showing_a_tab_loads_its_link_once() {
        assert_eq!(
            on_tab_shown(linked(true, "Inbox")),
            (true, linked(false, "Inbox"))
        );
        assert_eq!(
            on_tab_shown(linked(false, "Inbox")),
            (false, linked(false, "Inbox"))
        );
        // Waking without a link loads nothing.
        let no_link = PinnedLink {
            wakes_at_link: true,
            ..PinnedLink::default()
        };
        assert_eq!(on_tab_shown(no_link), (false, PinnedLink::default()));
    }

    #[test]
    fn menu_items_for_one_tab() {
        use TabNameCommand::*;
        let item = |command, enabled| TabNameMenuItem { command, enabled };
        assert_eq!(
            tab_name_menu_items(1, false, false),
            vec![item(Rename, true)]
        );
        assert_eq!(
            tab_name_menu_items(1, true, false),
            vec![
                item(Rename, true),
                item(EditPinnedLink, true),
                item(UsePageAsPinnedLink, false),
            ]
        );
        assert_eq!(
            tab_name_menu_items(1, true, true),
            vec![
                item(Rename, true),
                item(EditPinnedLink, true),
                item(UsePageAsPinnedLink, true),
            ]
        );
        assert!(tab_name_menu_items(2, true, true).is_empty());
        assert!(tab_name_menu_items(0, false, false).is_empty());
    }

    #[test]
    fn menu_targets() {
        assert_eq!(tab_menu_targets(false, 3, 0), 1);
        assert_eq!(tab_menu_targets(true, 3, 0), 3);
        assert_eq!(tab_menu_targets(true, 1, 0), 1);
        // A split view counts once.
        assert_eq!(tab_menu_targets(true, 2, 2), 1);
        assert_eq!(tab_menu_targets(true, 4, 2), 3);
        assert_eq!(tab_menu_targets(true, 3, 1), 3);
        // A split larger than the selection can't happen; it still counts as one.
        assert_eq!(tab_menu_targets(true, 1, 2), 1);
    }
}
