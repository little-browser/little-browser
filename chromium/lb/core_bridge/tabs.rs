// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The C++ binding of lb-core's tab rules (pinned close, the tab to show, restore order, tab
//! names and pinned links, pinned groups) and the password manager list, in `namespace lb::core`:
//! include `lb/core_bridge/tabs.rs.h`. The rules live in lb-core and are tested there; this module
//! only converts types. An absent optional value is -1; a missing link or group is empty.

use lb_core::{password_manager, pinned_groups, tabs};

#[cxx::bridge(namespace = "lb::core")]
mod ffi {
    // Pinned close (lb_core::tabs).
    struct PinnedCloseState {
        user_gesture: bool,
        closing_all: bool,
        pinned: bool,
        split: bool,
        asleep: bool,
        shown: bool,
        hidden_by_earlier_close: bool,
        sleep_pending: bool,
        asks_before_leaving: bool,
        had_user_edits: bool,
    }

    enum PinnedCloseOutcome {
        Close,
        CloseAskingFirst,
        Sleep,
        Hide,
        AlreadySleeping,
    }

    struct TabToShowCandidate {
        asleep: bool,
        last_focused: i64,
        in_collapsed_group: bool,
    }

    enum TabNameCommand {
        Rename,
        EditPinnedLink,
        UsePageAsPinnedLink,
    }

    struct TabNameMenuItem {
        command: TabNameCommand,
        enabled: bool,
    }

    /// A pinned tab's link and what it keeps with it; empty strings for none.
    struct PinnedLink {
        link: String,
        wakes_at_link: bool,
        title: String,
    }

    struct TabShown {
        load_link: bool,
        pinned_link: PinnedLink,
    }

    // Pinned groups (lb_core::pinned_groups).
    struct PinnedGroupTab {
        asleep: bool,
        hidden_after_close: bool,
    }

    struct PinnedGroupClose {
        remove: bool,
        tabs: Vec<usize>,
    }

    struct PinnedItem {
        group: String,
        active: bool,
    }

    struct PinnedGroupInfo {
        id: String,
        collapsed: bool,
    }

    /// `header`: the group's index for a header row, -1 for an item row.
    struct PinnedRow {
        header: i64,
        item: usize,
        indented: bool,
        shown: bool,
    }

    enum PinnedGroupCommand {
        Rename,
        Color,
        Unpin,
        Ungroup,
        Close,
        Remove,
    }

    // Password managers (lb_core::password_manager).
    struct PasswordManagerInfo {
        name: String,
        about: String,
        extension_id: String,
        android_package: String,
    }

    struct PasswordManagerChoiceState {
        off_the_record: bool,
        offered_before: bool,
        /// Extension ids on desktop, app package names on Android.
        installed: Vec<String>,
    }

    enum PasswordManagerChoiceStep {
        Offer,
        Skip,
        Settled,
    }

    extern "Rust" {
        fn may_hold_unsaved_input(state: &PinnedCloseState) -> bool;
        fn decide_pinned_close(state: &PinnedCloseState) -> PinnedCloseOutcome;
        /// The index in `others`, or -1 when it is empty.
        fn choose_tab_to_show(others: &[TabToShowCandidate]) -> i64;
        /// `selected` -1: none.
        fn tab_restore_order(tab_count: usize, selected: i64) -> Vec<usize>;

        fn clean_tab_name(typed: &str) -> String;
        fn name_from_input(typed: &str, page_title: &str) -> String;
        fn tab_title_to_show(name: &str, pinned_title: &str, page_title: &str) -> String;
        fn has_own_title(name: &str, pinned_title: &str) -> bool;
        fn has_left_pinned_link(link: &str, page: &str) -> bool;
        fn settle_pinned_link(
            pinned_link: &PinnedLink,
            pinned: bool,
            committed: &str,
            page_is_blank: bool,
        ) -> PinnedLink;
        fn restored_pinned_link(
            pinned_link: &PinnedLink,
            pinned: bool,
            committed: &str,
            page_is_blank: bool,
            shown: bool,
        ) -> PinnedLink;
        fn take_pinned_title(
            pinned_link: &PinnedLink,
            committed: &str,
            page_title: &str,
        ) -> PinnedLink;
        fn with_pinned_link(pinned_link: &PinnedLink, link: &str) -> PinnedLink;
        fn after_pinned_sleep(pinned_link: &PinnedLink, current: &str) -> PinnedLink;
        fn on_tab_shown(pinned_link: &PinnedLink) -> TabShown;
        fn tab_name_menu_items(
            tabs: usize,
            pinned: bool,
            left_pinned_link: bool,
        ) -> Vec<TabNameMenuItem>;
        fn tab_menu_targets(
            menu_tab_selected: bool,
            selected: usize,
            selected_in_split: usize,
        ) -> usize;

        fn normalize_pinned_groups(groups: &Vec<String>) -> Vec<String>;
        fn group_after_move(
            own: &str,
            before: &str,
            after: &str,
            own_has_other_items: bool,
        ) -> String;
        fn restored_tab_group(saved: &str, before: &str, after: &str) -> String;
        fn decide_pinned_group_close(tabs: &[PinnedGroupTab]) -> PinnedGroupClose;
        fn new_pinned_group_position(groups: &Vec<String>, selected: &[bool]) -> usize;
        fn pinned_rows(items: &Vec<PinnedItem>, groups: &Vec<PinnedGroupInfo>) -> Vec<PinnedRow>;
        fn pinned_group_menu(all_tabs_asleep: bool) -> Vec<PinnedGroupCommand>;

        fn password_managers() -> Vec<PasswordManagerInfo>;
        fn password_manager_choice_step(
            state: &PasswordManagerChoiceState,
        ) -> PasswordManagerChoiceStep;
    }
}

fn close_state(s: &ffi::PinnedCloseState) -> tabs::PinnedCloseState {
    tabs::PinnedCloseState {
        user_gesture: s.user_gesture,
        closing_all: s.closing_all,
        pinned: s.pinned,
        split: s.split,
        asleep: s.asleep,
        shown: s.shown,
        hidden_by_earlier_close: s.hidden_by_earlier_close,
        sleep_pending: s.sleep_pending,
        asks_before_leaving: s.asks_before_leaving,
        had_user_edits: s.had_user_edits,
    }
}

fn may_hold_unsaved_input(state: &ffi::PinnedCloseState) -> bool {
    tabs::may_hold_unsaved_input(&close_state(state))
}

fn decide_pinned_close(state: &ffi::PinnedCloseState) -> ffi::PinnedCloseOutcome {
    match tabs::decide_pinned_close(&close_state(state)) {
        tabs::PinnedCloseOutcome::Close => ffi::PinnedCloseOutcome::Close,
        tabs::PinnedCloseOutcome::CloseAskingFirst => ffi::PinnedCloseOutcome::CloseAskingFirst,
        tabs::PinnedCloseOutcome::Sleep => ffi::PinnedCloseOutcome::Sleep,
        tabs::PinnedCloseOutcome::Hide => ffi::PinnedCloseOutcome::Hide,
        tabs::PinnedCloseOutcome::AlreadySleeping => ffi::PinnedCloseOutcome::AlreadySleeping,
    }
}

fn choose_tab_to_show(others: &[ffi::TabToShowCandidate]) -> i64 {
    let others: Vec<tabs::TabToShowCandidate> = others
        .iter()
        .map(|t| tabs::TabToShowCandidate {
            asleep: t.asleep,
            last_focused: t.last_focused,
            in_collapsed_group: t.in_collapsed_group,
        })
        .collect();
    tabs::choose_tab_to_show(&others).map_or(-1, |i| i as i64)
}

fn tab_restore_order(tab_count: usize, selected: i64) -> Vec<usize> {
    tabs::tab_restore_order(tab_count, usize::try_from(selected).ok())
}

fn clean_tab_name(typed: &str) -> String {
    tabs::clean_tab_name(typed)
}

fn name_from_input(typed: &str, page_title: &str) -> String {
    tabs::name_from_input(typed, page_title)
}

fn tab_title_to_show(name: &str, pinned_title: &str, page_title: &str) -> String {
    tabs::tab_title_to_show(name, pinned_title, page_title)
}

fn has_own_title(name: &str, pinned_title: &str) -> bool {
    tabs::has_own_title(name, pinned_title)
}

fn has_left_pinned_link(link: &str, page: &str) -> bool {
    tabs::has_left_pinned_link(link, page)
}

fn to_link(p: &ffi::PinnedLink) -> tabs::PinnedLink {
    tabs::PinnedLink {
        link: p.link.clone(),
        wakes_at_link: p.wakes_at_link,
        title: p.title.clone(),
    }
}

fn from_link(p: tabs::PinnedLink) -> ffi::PinnedLink {
    ffi::PinnedLink {
        link: p.link,
        wakes_at_link: p.wakes_at_link,
        title: p.title,
    }
}

fn settle_pinned_link(
    pinned_link: &ffi::PinnedLink,
    pinned: bool,
    committed: &str,
    page_is_blank: bool,
) -> ffi::PinnedLink {
    from_link(tabs::settle_pinned_link(
        to_link(pinned_link),
        pinned,
        committed,
        page_is_blank,
    ))
}

fn restored_pinned_link(
    pinned_link: &ffi::PinnedLink,
    pinned: bool,
    committed: &str,
    page_is_blank: bool,
    shown: bool,
) -> ffi::PinnedLink {
    from_link(tabs::restored_pinned_link(
        to_link(pinned_link),
        pinned,
        committed,
        page_is_blank,
        shown,
    ))
}

fn take_pinned_title(
    pinned_link: &ffi::PinnedLink,
    committed: &str,
    page_title: &str,
) -> ffi::PinnedLink {
    from_link(tabs::take_pinned_title(
        to_link(pinned_link),
        committed,
        page_title,
    ))
}

fn with_pinned_link(pinned_link: &ffi::PinnedLink, link: &str) -> ffi::PinnedLink {
    from_link(tabs::with_pinned_link(to_link(pinned_link), link))
}

fn after_pinned_sleep(pinned_link: &ffi::PinnedLink, current: &str) -> ffi::PinnedLink {
    from_link(tabs::after_pinned_sleep(to_link(pinned_link), current))
}

fn on_tab_shown(pinned_link: &ffi::PinnedLink) -> ffi::TabShown {
    let (load_link, pinned_link) = tabs::on_tab_shown(to_link(pinned_link));
    ffi::TabShown {
        load_link,
        pinned_link: from_link(pinned_link),
    }
}

fn tab_name_menu_items(
    tabs: usize,
    pinned: bool,
    left_pinned_link: bool,
) -> Vec<ffi::TabNameMenuItem> {
    tabs::tab_name_menu_items(tabs, pinned, left_pinned_link)
        .into_iter()
        .map(|item| ffi::TabNameMenuItem {
            command: match item.command {
                tabs::TabNameCommand::Rename => ffi::TabNameCommand::Rename,
                tabs::TabNameCommand::EditPinnedLink => ffi::TabNameCommand::EditPinnedLink,
                tabs::TabNameCommand::UsePageAsPinnedLink => {
                    ffi::TabNameCommand::UsePageAsPinnedLink
                }
            },
            enabled: item.enabled,
        })
        .collect()
}

fn tab_menu_targets(menu_tab_selected: bool, selected: usize, selected_in_split: usize) -> usize {
    tabs::tab_menu_targets(menu_tab_selected, selected, selected_in_split)
}

fn normalize_pinned_groups(groups: &Vec<String>) -> Vec<String> {
    pinned_groups::normalize_pinned_groups(groups.clone())
}

fn group_after_move(own: &str, before: &str, after: &str, own_has_other_items: bool) -> String {
    pinned_groups::group_after_move(own, before, after, own_has_other_items)
}

fn restored_tab_group(saved: &str, before: &str, after: &str) -> String {
    pinned_groups::restored_tab_group(saved, before, after)
}

fn decide_pinned_group_close(tabs: &[ffi::PinnedGroupTab]) -> ffi::PinnedGroupClose {
    let tabs: Vec<pinned_groups::PinnedGroupTab> = tabs
        .iter()
        .map(|t| pinned_groups::PinnedGroupTab {
            asleep: t.asleep,
            hidden_after_close: t.hidden_after_close,
        })
        .collect();
    let close = pinned_groups::decide_pinned_group_close(&tabs);
    ffi::PinnedGroupClose {
        remove: close.remove,
        tabs: close.tabs,
    }
}

fn new_pinned_group_position(groups: &Vec<String>, selected: &[bool]) -> usize {
    pinned_groups::new_pinned_group_position(groups, selected)
}

fn pinned_rows(
    items: &Vec<ffi::PinnedItem>,
    groups: &Vec<ffi::PinnedGroupInfo>,
) -> Vec<ffi::PinnedRow> {
    let items: Vec<pinned_groups::PinnedItem> = items
        .iter()
        .map(|i| pinned_groups::PinnedItem {
            group: i.group.clone(),
            active: i.active,
        })
        .collect();
    let groups: Vec<pinned_groups::PinnedGroupInfo> = groups
        .iter()
        .map(|g| pinned_groups::PinnedGroupInfo {
            id: g.id.clone(),
            collapsed: g.collapsed,
        })
        .collect();
    pinned_groups::pinned_rows(&items, &groups)
        .into_iter()
        .map(|r| ffi::PinnedRow {
            header: r.header.map_or(-1, |h| h as i64),
            item: r.item,
            indented: r.indented,
            shown: r.shown,
        })
        .collect()
}

fn pinned_group_menu(all_tabs_asleep: bool) -> Vec<ffi::PinnedGroupCommand> {
    pinned_groups::pinned_group_menu(all_tabs_asleep)
        .into_iter()
        .map(|c| match c {
            pinned_groups::PinnedGroupCommand::Rename => ffi::PinnedGroupCommand::Rename,
            pinned_groups::PinnedGroupCommand::Color => ffi::PinnedGroupCommand::Color,
            pinned_groups::PinnedGroupCommand::Unpin => ffi::PinnedGroupCommand::Unpin,
            pinned_groups::PinnedGroupCommand::Ungroup => ffi::PinnedGroupCommand::Ungroup,
            pinned_groups::PinnedGroupCommand::Close => ffi::PinnedGroupCommand::Close,
            pinned_groups::PinnedGroupCommand::Remove => ffi::PinnedGroupCommand::Remove,
        })
        .collect()
}

fn password_managers() -> Vec<ffi::PasswordManagerInfo> {
    password_manager::PASSWORD_MANAGERS
        .iter()
        .map(|m| ffi::PasswordManagerInfo {
            name: m.name.to_owned(),
            about: m.about.to_owned(),
            extension_id: m.extension_id.to_owned(),
            android_package: m.android_package.to_owned(),
        })
        .collect()
}

fn password_manager_choice_step(
    state: &ffi::PasswordManagerChoiceState,
) -> ffi::PasswordManagerChoiceStep {
    let state = password_manager::State {
        off_the_record: state.off_the_record,
        offered_before: state.offered_before,
        installed: state.installed.clone(),
    };
    match password_manager::next_step(&state) {
        password_manager::Step::Offer => ffi::PasswordManagerChoiceStep::Offer,
        password_manager::Step::Skip => ffi::PasswordManagerChoiceStep::Skip,
        password_manager::Step::Settled => ffi::PasswordManagerChoiceStep::Settled,
    }
}
