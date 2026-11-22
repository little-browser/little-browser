// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/tabs/pinned_group_tracker.h"

#include <algorithm>
#include <ranges>
#include <set>
#include <utility>

#include "base/auto_reset.h"
#include "base/logging.h"
#include "base/uuid.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/browser/web_contents.h"
#include "lb/browser/spaces/space_model.h"
#include "lb/browser/ui/tabs/pinned_tab_sleep.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"

namespace lb {

namespace {

// The units `tabs` move as: each tab, or a split view whole (both its tabs, also when only one of
// them is in `tabs`), in strip order.
std::vector<std::vector<tabs::TabInterface*>> UnitsOf(const TabStripModel& model,
                                                      std::vector<tabs::TabInterface*> tabs) {
  std::erase_if(tabs, [&](tabs::TabInterface* t) { return !t || model.GetIndexOfTab(t) < 0; });
  std::ranges::sort(tabs, {}, [&](tabs::TabInterface* t) { return model.GetIndexOfTab(t); });
  std::vector<std::vector<tabs::TabInterface*>> units;
  for (tabs::TabInterface* tab : tabs) {
    const std::optional<split_tabs::SplitTabId> split = tab->GetSplit();
    if (split && !units.empty() && units.back().front()->GetSplit() == split) {
      continue;  // already taken with the first tab of its split
    }
    units.push_back(TabOrSplitOf(model, tab));
  }
  return units;
}

// Moves `units` (in strip order) next to each other, in order, after `target` of the other tabs.
// A split view moves whole (moving its tabs one by one would take them out of it). Units above
// their place move last first, so the ones still above don't count against `target`.
void MoveUnitsTo(TabStripModel* model,
                 const std::vector<std::vector<tabs::TabInterface*>>& units,
                 int target) {
  std::set<const tabs::TabInterface*> moving;
  for (const auto& unit : units) {
    moving.insert(unit.begin(), unit.end());
  }
  struct Move {
    const std::vector<tabs::TabInterface*>* unit;
    int to;
  };
  std::vector<Move> down;  // units above their place, which move down
  std::vector<Move> up;
  int offset = 0;
  for (const auto& unit : units) {
    const int index = model->GetIndexOfTab(unit.front());
    int others_before = 0;
    for (int i = 0; i < index; ++i) {
      others_before += !moving.contains(model->GetTabAtIndex(i));
    }
    (others_before < target ? down : up).push_back({&unit, target + offset});
    offset += static_cast<int>(unit.size());
  }
  auto move = [model](const Move& m) {
    const std::vector<tabs::TabInterface*>& unit = *m.unit;
    const int from = model->GetIndexOfTab(unit.front());
    if (from < 0 || from == m.to) {
      return;
    }
    if (unit.size() > 1 && unit.front()->GetSplit()) {
      model->MoveSplitTo(*unit.front()->GetSplit(), m.to, unit.front()->IsPinned(),
                         unit.front()->GetGroup());
    } else {
      model->MoveWebContentsAt(from, m.to, /*select_after_move=*/false);
    }
  };
  std::ranges::for_each(down | std::views::reverse, move);
  std::ranges::for_each(up, move);
}

}  // namespace

std::vector<tabs::TabInterface*> TabOrSplitOf(const TabStripModel& model, tabs::TabInterface* tab) {
  const std::optional<split_tabs::SplitTabId> split = tab->GetSplit();
  if (!split) {
    return {tab};
  }
  std::vector<tabs::TabInterface*> unit;
  for (int i = 0; i < model.count(); ++i) {
    if (model.GetTabAtIndex(i)->GetSplit() == split) {
      unit.push_back(model.GetTabAtIndex(i));
    }
  }
  return unit;
}

bool PinnedGroupsShown(BrowserWindowInterface* browser) {
  const tabs::VerticalTabStripStateController* state =
      browser ? tabs::VerticalTabStripStateController::From(browser) : nullptr;
  return state && state->ShouldDisplayVerticalTabs() &&
         state->GetCollapseState() == tabs::VerticalTabStripCollapseState::kExpanded;
}

PinnedGroupTracker::PinnedGroupTracker(BrowserWindowInterface* browser,
                                       base::RepeatingClosure on_changed)
    : browser_(browser), on_changed_(std::move(on_changed)) {}

PinnedGroupTracker::~PinnedGroupTracker() = default;

TabStripModel* PinnedGroupTracker::model() const {
  return browser_->GetTabStripModel();
}

const PinnedGroup* PinnedGroupTracker::GroupOf(const tabs::TabInterface* tab) const {
  if (!tab || !tab->IsPinned()) {
    return nullptr;
  }
  const auto it = group_of_.find(tab->GetHandle());
  return it == group_of_.end() ? nullptr : Find(it->second);
}

const PinnedGroup* PinnedGroupTracker::Find(const std::string& id) const {
  const auto it = groups_.find(id);
  return it == groups_.end() ? nullptr : &it->second;
}

std::vector<tabs::TabInterface*> PinnedGroupTracker::TabsOf(const std::string& id) const {
  std::vector<tabs::TabInterface*> tabs;
  TabStripModel* m = model();
  for (int i = 0; i < m->IndexOfFirstNonPinnedTab(); ++i) {
    tabs::TabInterface* tab = m->GetTabAtIndex(i);
    if (const auto it = group_of_.find(tab->GetHandle());
        it != group_of_.end() && it->second == id) {
      tabs.push_back(tab);
    }
  }
  return tabs;
}

std::vector<std::string> PinnedGroupTracker::GroupIds() const {
  std::vector<std::string> ids;
  for (const auto& [id, group] : groups_) {
    ids.push_back(id);
  }
  return ids;
}

std::vector<std::vector<tabs::TabInterface*>> PinnedGroupTracker::PinnedItems() const {
  std::vector<std::vector<tabs::TabInterface*>> items;
  TabStripModel* m = model();
  for (int i = 0; i < m->IndexOfFirstNonPinnedTab(); ++i) {
    tabs::TabInterface* tab = m->GetTabAtIndex(i);
    if (!items.empty() && tab->GetSplit() && items.back().front()->GetSplit() == tab->GetSplit()) {
      items.back().push_back(tab);
    } else {
      items.push_back({tab});
    }
  }
  return items;
}

bool PinnedGroupTracker::SetMembership(tabs::TabInterface* tab, const std::string& id) {
  const auto it = group_of_.find(tab->GetHandle());
  const std::string old = it == group_of_.end() ? std::string() : it->second;
  if (old == id) {
    return false;
  }
  if (TabDragController::IsActive()) {
    drag_origins_.try_emplace(tab->GetHandle(), old);
  }
  if (id.empty()) {
    group_of_.erase(tab->GetHandle());
  } else {
    group_of_[tab->GetHandle()] = id;
  }
  return true;
}

std::string PinnedGroupTracker::Create(const std::vector<tabs::TabInterface*>& tabs,
                                       PinnedGroup group) {
  TabStripModel* m = model();
  const std::set<const tabs::TabInterface*> chosen(tabs.begin(), tabs.end());
  const std::vector<std::vector<tabs::TabInterface*>> items = PinnedItems();
  std::vector<bool> selected;
  std::vector<std::vector<tabs::TabInterface*>> units;
  for (const auto& item : items) {
    selected.push_back(
        std::ranges::any_of(item, [&](tabs::TabInterface* t) { return chosen.contains(t); }));
    if (selected.back()) {
      units.push_back(item);
    }
  }
  if (units.empty()) {
    return std::string();
  }
  if (group.id.empty()) {
    group.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  }
  const std::string id = group.id;
  groups_[id] = std::move(group);
  {
    base::AutoReset<bool> updating(&updating_, true);
    // They leave their groups, then go where the first of them is, never inside another group.
    std::vector<std::string> groups;
    for (size_t i = 0; i < items.size(); ++i) {
      for (tabs::TabInterface* tab : selected[i] ? items[i] : std::vector<tabs::TabInterface*>()) {
        SetMembership(tab, std::string());
      }
      const auto it = group_of_.find(items[i].front()->GetHandle());
      groups.push_back(it == group_of_.end() ? std::string() : it->second);
    }
    const size_t position = NewPinnedGroupPosition(groups, selected);
    int target = 0;
    for (size_t i = 0, others = 0; i < items.size() && others < position; ++i) {
      if (!selected[i]) {
        target += static_cast<int>(items[i].size());
        ++others;
      }
    }
    MoveUnitsTo(m, units, target);
    for (const auto& unit : units) {
      for (tabs::TabInterface* tab : unit) {
        SetMembership(tab, id);
      }
    }
  }
  Update();
  WriteSessionForGroup(id);
  on_changed_.Run();
  return id;
}

void PinnedGroupTracker::SetTitle(const std::string& id, const std::u16string& title) {
  if (auto it = groups_.find(id); it != groups_.end() && it->second.title != title) {
    it->second.title = title;
    WriteSessionForGroup(id);
    on_changed_.Run();
  }
}

void PinnedGroupTracker::SetColor(const std::string& id, int color) {
  if (auto it = groups_.find(id); it != groups_.end() && it->second.color != color) {
    it->second.color = color;
    WriteSessionForGroup(id);
    on_changed_.Run();
  }
}

void PinnedGroupTracker::SetCollapsed(const std::string& id, bool collapsed) {
  if (auto it = groups_.find(id); it != groups_.end() && it->second.collapsed != collapsed) {
    it->second.collapsed = collapsed;
    WriteSessionForGroup(id);
    on_changed_.Run();
  }
}

void PinnedGroupTracker::SetGroupOf(tabs::TabInterface* tab, const std::string& id) {
  if (!tab || !tab->IsPinned() || model()->GetIndexOfTab(tab) < 0 ||
      (!id.empty() && !groups_.contains(id))) {
    return;
  }
  bool changed = false;
  const auto units = UnitsOf(*model(), {tab});
  for (tabs::TabInterface* t : units.front()) {
    changed = SetMembership(t, id) || changed;
  }
  if (!changed) {
    return;
  }
  Update();
  for (tabs::TabInterface* t : units.front()) {
    WriteSession(t);
  }
  on_changed_.Run();
}

void PinnedGroupTracker::SetDraggedGroupOf(tabs::TabInterface* tab, const std::string& id) {
  // A decision from the drag's last layout that lands after the drag is over is stale.
  if (TabDragController::IsActive()) {
    SetGroupOf(tab, id);
  }
}

std::string PinnedGroupTracker::DragStartGroupOf(const tabs::TabInterface* tab) const {
  if (!tab) {
    return std::string();
  }
  if (const auto origin = drag_origins_.find(tab->GetHandle()); origin != drag_origins_.end()) {
    return origin->second;
  }
  const auto it = group_of_.find(tab->GetHandle());
  return it == group_of_.end() ? std::string() : it->second;
}

void PinnedGroupTracker::EndTabDrag(bool cancelled) {
  // The groups the drag changed are still kept alive by `drag_origins_` here.
  std::vector<tabs::TabInterface*> restored;
  if (cancelled) {
    for (const auto& [handle, id] : drag_origins_) {
      tabs::TabInterface* tab = handle.Get();
      // Only tabs back in this window: a drag across windows ends in each of them.
      if (!tab || !tab->IsPinned() || model()->GetIndexOfTab(tab) < 0 ||
          (!id.empty() && !groups_.contains(id))) {
        continue;
      }
      const auto it = group_of_.find(handle);
      if ((it == group_of_.end() ? std::string() : it->second) == id) {
        continue;
      }
      if (id.empty()) {
        group_of_.erase(it);
      } else {
        group_of_[handle] = id;
      }
      restored.push_back(tab);
    }
  }
  drag_origins_.clear();
  Update();
  for (tabs::TabInterface* tab : restored) {
    WriteSession(tab);
  }
  if (!restored.empty()) {
    on_changed_.Run();
  }
}

void PinnedGroupTracker::RemoveFromGroups(const std::vector<tabs::TabInterface*>& tabs) {
  TabStripModel* m = model();
  std::vector<tabs::TabInterface*> changed;
  for (const auto& unit : UnitsOf(*m, tabs)) {
    const PinnedGroup* group = GroupOf(unit.front());
    if (!group) {
      continue;
    }
    const std::string id = group->id;
    base::AutoReset<bool> updating(&updating_, true);
    for (tabs::TabInterface* tab : unit) {
      SetMembership(tab, std::string());
      changed.push_back(tab);
    }
    // One in the middle of its group moves out past the group's last tab.
    const std::vector<tabs::TabInterface*> rest = TabsOf(id);
    const int index = m->GetIndexOfTab(unit.front());
    if (!rest.empty() && index > m->GetIndexOfTab(rest.front()) &&
        index < m->GetIndexOfTab(rest.back())) {
      MoveUnitsTo(m, {unit}, m->GetIndexOfTab(rest.back()) + 1 - static_cast<int>(unit.size()));
    }
  }
  if (changed.empty()) {
    return;
  }
  Update();
  for (tabs::TabInterface* tab : changed) {
    WriteSession(tab);
  }
  on_changed_.Run();
}

void PinnedGroupTracker::Ungroup(const std::string& id) {
  // Only its tabs in the tab strip: tabs of a parked space keep their group.
  const std::vector<tabs::TabInterface*> tabs = TabsOf(id);
  {
    base::AutoReset<bool> updating(&updating_, true);
    for (tabs::TabInterface* tab : tabs) {
      SetMembership(tab, std::string());
    }
  }
  Update();
  for (tabs::TabInterface* tab : tabs) {
    WriteSession(tab);
  }
  on_changed_.Run();
}

void PinnedGroupTracker::Unpin(const std::string& id, size_t today_tabs_before) {
  const PinnedGroup* group = Find(id);
  const std::vector<tabs::TabInterface*> tabs = TabsOf(id);
  if (!group || tabs.empty()) {
    return;
  }
  const PinnedGroup visuals = *group;
  TabStripModel* m = model();
  // A collapsed today group hides every tab, the shown one too, so it unpins expanded then.
  const bool collapsed = visuals.collapsed && !std::ranges::contains(tabs, m->GetActiveTab());
  Ungroup(id);
  std::vector<int> indices;
  for (tabs::TabInterface* tab : tabs) {
    indices.push_back(m->GetIndexOfTab(tab));
  }
  std::ranges::sort(indices);
  // Unpinned in order, they land first among the today tabs.
  m->LbSetTabsPinned(indices, false);
  if (!m->SupportsTabGroups()) {
    return;
  }
  indices.clear();
  for (tabs::TabInterface* tab : tabs) {
    if (const int index = m->GetIndexOfTab(tab); index >= 0) {
      indices.push_back(index);
    }
  }
  std::ranges::sort(indices);
  const tab_groups::TabGroupId today = m->AddToNewGroup(indices);
  // The uint32_t constructor maps an unknown colour to grey.
  m->ChangeTabGroupVisuals(
      today, tab_groups::TabGroupVisualData(visuals.title, static_cast<uint32_t>(visuals.color),
                                            collapsed));
  // AddToNewGroup keeps them first among the today tabs; the others are after them.
  const int others = m->count() - m->IndexOfFirstNonPinnedTab() - static_cast<int>(tabs.size());
  const int before = std::min(static_cast<int>(today_tabs_before), others);
  if (before > 0) {
    m->MoveGroupTo(today, m->IndexOfFirstNonPinnedTab() + before);
  }
}

void PinnedGroupTracker::Move(const std::string& id, size_t pinned_tabs_before) {
  const std::vector<tabs::TabInterface*> tabs = TabsOf(id);
  if (tabs.empty()) {
    return;
  }
  TabStripModel* m = model();
  const int others = m->IndexOfFirstNonPinnedTab() - static_cast<int>(tabs.size());
  {
    // One move of the whole group: no tab leaves it on the way.
    base::AutoReset<bool> updating(&updating_, true);
    MoveUnitsTo(m, UnitsOf(*m, tabs), std::min(static_cast<int>(pinned_tabs_before), others));
  }
  Update();
  on_changed_.Run();
}

bool PinnedGroupTracker::AllTabsAsleep(const std::string& id) const {
  std::vector<PinnedGroupTab> states;
  for (const tabs::TabInterface* tab : TabsOf(id)) {
    states.push_back({.asleep = IsAsleep(*tab), .hidden_after_close = IsHiddenAfterClose(*tab)});
  }
  return DecidePinnedGroupClose(states).remove;
}

void PinnedGroupTracker::Close(const std::string& id) {
  const std::vector<tabs::TabInterface*> tabs = TabsOf(id);
  std::vector<PinnedGroupTab> states;
  for (const tabs::TabInterface* tab : tabs) {
    states.push_back({.asleep = IsAsleep(*tab), .hidden_after_close = IsHiddenAfterClose(*tab)});
  }
  const PinnedGroupClose close = DecidePinnedGroupClose(states);
  std::vector<tabs::TabInterface*> chosen;
  for (size_t i : close.tabs) {
    chosen.push_back(tabs[i]);
  }
  TabStripModel* m = model();
  if (!close.remove) {
    SleepPinnedTabs(*m, chosen);
    return;
  }
  for (tabs::TabInterface* tab : chosen) {
    if (const int index = m->GetIndexOfTab(tab); index >= 0) {
      m->CloseWebContentsAt(index, TabCloseTypes::CLOSE_USER_GESTURE);
    }
  }
}

void PinnedGroupTracker::PinTabGroup(const tab_groups::TabGroupId& group,
                                     std::optional<size_t> pinned_tabs_before) {
  TabStripModel* m = model();
  // Gone by now (a posted drop), or never here.
  if (!m->group_model() || !m->group_model()->ContainsTabGroup(group)) {
    return;
  }
  const TabGroup* tab_group = m->group_model()->GetTabGroup(group);
  const tab_groups::TabGroupVisualData visuals = *tab_group->visual_data();
  std::vector<tabs::TabInterface*> tabs;
  std::vector<int> indices;
  const gfx::Range range = tab_group->ListTabs();
  for (uint32_t i = range.start(); i < range.end(); ++i) {
    tabs.push_back(m->GetTabAtIndex(static_cast<int>(i)));
    indices.push_back(static_cast<int>(i));
  }
  if (indices.empty()) {
    return;
  }
  m->RemoveFromGroup(indices);
  indices.clear();
  for (tabs::TabInterface* tab : tabs) {
    indices.push_back(m->GetIndexOfTab(tab));
  }
  std::ranges::sort(indices);
  {
    // Pinned in order, they land last among the pinned tabs; then they move to their place.
    base::AutoReset<bool> updating(&updating_, true);
    m->LbSetTabsPinned(indices, true);
    if (pinned_tabs_before) {
      const int others = m->IndexOfFirstNonPinnedTab() - static_cast<int>(tabs.size());
      MoveUnitsTo(m, UnitsOf(*m, tabs), std::min(static_cast<int>(*pinned_tabs_before), others));
    }
  }
  Create(tabs, PinnedGroup{.title = visuals.title(),
                           .color = static_cast<int>(visuals.color()),
                           .collapsed = visuals.is_collapsed()});
}

void PinnedGroupTracker::PinTabs(const std::vector<tabs::TabInterface*>& tabs,
                                 size_t pinned_tabs_before) {
  TabStripModel* m = model();
  std::vector<int> indices;
  for (tabs::TabInterface* tab : tabs) {
    if (const int index = tab ? m->GetIndexOfTab(tab) : -1; index >= 0) {
      indices.push_back(index);
    }
  }
  if (indices.empty()) {
    return;
  }
  std::ranges::sort(indices);
  {
    base::AutoReset<bool> updating(&updating_, true);
    m->LbSetTabsPinned(indices, true);
    const auto units = UnitsOf(*m, tabs);
    int size = 0;
    for (const auto& unit : units) {
      size += static_cast<int>(unit.size());
    }
    MoveUnitsTo(
        m, units,
        std::min(static_cast<int>(pinned_tabs_before), m->IndexOfFirstNonPinnedTab() - size));
  }
  Update();
}

void PinnedGroupTracker::Update(const std::vector<tabs::TabInterface*>& moved,
                                const std::vector<tabs::TabInterface*>& closed,
                                const std::vector<tabs::TabInterface*>& left) {
  if (updating_) {
    return;
  }
  base::AutoReset<bool> updating(&updating_, true);
  // Tabs that closed are gone; tabs that went to another window, or were unpinned, leave their
  // group.
  std::erase_if(group_of_, [&](const auto& entry) {
    return !entry.first.Get() || std::ranges::contains(closed, entry.first.Get());
  });
  for (tabs::TabInterface* tab : left) {
    if (tab) {
      SetMembership(tab, std::string());
    }
  }
  TabStripModel* m = model();
  std::vector<tabs::TabInterface*> changed;
  for (int i = m->IndexOfFirstNonPinnedTab(); i < m->count(); ++i) {
    tabs::TabInterface* tab = m->GetTabAtIndex(i);
    if (SetMembership(tab, std::string())) {
      changed.push_back(tab);
    }
  }
  const std::vector<std::vector<tabs::TabInterface*>> items = PinnedItems();
  std::vector<std::string> groups;
  std::vector<bool> item_moved;
  for (const std::vector<tabs::TabInterface*>& item : items) {
    const auto it = group_of_.find(item.front()->GetHandle());
    groups.push_back(it != group_of_.end() && groups_.contains(it->second) ? it->second
                                                                           : std::string());
    item_moved.push_back(std::ranges::any_of(
        item, [&](tabs::TabInterface* t) { return std::ranges::contains(moved, t); }));
  }
  // Moved and inserted items take the group of where they landed, judged by the items next to
  // them that didn't move with them.
  std::vector<std::string> landed = groups;
  for (size_t i = 0; i < items.size(); ++i) {
    if (!item_moved[i]) {
      continue;
    }
    std::string before;
    for (size_t j = i; j-- > 0;) {
      if (!item_moved[j]) {
        before = groups[j];
        break;
      }
    }
    std::string after;
    for (size_t j = i + 1; j < items.size(); ++j) {
      if (!item_moved[j]) {
        after = groups[j];
        break;
      }
    }
    bool own_has_others = false;
    for (size_t j = 0; j < groups.size(); ++j) {
      own_has_others =
          own_has_others || (!item_moved[j] && !groups[i].empty() && groups[j] == groups[i]);
    }
    landed[i] = GroupAfterMove(groups[i], before, after, own_has_others);
  }
  const std::vector<std::string> normalized = NormalizePinnedGroups(landed);
  for (size_t i = 0; i < items.size(); ++i) {
    for (tabs::TabInterface* tab : items[i]) {
      if (SetMembership(tab, normalized[i])) {
        changed.push_back(tab);
      }
    }
  }
  // A group with no tabs left, here or in a parked space, is gone.
  std::set<std::string> used;
  for (const auto& [handle, id] : group_of_) {
    used.insert(id);
  }
  for (const auto& [handle, id] : drag_origins_) {
    used.insert(id);
  }
  const size_t before = groups_.size();
  std::erase_if(groups_, [&](const auto& entry) { return !used.contains(entry.first); });
  // A tab that moved here may bring another window's group in its session data: rewrite it too.
  std::set<tabs::TabInterface*> to_write(changed.begin(), changed.end());
  for (tabs::TabInterface* tab : moved) {
    if (tab && m->GetIndexOfTab(tab) >= 0) {
      to_write.insert(tab);
    }
  }
  for (tabs::TabInterface* tab : to_write) {
    WriteSession(tab);
  }
  if (!changed.empty() || groups_.size() != before) {
    on_changed_.Run();
  }
}

void PinnedGroupTracker::OnTabRestored(tabs::TabInterface* tab,
                                       const std::map<std::string, std::string>& extra_data) {
  const auto data = extra_data.find(kPinnedGroupSessionKey);
  if (!tab || data == extra_data.end()) {
    return;
  }
  std::optional<PinnedGroup> group = PinnedGroupFromSessionData(data->second);
  if (!group || !tab->IsPinned()) {
    return;
  }
  // Where it came back: the groups of the items next to it (lb::RestoredTabGroup).
  std::string before;
  std::string after;
  const std::vector<std::vector<tabs::TabInterface*>> items = PinnedItems();
  const auto group_of_item = [&](size_t i) {
    const auto it = group_of_.find(items[i].front()->GetHandle());
    return it != group_of_.end() && groups_.contains(it->second) ? it->second : std::string();
  };
  for (size_t i = 0; i < items.size(); ++i) {
    if (std::ranges::contains(items[i], tab)) {
      before = i > 0 ? group_of_item(i - 1) : std::string();
      after = i + 1 < items.size() ? group_of_item(i + 1) : std::string();
      break;
    }
  }
  const std::string id = RestoredTabGroup(group->id, before, after);
  if (id == group->id) {
    // The first tab of a group to come back brings its title, colour and collapsed state.
    VLOG(1) << "pinned group: session restore brought a tab back into \"" << group->title << "\"";
    groups_.try_emplace(id, std::move(*group));
  }
  group_of_[tab->GetHandle()] = id;
  Update();
  on_changed_.Run();
}

std::vector<PinnedGroupTracker::Membership> PinnedGroupTracker::Release(
    const std::vector<tabs::TabInterface*>& tabs) {
  std::vector<Membership> released;
  for (tabs::TabInterface* tab : tabs) {
    const auto it = tab ? group_of_.find(tab->GetHandle()) : group_of_.end();
    if (it == group_of_.end()) {
      continue;
    }
    if (const PinnedGroup* group = Find(it->second)) {
      released.emplace_back(tab->GetHandle(), *group);
    }
    group_of_.erase(it);
  }
  if (!released.empty()) {
    Update();  // drops the groups that have no tabs left here
    on_changed_.Run();
  }
  return released;
}

void PinnedGroupTracker::Adopt(const std::vector<Membership>& memberships) {
  for (const auto& [handle, group] : memberships) {
    groups_.try_emplace(group.id, group);
    group_of_[handle] = group.id;
  }
  if (!memberships.empty()) {
    on_changed_.Run();
  }
}

void PinnedGroupTracker::WriteSession(tabs::TabInterface* tab) const {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  SessionService* session =
      contents ? SessionServiceFactory::GetForProfileIfExisting(browser_->GetProfile()) : nullptr;
  if (!session) {
    return;
  }
  const SessionID window = sessions::SessionTabHelper::IdForWindowContainingTab(contents);
  const SessionID id = sessions::SessionTabHelper::IdForTab(contents);
  if (!window.is_valid() || !id.is_valid()) {
    return;
  }
  const PinnedGroup* group = GroupOf(tab);
  session->AddTabExtraData(window, id, kPinnedGroupSessionKey,
                           group ? PinnedGroupToSessionData(*group) : std::string());
}

void PinnedGroupTracker::WriteSessionForGroup(const std::string& id) const {
  for (tabs::TabInterface* tab : TabsOf(id)) {
    WriteSession(tab);
  }
}

PinnedGroupTracker* PinnedGroupTrackerFor(const tabs::TabInterface* tab) {
  BrowserWindowInterface* browser =
      tab ? const_cast<tabs::TabInterface*>(tab)->GetBrowserWindowInterface() : nullptr;
  SidebarController* controller = browser ? SidebarController::FromBrowser(browser) : nullptr;
  return controller ? &controller->pinned_groups() : nullptr;
}

bool IsInCollapsedPinnedGroup(const tabs::TabInterface* tab) {
  // With tabs on top or the strip collapsed, a pinned group's tabs show as ordinary pinned tabs.
  BrowserWindowInterface* browser =
      tab ? const_cast<tabs::TabInterface*>(tab)->GetBrowserWindowInterface() : nullptr;
  if (!PinnedGroupsShown(browser)) {
    return false;
  }
  const PinnedGroupTracker* tracker = PinnedGroupTrackerFor(tab);
  const PinnedGroup* group = tracker ? tracker->GroupOf(tab) : nullptr;
  return group && group->collapsed;
}

bool AddToNewPinnedGroup(const std::vector<tabs::TabInterface*>& tabs) {
  if (tabs.empty() || std::ranges::any_of(tabs, [](const tabs::TabInterface* tab) {
        return !tab || !tab->IsPinned();
      })) {
    return false;
  }
  BrowserWindowInterface* browser = tabs.front()->GetBrowserWindowInterface();
  SidebarController* controller = browser ? SidebarController::FromBrowser(browser) : nullptr;
  // With tabs on top or in the collapsed strip no pinned group would show: Chromium's own.
  if (!controller || !PinnedGroupsShown(browser)) {
    return false;
  }
  const std::optional<int> hue = controller->ActiveSpaceHue();
  PinnedGroupTracker& tracker = controller->pinned_groups();
  const std::string id = tracker.Create(
      tabs, PinnedGroup{.color = static_cast<int>(hue ? TabGroupColorForHue(*hue)
                                                      : tab_groups::TabGroupColorId::kGrey)});
  if (id.empty()) {
    return false;
  }
  tracker.RequestRename(id);
  return true;
}

bool TogglePinnedGroup(const std::vector<tabs::TabInterface*>& tabs) {
  if (tabs.empty() || std::ranges::any_of(tabs, [](const tabs::TabInterface* tab) {
        return !tab || !tab->IsPinned();
      })) {
    return false;
  }
  BrowserWindowInterface* browser = tabs.front()->GetBrowserWindowInterface();
  SidebarController* controller = browser ? SidebarController::FromBrowser(browser) : nullptr;
  if (!controller || !PinnedGroupsShown(browser)) {
    return false;
  }
  PinnedGroupTracker& tracker = controller->pinned_groups();
  if (std::ranges::all_of(
          tabs, [&](const tabs::TabInterface* tab) { return tracker.GroupOf(tab) != nullptr; })) {
    tracker.RemoveFromGroups(tabs);
    return true;
  }
  return AddToNewPinnedGroup(tabs);
}

void PinTabGroup(BrowserWindowInterface* browser, const tab_groups::TabGroupId& group) {
  SidebarController* controller = browser ? SidebarController::FromBrowser(browser) : nullptr;
  // With tabs on top or in the collapsed strip no pinned group would show.
  if (controller && PinnedGroupsShown(browser)) {
    controller->pinned_groups().PinTabGroup(group);
  }
}

std::string GetPinnedGroupSessionData(content::WebContents* contents) {
  tabs::TabInterface* tab = contents ? tabs::TabInterface::MaybeGetFromContents(contents) : nullptr;
  const PinnedGroupTracker* tracker = PinnedGroupTrackerFor(tab);
  const PinnedGroup* group = tracker ? tracker->GroupOf(tab) : nullptr;
  return group ? PinnedGroupToSessionData(*group) : std::string();
}

void OnTabRestored(BrowserWindowInterface* browser,
                   content::WebContents* contents,
                   const std::map<std::string, std::string>& extra_data) {
  SidebarController* controller = browser ? SidebarController::FromBrowser(browser) : nullptr;
  if (controller && contents) {
    controller->pinned_groups().OnTabRestored(tabs::TabInterface::GetFromContents(contents),
                                              extra_data);
  }
}

}  // namespace lb
