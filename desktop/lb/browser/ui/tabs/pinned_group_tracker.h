// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_TABS_PINNED_GROUP_TRACKER_H_
#define LB_BROWSER_UI_TABS_PINNED_GROUP_TRACKER_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "lb/browser/tabs/pinned_groups.h"

class BrowserWindowInterface;
class TabStripModel;

namespace content {
class WebContents;
}  // namespace content

namespace lb {

// The pinned groups of one window (docs/ux.md §6.1), in every space it holds. Chromium can't group
// pinned tabs, so this keeps which pinned tab is in which group, over Chromium's pinned tabs, and
// keeps each group one unbroken run as the tab strip changes (lb::NormalizePinnedGroups). A tab
// keeps its group while its space is parked. Each member's group is also written to Chromium's
// session, so a restored window gets its groups back (patches/lb/pinned-groups.patch).
class PinnedGroupTracker {
 public:
  // A tab's group, handed from one window's tracker to another's with a parked space.
  using Membership = std::pair<tabs::TabHandle, PinnedGroup>;

  // `on_changed` runs after any change to the groups or who is in them.
  PinnedGroupTracker(BrowserWindowInterface* browser, base::RepeatingClosure on_changed);
  PinnedGroupTracker(const PinnedGroupTracker&) = delete;
  PinnedGroupTracker& operator=(const PinnedGroupTracker&) = delete;
  ~PinnedGroupTracker();

  // The group `tab` is in, or null.
  const PinnedGroup* GroupOf(const tabs::TabInterface* tab) const;
  const PinnedGroup* Find(const std::string& id) const;
  // The group's tabs in the tab strip, in order.
  std::vector<tabs::TabInterface*> TabsOf(const std::string& id) const;
  // Every group, also those of parked spaces.
  std::vector<std::string> GroupIds() const;
  // A new group from Add to New Group starts with its name being edited: the sidebar takes the
  // request once its header is there.
  void RequestRename(const std::string& id) { rename_requests_.insert(id); }
  bool TakeRenameRequest(const std::string& id) { return rename_requests_.erase(id) > 0; }

  // Makes a group of `tabs` (pinned tabs of this window; a split view comes along whole). They
  // leave the groups they were in and move next to each other, in their order, where the first
  // of them is but never inside another group (lb::NewPinnedGroupPosition). An empty `group.id`
  // gets a new one. Returns the group's id, or empty if none of `tabs` is pinned.
  std::string Create(const std::vector<tabs::TabInterface*>& tabs, PinnedGroup group);
  void SetTitle(const std::string& id, const std::u16string& title);
  void SetColor(const std::string& id, int color);
  void SetCollapsed(const std::string& id, bool collapsed);
  // Puts `tab` (and the other tab of its split view) into group `id`, or out of any group for an
  // empty `id`, as a drag does (lb::DraggedTabInPinnedGroup). The next update keeps the groups
  // unbroken.
  void SetGroupOf(tabs::TabInterface* tab, const std::string& id);
  // The same for a tab being dragged: the group it had before the drag is remembered, and comes
  // back if the drag is cancelled.
  void SetDraggedGroupOf(tabs::TabInterface* tab, const std::string& id);
  // The id of the group `tab` was in when the tab drag under way started (empty: none), as
  // lb::DraggedPinnedGroup needs it.
  std::string DragStartGroupOf(const tabs::TabInterface* tab) const;
  // A tab drag ended: cancelled (Esc), the tabs it moved in or out of groups get back the groups
  // they had before it.
  void EndTabDrag(bool cancelled);
  // Takes `tabs` out of their pinned groups (Tab > Group Tab on tabs that are in groups); a tab in
  // the middle of a group moves out past its last tab.
  void RemoveFromGroups(const std::vector<tabs::TabInterface*>& tabs);

  // The group's menu (lb::PinnedGroupMenu).
  // Its tabs stay pinned, in no group.
  void Ungroup(const std::string& id);
  // Unpins its tabs into a today group (a Chromium tab group) with the same title, colour and
  // collapsed state (expanded when it holds the shown tab), after `today_tabs_before` of the today
  // tabs (first, by default).
  void Unpin(const std::string& id, size_t today_tabs_before = 0);
  // Moves the group's tabs, in order, after `pinned_tabs_before` of the other pinned tabs.
  void Move(const std::string& id, size_t pinned_tabs_before);
  // Puts the group's awake tabs to sleep together, as closing a pinned tab does; once they all
  // sleep, removes the group with its tabs (lb::DecidePinnedGroupClose).
  void Close(const std::string& id);
  // Whether the menu's Close is Remove: every tab sleeps, or is only hidden as its page may hold
  // unsaved input.
  bool AllTabsAsleep(const std::string& id) const;

  // A today group (a Chromium tab group) becomes a pinned group with its title, colour and
  // collapsed state, after `pinned_tabs_before` of the pinned tabs (last, by default).
  void PinTabGroup(const tab_groups::TabGroupId& group,
                   std::optional<size_t> pinned_tabs_before = std::nullopt);
  // Today tabs `tabs` are pinned, in order, after `pinned_tabs_before` of the pinned tabs.
  void PinTabs(const std::vector<tabs::TabInterface*>& tabs, size_t pinned_tabs_before);

  // After any change to the tab strip: `moved` are the tabs it moved or inserted, which take the
  // group of where they landed (lb::GroupAfterMove, judged by neighbours that didn't move with
  // them), `closed` the tabs it closed and `left` the tabs that went to another window. Keeps
  // every group one unbroken run, and drops groups and tabs that are gone. While a tab drag runs,
  // each tab's group before its first change is remembered (EndTabDrag).
  void Update(const std::vector<tabs::TabInterface*>& moved = {},
              const std::vector<tabs::TabInterface*>& closed = {},
              const std::vector<tabs::TabInterface*>& left = {});
  // Session restore added `tab` with `extra_data` (lb::OnTabRestored).
  void OnTabRestored(tabs::TabInterface* tab, const std::map<std::string, std::string>& extra_data);

  // Another window takes a space parked in this one: the groups of its tabs `tabs` go with them.
  std::vector<Membership> Release(const std::vector<tabs::TabInterface*>& tabs);
  // This window takes tabs whose groups another window released.
  void Adopt(const std::vector<Membership>& memberships);

 private:
  TabStripModel* model() const;
  // The items of the pinned area in order: each tab, or both tabs of a split view.
  std::vector<std::vector<tabs::TabInterface*>> PinnedItems() const;
  // Sets `tab`'s group (empty: none) in the map, remembering its group before for a tab drag
  // that is under way. Returns whether it changed.
  bool SetMembership(tabs::TabInterface* tab, const std::string& id);
  // Writes `tab`'s group (or none) to Chromium's session.
  void WriteSession(tabs::TabInterface* tab) const;
  void WriteSessionForGroup(const std::string& id) const;

  raw_ptr<BrowserWindowInterface> browser_;
  base::RepeatingClosure on_changed_;
  std::map<std::string, PinnedGroup> groups_;
  std::map<tabs::TabHandle, std::string> group_of_;
  std::set<std::string> rename_requests_;
  // For a tab drag under way: each tab whose group changed, with its group before (empty: none).
  // Those groups are kept while the drag lasts, even with no tabs left.
  std::map<tabs::TabHandle, std::string> drag_origins_;
  bool updating_ = false;
};

// `tab`, or both tabs of its split view in strip order: what moves, pins or unpins together.
std::vector<tabs::TabInterface*> TabOrSplitOf(const TabStripModel& model, tabs::TabInterface* tab);
// Whether pinned groups show in `browser`'s window: the vertical strip, expanded. With tabs on top
// or the strip collapsed, a pinned group's tabs show as ordinary pinned tabs.
bool PinnedGroupsShown(BrowserWindowInterface* browser);
// The tracker of `tab`'s window, or null.
PinnedGroupTracker* PinnedGroupTrackerFor(const tabs::TabInterface* tab);
// Whether `tab` is in a collapsed pinned group where pinned groups show, the expanded vertical
// strip (patches/lb/pinned-groups.patch: Chromium counts it as in a collapsed group, as for
// Ctrl+Tab). Elsewhere its tab shows as an ordinary pinned tab, so it isn't.
bool IsInCollapsedPinnedGroup(const tabs::TabInterface* tab);

// Add to New Group (the tab menu) with only pinned tabs (patches/lb/pinned-groups.patch): makes
// them a pinned group in the group colour nearest the space's, its name being edited. Only where
// pinned groups show, the expanded vertical strip; elsewhere, and unless every one of `tabs` is
// pinned, returns false and does nothing.
bool AddToNewPinnedGroup(const std::vector<tabs::TabInterface*>& tabs);
// Tab > Group Tab (patches/lb/pinned-groups.patch), on pinned tabs where pinned groups show:
// takes them out of their pinned groups if all are in one, else makes them a new pinned group.
// Returns false, doing nothing, otherwise.
bool TogglePinnedGroup(const std::vector<tabs::TabInterface*>& tabs);

// The today group menu's Pin Group (patches/lb/pinned-groups.patch): `group` becomes a pinned
// group (PinnedGroupTracker::PinTabGroup). Only where pinned groups show; elsewhere it does
// nothing.
void PinTabGroup(BrowserWindowInterface* browser, const tab_groups::TabGroupId& group);

// Session hooks (patches/lb/pinned-groups.patch): what to save for a tab, in the session file and
// in tab restore's record of a closed tab or window; and a tab that session or tab restore just
// added, which gets its pinned group back, and its name and link (lb/browser/ui/tabs/
// tab_custom_data.h).
std::string GetPinnedGroupSessionData(content::WebContents* contents);
void OnTabRestored(BrowserWindowInterface* browser,
                   content::WebContents* contents,
                   const std::map<std::string, std::string>& extra_data);

}  // namespace lb

#endif  // LB_BROWSER_UI_TABS_PINNED_GROUP_TRACKER_H_
