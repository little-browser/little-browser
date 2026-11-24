// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Space rules every platform shares (docs/ux.md §5): the defaults of a new space, colour
//! maths, which space a new window opens on, and how a space's saved tabs are ordered and
//! grouped. The shells keep the saved form (desktop: Local State) and their own types.

/// Space icons offered in menus, in the order new spaces take them.
pub const SPACE_ICONS: [&str; 8] = [
    "\u{1F3E0}",
    "\u{1F4BC}",
    "\u{1F3A8}",
    "\u{1F4DA}",
    "\u{1F9EA}",
    "\u{1F3AE}",
    "\u{2708}\u{FE0F}",
    "\u{1F331}",
];

/// A space colour offered in menus: a name and a hue in degrees.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SpaceColour {
    pub name: &'static str,
    pub hue: i32,
}

pub const SPACE_COLOURS: [SpaceColour; 8] = [
    SpaceColour {
        name: "Blue",
        hue: 222,
    },
    SpaceColour {
        name: "Teal",
        hue: 180,
    },
    SpaceColour {
        name: "Green",
        hue: 140,
    },
    SpaceColour {
        name: "Amber",
        hue: 40,
    },
    SpaceColour {
        name: "Orange",
        hue: 24,
    },
    SpaceColour {
        name: "Red",
        hue: 0,
    },
    SpaceColour {
        name: "Pink",
        hue: 330,
    },
    SpaceColour {
        name: "Purple",
        hue: 270,
    },
];

/// The first space's colour (blue).
pub const FIRST_SPACE_HUE: i32 = 222;

/// Golden-angle-ish step: consecutive spaces get clearly different colours.
const HUE_STEP: i32 = 137;

/// The hue of the next new space, given the last space's hue (none: there are no spaces yet).
pub fn next_space_hue(last_hue: Option<i32>) -> i32 {
    match last_hue {
        None => FIRST_SPACE_HUE,
        Some(hue) => (hue + HUE_STEP) % 360,
    }
}

/// The icon of the next new space, given how many spaces there are: they cycle through
/// [`SPACE_ICONS`].
pub fn next_space_icon(count: usize) -> &'static str {
    SPACE_ICONS[count % SPACE_ICONS.len()]
}

/// The name of the next new space: "Space 3" when there are two.
pub fn next_space_name(count: usize) -> String {
    format!("Space {}", count + 1)
}

/// Any number of degrees as a hue in [0, 360).
pub fn normalize_hue(hue: i32) -> i32 {
    hue.rem_euclid(360)
}

/// The shorter way round the colour wheel between two hues: 0 to 180.
pub fn hue_distance(a: i32, b: i32) -> i32 {
    let d = (normalize_hue(a) - normalize_hue(b)).abs();
    d.min(360 - d)
}

/// The index into `hues` of the hue closest to `hue`; the first of equally close ones. None for
/// an empty list.
pub fn nearest_hue(hues: &[i32], hue: i32) -> Option<usize> {
    let mut best: Option<(usize, i32)> = None;
    for (i, &h) in hues.iter().enumerate() {
        let d = hue_distance(h, hue);
        if best.is_none_or(|(_, bd)| d < bd) {
            best = Some((i, d));
        }
    }
    best.map(|(i, _)| i)
}

/// The index into [`SPACE_COLOURS`] of the colour closest to `hue` (the new-space dialog's
/// preset).
pub fn nearest_space_colour(hue: i32) -> usize {
    let hues: Vec<i32> = SPACE_COLOURS.iter().map(|c| c.hue).collect();
    nearest_hue(&hues, hue).unwrap_or(0)
}

/// A space as a window chooser sees it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct WindowSpace {
    pub id: String,
    pub profile: String,
    /// A window shows it already.
    pub shown: bool,
}

/// Which space a new window of `profile` opens on: the profile's last active space, else its
/// first space no window shows; during session restore (the window is put on its saved space
/// right after), any of its spaces. None means "create a new space".
pub fn choose_space_for_window(
    spaces: &[WindowSpace],
    profile: &str,
    last_active_id: &str,
    restoring_session: bool,
) -> Option<String> {
    let mine = || spaces.iter().filter(|s| s.profile == profile);
    mine()
        .find(|s| s.id == last_active_id && !s.shown)
        .or_else(|| mine().find(|s| !s.shown))
        .or_else(|| {
            if restoring_session {
                mine().next()
            } else {
                None
            }
        })
        .map(|s| s.id.clone())
}

/// Where a tab of a merged list comes from: `added` (imported) or the space's own list.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MergeSlot {
    pub added: bool,
    pub index: usize,
}

/// The order of a space's saved tabs with imported ones added (docs/ux.md §13), from whether
/// each tab is pinned: its pinned tabs, the imported pinned tabs, its other tabs, the imported
/// other tabs. The shell keeps the space's active tab and makes none of the added ones active.
pub fn merge_order(existing_pinned: &[bool], added_pinned: &[bool]) -> Vec<MergeSlot> {
    let mut order = Vec::with_capacity(existing_pinned.len() + added_pinned.len());
    for pinned in [true, false] {
        for (added, list) in [(false, existing_pinned), (true, added_pinned)] {
            for (index, &p) in list.iter().enumerate() {
                if p == pinned {
                    order.push(MergeSlot { added, index });
                }
            }
        }
    }
    order
}

/// Which saved tab to show first: the one saved as active, else the first. 0 for no tabs.
pub fn active_tab(active: &[bool]) -> usize {
    active.iter().position(|&a| a).unwrap_or(0)
}

/// Runs of neighbouring tabs with the same non-empty id (a tab group, or a split view): the
/// indexes of each run's tabs, in order.
pub fn runs(ids: &[String]) -> Vec<Vec<usize>> {
    let mut runs: Vec<Vec<usize>> = Vec::new();
    for (i, id) in ids.iter().enumerate() {
        if id.is_empty() {
            continue;
        }
        match runs.last_mut() {
            Some(run) if i > 0 && ids[i - 1] == *id => run.push(i),
            _ => runs.push(vec![i]),
        }
    }
    runs
}

/// The split views among saved tabs, from their split ids: runs of exactly two tabs (anything
/// else is not a split view and is left out).
pub fn split_runs(ids: &[String]) -> Vec<Vec<usize>> {
    runs(ids).into_iter().filter(|run| run.len() == 2).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ids(list: &[&str]) -> Vec<String> {
        list.iter().map(|s| s.to_string()).collect()
    }

    fn space(id: &str, profile: &str, shown: bool) -> WindowSpace {
        WindowSpace {
            id: id.into(),
            profile: profile.into(),
            shown,
        }
    }

    #[test]
    fn new_space_defaults() {
        assert_eq!(next_space_hue(None), FIRST_SPACE_HUE);
        assert_eq!(next_space_hue(Some(222)), 359);
        assert_eq!(next_space_hue(Some(300)), 77);
        assert_eq!(next_space_icon(0), SPACE_ICONS[0]);
        assert_eq!(next_space_icon(1), SPACE_ICONS[1]);
        assert_eq!(next_space_icon(SPACE_ICONS.len()), SPACE_ICONS[0]);
        assert_eq!(next_space_name(0), "Space 1");
        assert_eq!(next_space_name(2), "Space 3");
    }

    #[test]
    fn consecutive_spaces_get_different_colours() {
        let mut hue = next_space_hue(None);
        for _ in 0..8 {
            let next = next_space_hue(Some(hue));
            assert!(hue_distance(hue, next) >= 40, "{hue} → {next}");
            hue = next;
        }
    }

    #[test]
    fn hue_maths() {
        assert_eq!(normalize_hue(0), 0);
        assert_eq!(normalize_hue(360), 0);
        assert_eq!(normalize_hue(725), 5);
        assert_eq!(normalize_hue(-30), 330);
        assert_eq!(normalize_hue(-390), 330);
        assert_eq!(hue_distance(10, 350), 20);
        assert_eq!(hue_distance(350, 10), 20);
        assert_eq!(hue_distance(0, 180), 180);
        assert_eq!(hue_distance(-10, 10), 20);
        assert_eq!(hue_distance(90, 90), 0);
    }

    #[test]
    fn nearest_colours() {
        assert_eq!(
            SPACE_COLOURS[nearest_space_colour(FIRST_SPACE_HUE)].name,
            "Blue"
        );
        assert_eq!(SPACE_COLOURS[nearest_space_colour(355)].name, "Red");
        assert_eq!(SPACE_COLOURS[nearest_space_colour(-5)].name, "Red");
        assert_eq!(SPACE_COLOURS[nearest_space_colour(185)].name, "Teal");
        assert_eq!(SPACE_COLOURS[nearest_space_colour(300)].name, "Pink");
        assert_eq!(nearest_hue(&[], 10), None);
        // Equally close: the first.
        assert_eq!(nearest_hue(&[0, 20], 10), Some(0));
    }

    #[test]
    fn window_opens_on_the_last_active_space() {
        let spaces = [
            space("a", "P", false),
            space("b", "P", false),
            space("c", "Q", false),
        ];
        assert_eq!(
            choose_space_for_window(&spaces, "P", "b", false).as_deref(),
            Some("b")
        );
    }

    #[test]
    fn window_skips_shown_and_other_profiles_spaces() {
        let spaces = [
            space("a", "P", true),
            space("b", "Q", false),
            space("c", "P", false),
        ];
        // The last active one is shown: the first unshown space of the profile.
        assert_eq!(
            choose_space_for_window(&spaces, "P", "a", false).as_deref(),
            Some("c")
        );
        // Another profile's last active space doesn't count.
        assert_eq!(
            choose_space_for_window(&spaces, "P", "b", false).as_deref(),
            Some("c")
        );
    }

    #[test]
    fn window_with_every_space_shown() {
        let spaces = [
            space("a", "P", true),
            space("b", "P", true),
            space("c", "Q", false),
        ];
        assert_eq!(choose_space_for_window(&spaces, "P", "a", false), None);
        // Restoring a session: any of the profile's spaces (the window moves right after).
        assert_eq!(
            choose_space_for_window(&spaces, "P", "", true).as_deref(),
            Some("a")
        );
        // A profile without spaces.
        assert_eq!(choose_space_for_window(&spaces, "R", "", true), None);
        assert_eq!(choose_space_for_window(&[], "P", "", true), None);
    }

    #[test]
    fn merged_tabs_keep_pinned_first() {
        let order = merge_order(&[true, false, true], &[false, true]);
        let slots: Vec<_> = order.iter().map(|s| (s.added, s.index)).collect();
        assert_eq!(
            slots,
            [(false, 0), (false, 2), (true, 1), (false, 1), (true, 0)]
        );
        assert!(merge_order(&[], &[]).is_empty());
        assert_eq!(
            merge_order(&[], &[false]),
            [MergeSlot {
                added: true,
                index: 0
            }]
        );
    }

    #[test]
    fn active_tab_is_the_saved_one_or_the_first() {
        assert_eq!(active_tab(&[false, true, true]), 1);
        assert_eq!(active_tab(&[false, false]), 0);
        assert_eq!(active_tab(&[]), 0);
    }

    #[test]
    fn groups_are_runs_of_neighbours() {
        assert_eq!(
            runs(&ids(&["", "g", "g", "", "g", "h", "h"])),
            [vec![1, 2], vec![4], vec![5, 6]]
        );
        assert!(runs(&[]).is_empty());
        assert!(runs(&ids(&["", ""])).is_empty());
        assert_eq!(runs(&ids(&["g"])), [vec![0]]);
    }

    #[test]
    fn splits_are_runs_of_two() {
        assert_eq!(
            split_runs(&ids(&["s", "s", "", "t", "u", "u", "u"])),
            [vec![0, 1]]
        );
        assert!(split_runs(&ids(&["s"])).is_empty());
    }
}
