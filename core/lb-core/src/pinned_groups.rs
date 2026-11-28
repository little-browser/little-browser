// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Pinned groups (docs/ux.md §6.1): a named, coloured, collapsible run of pinned tabs, kept by
//! the shell over the browser's pinned tabs, which the engine can't group. The rules here work on
//! the pinned area's items in order (a tab, or a split view, which is grouped whole), each with
//! the id of its group, empty for none.

use std::collections::HashMap;

/// Keeps each pinned group one unbroken run. Loose items lying between two items of the same group
/// join it, as a tab inserted inside a group does; then each group keeps its first longest run,
/// and its items elsewhere leave it.
pub fn normalize_pinned_groups(mut groups: Vec<String>) -> Vec<String> {
    // Loose items between two items of one group join it.
    let mut i = 0;
    while i < groups.len() {
        if !groups[i].is_empty() {
            i += 1;
            continue;
        }
        let mut end = i;
        while end < groups.len() && groups[end].is_empty() {
            end += 1;
        }
        if i > 0 && end < groups.len() && groups[i - 1] == groups[end] {
            let group = groups[end].clone();
            groups[i..end].fill(group);
        }
        i = end;
    }
    // Each group keeps its first longest run: [start, end) of its items.
    let mut kept: HashMap<String, (usize, usize)> = HashMap::new();
    let mut i = 0;
    while i < groups.len() {
        let mut end = i + 1;
        while end < groups.len() && groups[end] == groups[i] {
            end += 1;
        }
        if !groups[i].is_empty() {
            let run = kept.entry(groups[i].clone()).or_insert((i, end));
            if end - i > run.1 - run.0 {
                *run = (i, end);
            }
        }
        i = end;
    }
    for (i, group) in groups.iter_mut().enumerate() {
        if let Some(&(start, end)) = kept.get(group.as_str()) {
            if i < start || i >= end {
                group.clear();
            }
        }
    }
    groups
}

/// The group of an item of the pinned area that just moved there or was inserted, from `own`, its
/// group until now, and the groups of the items now before and after it (empty: in none, or no
/// item there). Between two items of one group it joins that group. Next to an item of its own
/// group it stays in it, and so it does when no other item of its group is in the window (the
/// first of a group to come back with its space). Anywhere else it leaves.
pub fn group_after_move(own: &str, before: &str, after: &str, own_has_other_items: bool) -> String {
    if !before.is_empty() && before == after {
        before.to_owned()
    } else if !own.is_empty() && (own == before || own == after || !own_has_other_items) {
        own.to_owned()
    } else {
        String::new()
    }
}

/// The group of a pinned tab that reopening a closed tab brings back: `saved`, the group it had
/// when it closed (empty: none), unless it came back between two items of another group (`before`
/// and `after`, as for [`group_after_move`]): then it is in that group, as an inserted tab is, so
/// no group is split.
pub fn restored_tab_group(saved: &str, before: &str, after: &str) -> String {
    if !before.is_empty() && before == after && before != saved {
        before.to_owned()
    } else {
        saved.to_owned()
    }
}

/// A pinned group's tab, as the group's Close sees it.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct PinnedGroupTab {
    pub asleep: bool,
    /// A close only hid it, as its page may hold unsaved input: the next close really closes it.
    pub hidden_after_close: bool,
}

/// What a pinned group's Close does: `tabs` are the indexes it acts on.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct PinnedGroupClose {
    /// The group goes, with all its tabs.
    pub remove: bool,
    pub tabs: Vec<usize>,
}

/// What a pinned group's Close does. While any of its tabs is awake, it puts those to sleep, and a
/// tab only hidden by an earlier close stays hidden. Once every tab sleeps or is hidden, it is
/// Remove: the group goes with all its tabs.
pub fn decide_pinned_group_close(tabs: &[PinnedGroupTab]) -> PinnedGroupClose {
    let resting = |tab: &PinnedGroupTab| tab.asleep || tab.hidden_after_close;
    let remove = tabs.iter().all(resting);
    PinnedGroupClose {
        remove,
        tabs: (0..tabs.len())
            .filter(|&i| remove || !resting(&tabs[i]))
            .collect(),
    }
}

/// Past the end of the run of `groups[index]`'s group when `index` falls inside a group (between
/// two of its items); `index` otherwise.
fn past_group_at(groups: &[String], mut index: usize) -> usize {
    while index > 0
        && index < groups.len()
        && !groups[index].is_empty()
        && groups[index] == groups[index - 1]
    {
        index += 1;
    }
    index
}

/// Where a new pinned group made of some of the pinned area's items goes, as the number of the
/// other items before it. `groups` are the items' groups once the new group's items have left
/// theirs (empty: none), and `selected` marks the new group's items. It goes where its first item
/// is, but never inside another group: there it moves past that group's last item.
pub fn new_pinned_group_position(groups: &[String], selected: &[bool]) -> usize {
    let mut others = Vec::new();
    let mut position = None;
    for (i, group) in groups.iter().enumerate() {
        if selected.get(i).copied().unwrap_or(false) {
            position.get_or_insert(others.len());
        } else {
            others.push(group.clone());
        }
    }
    past_group_at(&others, position.unwrap_or(others.len()))
}

/// One item of the pinned area, as the shell lays it out.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct PinnedItem {
    /// Empty: in no group.
    pub group: String,
    pub active: bool,
}

/// A pinned group, as the pinned area's rows need it.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct PinnedGroupInfo {
    pub id: String,
    pub collapsed: bool,
}

/// One row of the pinned area: a group's header, or an item.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PinnedRow {
    /// Set for a header: the group's index in `groups`.
    pub header: Option<usize>,
    /// For an item row: its index in `items`.
    pub item: usize,
    /// An item inside a group.
    pub indented: bool,
    /// False for an item hidden in a collapsed group.
    pub shown: bool,
}

/// The rows of the pinned area: each group's header before its first item, and every item, inside
/// a group indented. A collapsed group shows only the active tab, if it holds it. `items` must
/// already be normalized ([`normalize_pinned_groups`]); items naming a group not in `groups` count
/// as loose.
pub fn pinned_rows(items: &[PinnedItem], groups: &[PinnedGroupInfo]) -> Vec<PinnedRow> {
    let mut rows = Vec::new();
    let mut current = None; // the group of the previous item
    for (i, item) in items.iter().enumerate() {
        let group = if item.group.is_empty() {
            None
        } else {
            groups.iter().position(|g| g.id == item.group)
        };
        if group.is_some() && group != current {
            // A header is always shown, collapsed or not.
            rows.push(PinnedRow {
                header: group,
                item: 0,
                indented: false,
                shown: true,
            });
        }
        current = group;
        rows.push(PinnedRow {
            header: None,
            item: i,
            indented: group.is_some(),
            shown: group.is_none_or(|g| !groups[g].collapsed || item.active),
        });
    }
    rows
}

/// The menu of a pinned group, in order.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PinnedGroupCommand {
    Rename,
    /// A submenu of the group colours.
    Color,
    Unpin,
    Ungroup,
    /// Puts every tab in it to sleep ([`decide_pinned_group_close`]).
    Close,
    /// Close, once all its tabs sleep: the group goes with its tabs.
    Remove,
}

pub fn pinned_group_menu(all_tabs_asleep: bool) -> Vec<PinnedGroupCommand> {
    use PinnedGroupCommand::*;
    vec![
        Rename,
        Color,
        Unpin,
        Ungroup,
        if all_tabs_asleep { Remove } else { Close },
    ]
}

#[cfg(test)]
mod tests {
    use super::*;

    fn strings(groups: &[&str]) -> Vec<String> {
        groups.iter().map(|&g| g.to_owned()).collect()
    }

    fn normalized(groups: &[&str]) -> Vec<String> {
        normalize_pinned_groups(strings(groups))
    }

    #[test]
    fn normal_groups_stay() {
        assert_eq!(normalized(&[]), strings(&[]));
        assert_eq!(
            normalized(&["", "a", "a", "", "b"]),
            strings(&["", "a", "a", "", "b"])
        );
    }

    #[test]
    fn loose_items_inside_a_group_join_it() {
        assert_eq!(
            normalized(&["a", "", "", "a"]),
            strings(&["a", "a", "a", "a"])
        );
        assert_eq!(normalized(&["a", "", "b"]), strings(&["a", "", "b"]));
        assert_eq!(normalized(&["", "", "a"]), strings(&["", "", "a"]));
        assert_eq!(normalized(&["a", "", ""]), strings(&["a", "", ""]));
    }

    #[test]
    fn a_split_group_keeps_its_first_longest_run() {
        assert_eq!(
            normalized(&["a", "b", "a", "a"]),
            strings(&["", "b", "a", "a"])
        );
        assert_eq!(
            normalized(&["a", "a", "b", "a"]),
            strings(&["a", "a", "b", ""])
        );
        assert_eq!(normalized(&["a", "b", "a"]), strings(&["a", "b", ""]));
    }

    #[test]
    fn group_after_a_move() {
        // Between two items of one group: joins it.
        assert_eq!(group_after_move("", "a", "a", false), "a");
        assert_eq!(group_after_move("b", "a", "a", true), "a");
        // Next to its own group: stays.
        assert_eq!(group_after_move("a", "a", "", true), "a");
        assert_eq!(group_after_move("a", "", "a", true), "a");
        // The only item of its group here: stays.
        assert_eq!(group_after_move("a", "", "", false), "a");
        assert_eq!(group_after_move("a", "b", "", false), "a");
        // Anywhere else: leaves.
        assert_eq!(group_after_move("a", "b", "", true), "");
        assert_eq!(group_after_move("a", "", "", true), "");
        assert_eq!(group_after_move("", "", "", false), "");
        assert_eq!(group_after_move("", "a", "b", false), "");
    }

    #[test]
    fn restored_tab_group_keeps_its_group_unless_inside_another() {
        assert_eq!(restored_tab_group("a", "", ""), "a");
        assert_eq!(restored_tab_group("a", "a", "a"), "a");
        assert_eq!(restored_tab_group("a", "b", "b"), "b");
        assert_eq!(restored_tab_group("", "b", "b"), "b");
        assert_eq!(restored_tab_group("a", "b", "c"), "a");
        assert_eq!(restored_tab_group("", "", ""), "");
    }

    fn group_tab(asleep: bool, hidden_after_close: bool) -> PinnedGroupTab {
        PinnedGroupTab {
            asleep,
            hidden_after_close,
        }
    }

    #[test]
    fn group_close_sleeps_awake_tabs() {
        assert_eq!(
            decide_pinned_group_close(&[
                group_tab(false, false),
                group_tab(true, false),
                group_tab(false, true)
            ]),
            PinnedGroupClose {
                remove: false,
                tabs: vec![0]
            }
        );
    }

    #[test]
    fn group_close_removes_once_all_rest() {
        assert_eq!(
            decide_pinned_group_close(&[group_tab(true, false), group_tab(false, true)]),
            PinnedGroupClose {
                remove: true,
                tabs: vec![0, 1]
            }
        );
        assert_eq!(
            decide_pinned_group_close(&[]),
            PinnedGroupClose {
                remove: true,
                tabs: vec![]
            }
        );
    }

    #[test]
    fn new_group_goes_where_its_first_item_is() {
        assert_eq!(
            new_pinned_group_position(&strings(&["", "", ""]), &[false, true, true]),
            1
        );
        assert_eq!(
            new_pinned_group_position(&strings(&["", "", ""]), &[true, false, true]),
            0
        );
        // Selected items past the end of `selected`'s length count as not selected.
        assert_eq!(new_pinned_group_position(&strings(&["", ""]), &[]), 2);
    }

    #[test]
    fn new_group_never_goes_inside_another() {
        assert_eq!(
            new_pinned_group_position(&strings(&["a", "", "a", "a"]), &[false, true, false, false]),
            3
        );
        assert_eq!(
            new_pinned_group_position(&strings(&["a", "", "b"]), &[false, true, false]),
            1
        );
    }

    fn item(group: &str, active: bool) -> PinnedItem {
        PinnedItem {
            group: group.to_owned(),
            active,
        }
    }

    fn info(id: &str, collapsed: bool) -> PinnedGroupInfo {
        PinnedGroupInfo {
            id: id.to_owned(),
            collapsed,
        }
    }

    fn header(group: usize) -> PinnedRow {
        PinnedRow {
            header: Some(group),
            item: 0,
            indented: false,
            shown: true,
        }
    }

    fn row(item: usize, indented: bool, shown: bool) -> PinnedRow {
        PinnedRow {
            header: None,
            item,
            indented,
            shown,
        }
    }

    #[test]
    fn rows_have_a_header_before_each_group() {
        let items = [
            item("", false),
            item("a", false),
            item("a", false),
            item("b", false),
        ];
        let groups = [info("a", false), info("b", false)];
        assert_eq!(
            pinned_rows(&items, &groups),
            vec![
                row(0, false, true),
                header(0),
                row(1, true, true),
                row(2, true, true),
                header(1),
                row(3, true, true),
            ]
        );
    }

    #[test]
    fn a_collapsed_group_shows_only_its_active_tab() {
        let items = [item("a", false), item("a", true), item("a", false)];
        assert_eq!(
            pinned_rows(&items, &[info("a", true)]),
            vec![
                header(0),
                row(0, true, false),
                row(1, true, true),
                row(2, true, false)
            ]
        );
    }

    #[test]
    fn items_of_an_unknown_group_are_loose() {
        assert_eq!(
            pinned_rows(&[item("x", false)], &[]),
            vec![row(0, false, true)]
        );
        assert!(pinned_rows(&[], &[info("a", false)]).is_empty());
    }

    #[test]
    fn group_menu() {
        use PinnedGroupCommand::*;
        assert_eq!(
            pinned_group_menu(false),
            vec![Rename, Color, Unpin, Ungroup, Close]
        );
        assert_eq!(
            pinned_group_menu(true),
            vec![Rename, Color, Unpin, Ungroup, Remove]
        );
    }
}
