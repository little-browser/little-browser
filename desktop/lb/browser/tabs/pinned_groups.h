// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_TABS_PINNED_GROUPS_H_
#define LB_BROWSER_TABS_PINNED_GROUPS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lb {

// A pinned group (docs/ux.md §6.1): a named, coloured, collapsible run of pinned tabs. Chromium
// can't group pinned tabs, so the sidebar keeps these itself, over Chromium's pinned tabs.
struct PinnedGroup {
  std::string id;
  std::u16string title;
  int color = 0;  // tab_groups::TabGroupColorId
  bool collapsed = false;
};

// A pinned group tab's group in Chromium's session (tab extra data under this key), so the
// group comes back when the window is restored. A tab in no group has empty data.
inline constexpr char kPinnedGroupSessionKey[] = "lb_pinned_group";
std::string PinnedGroupToSessionData(const PinnedGroup& group);
// The group in session data written by PinnedGroupToSessionData; nullopt for empty or bad data.
std::optional<PinnedGroup> PinnedGroupFromSessionData(std::string_view data);

// Keeps each pinned group one unbroken run. `groups` has one entry per item of the pinned area,
// in order (a tab, or a split view, which is grouped whole): the id of its group, or empty for
// none. Loose items lying between two items of the same group join it, as a tab inserted inside
// a group does; then each group keeps its first longest run, and its items elsewhere leave it.
std::vector<std::string> NormalizePinnedGroups(std::vector<std::string> groups);

// The group of an item of the pinned area that just moved there or was inserted (docs/ux.md
// §6.1), from `own`, its group until now, and the groups of the items now before and after it
// (empty: in none, or no item there). Between two items of one group it joins that group. Next to
// an item of its own group it stays in it, and so it does when no other item of its group is in
// the tab strip (the first of a group to come back with its space). Anywhere else it leaves.
std::string GroupAfterMove(const std::string& own,
                           const std::string& before,
                           const std::string& after,
                           bool own_has_other_items);

// The group of a pinned tab that a tab restore (Reopen Closed Tab) brings back (docs/ux.md §6.1):
// `saved`, the group it had when it closed (empty: none), unless it came back between two items
// of another group (`before` and `after`, as for lb::GroupAfterMove): then it is in that group,
// as an inserted tab is, so no group is split.
std::string RestoredTabGroup(const std::string& saved,
                             const std::string& before,
                             const std::string& after);

// A vertical span in the sidebar, top inclusive, bottom exclusive.
struct Span {
  int top = 0;
  int bottom = 0;
};

// Whether a tab dragged over a pinned group is in it (docs/ux.md §6.1), as Chromium decides for
// today groups. `group` runs from the top of its header to the bottom of its last tab; for the
// group the dragged tab is in, its own place counts as one of those tabs, so a tab dragged in place
// stays in. A tab outside joins an expanded group once it overlaps it by 40% of the header's
// height; a tab inside stays in until it overlaps by less than its own height less 10% of the
// header's height.
bool DraggedTabInPinnedGroup(Span dragged,
                             Span group,
                             int header_height,
                             bool collapsed,
                             bool was_in);

// A pinned group shown in the sidebar while a tab is dragged, as lb::DraggedPinnedGroup weighs it.
struct DraggedOverGroup {
  std::string id;
  Span span;  // from its header's top to its last tab's bottom (lb::DraggedTabInPinnedGroup)
  bool collapsed = false;
};

// Which pinned group a dragged tab is in (docs/ux.md §6.1), or empty for none. `now` is the group
// it is in at the moment, `home` the group it was in when the drag started, and `home_whole` says
// the dragged tabs were all of `home`'s tabs: such a group goes along with them. So a tab in its
// whole home group stays in it. Otherwise the group it is in keeps it while
// lb::DraggedTabInPinnedGroup says so; else the highest other group it overlaps enough takes it;
// else it is in its whole home group again, or in none. (Passing between two tabs of a group puts
// it in that group, lb::GroupAfterMove; leaving it, a tab of a whole group goes back home.)
std::string DraggedPinnedGroup(Span dragged,
                               std::vector<DraggedOverGroup> groups,
                               int header_height,
                               const std::string& now,
                               const std::string& home,
                               bool home_whole);

// Where a whole group dropped in an area of the sidebar goes (docs/ux.md §6.1), as the number of
// the area's other tabs before it. `tab_middles` are the middles of those tabs' rows, top to
// bottom, and `tab_groups` their groups (empty: none). It goes before the first tab whose middle
// is below `y`, but never inside another group: a drop between two tabs of one group moves past
// that group's last tab.
size_t GroupDropIndex(const std::vector<int>& tab_middles,
                      const std::vector<std::string>& tab_groups,
                      int y);

// Where the line showing a group's drop place goes (lb::GroupDropIndex gave `index`): the top of
// the row it goes before, or below the lowest row, or `empty_y` in an area with no rows. `rows`
// are the area's tab rows in tab order; a row that starts a group begins at its header, and a tab
// hidden in a collapsed group has its group's header as its row.
int GroupDropLineY(const std::vector<Span>& rows, size_t index, int empty_y);

// What dropping a whole group does (docs/ux.md §6.1), from whether it is a pinned group and where
// it is dropped: `y` against the middle of the "+ New Tab" row (none when that row isn't shown).
// Above the middle a pinned group moves among the pinned tabs and a today group becomes a pinned
// group there. Below it a pinned group becomes a today group, and a today group stays where
// Chromium put it.
enum class GroupDrop {
  kNothing,
  kMovePinned,  // a pinned group moves among the pinned tabs
  kUnpin,       // a pinned group becomes a today group
  kPin,         // a today group becomes a pinned group
};
GroupDrop DecideGroupDrop(bool pinned_group, std::optional<int> new_tab_middle, int y);

// A pinned group's tab, as the group's Close sees it.
struct PinnedGroupTab {
  bool asleep = false;
  // A close only hid it, as its page may hold unsaved input: the next close really closes it.
  bool hidden_after_close = false;
};

// What a pinned group's Close does (docs/ux.md §6.1). While any of its tabs is awake, it puts
// those to sleep, and a tab only hidden by an earlier close stays hidden. Once every tab sleeps or
// is hidden, it is Remove: the group goes with all its tabs. `tabs` are the indexes it acts on.
struct PinnedGroupClose {
  bool remove = false;
  std::vector<size_t> tabs;
};
PinnedGroupClose DecidePinnedGroupClose(const std::vector<PinnedGroupTab>& tabs);

// Where a new pinned group made of some of the pinned area's items goes (docs/ux.md §6.1), as the
// number of the other items before it. `groups` are the items' groups once the new group's items
// have left theirs (empty: none), and `selected` marks the new group's items. It goes where its
// first item is, but never inside another group: there it moves past that group's last item.
size_t NewPinnedGroupPosition(const std::vector<std::string>& groups,
                              const std::vector<bool>& selected);

// One item of the pinned area, as the sidebar lays it out.
struct PinnedItem {
  std::string group;  // empty: in no group
  bool active = false;
};

// One row of the pinned area: a group's header, or an item.
struct PinnedRow {
  std::optional<size_t> header;  // set for a header: the group's index in `groups`
  size_t item = 0;               // for an item row: its index in `items`
  bool indented = false;         // an item inside a group
  bool shown = true;             // false for an item hidden in a collapsed group
};

// The rows of the pinned area (docs/ux.md §6.1): each group's header before its first item, and
// every item, inside a group indented. A collapsed group shows only the active tab, if it holds
// it. `items` must already be normalized; items naming a group not in `groups` count as loose.
std::vector<PinnedRow> PinnedRows(const std::vector<PinnedItem>& items,
                                  const std::vector<PinnedGroup>& groups);

// The menu of a pinned group (docs/ux.md §6.1), in order. Close puts every tab in it to sleep;
// once they all sleep, it is Remove instead (lb::DecidePinnedGroupClose).
enum class PinnedGroupCommand {
  kRename,
  kColor,  // a submenu of the group colours
  kUnpin,
  kUngroup,
  kClose,
  kRemove,
};
std::vector<PinnedGroupCommand> PinnedGroupMenu(bool all_tabs_asleep);

}  // namespace lb

#endif  // LB_BROWSER_TABS_PINNED_GROUPS_H_
