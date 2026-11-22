// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/tabs/pinned_tab_sleep.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/resource_coordinator/lifecycle_unit_state.mojom.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "components/performance_manager/public/graph/page_node.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "lb/browser/tabs/pinned_close.h"
#include "lb/browser/tabs/tab_to_show.h"
#include "lb/browser/ui/tabs/pinned_group_tracker.h"
#include "url/gurl.h"

namespace lb {

namespace {

using resource_coordinator::TabLifecycleUnitExternal;

// Pinned tabs closed but not put to sleep yet (closed together, or closed again before the sleep
// ran, which is right after the close), with what the close decided: kSleep or kHide.
std::map<tabs::TabHandle, PinnedCloseOutcome>& PendingSleep() {
  static base::NoDestructor<std::map<tabs::TabHandle, PinnedCloseOutcome>> pending;
  return *pending;
}

// Pinned tabs a close only hid, as their page may hold unsaved input (MayHoldUnsavedInput), and
// tabs whose page is asking before it is left. Their next close isn't stopped.
std::set<tabs::TabHandle>& HiddenNotUnloaded() {
  static base::NoDestructor<std::set<tabs::TabHandle>> hidden;
  return *hidden;
}

// The index of the window's tab to show instead of `tab` (see ChooseTabToShow), or -1. Tabs about
// to go to sleep themselves are left out.
int TabToShowInstead(const TabStripModel& model, const tabs::TabInterface* tab) {
  std::vector<const tabs::TabInterface*> tabs;
  std::vector<TabToShowCandidate> others;
  for (const tabs::TabInterface* other : model) {
    if (other == tab || PendingSleep().contains(other->GetHandle())) {
      continue;
    }
    TabLifecycleUnitExternal* unit =
        TabLifecycleUnitExternal::FromWebContents(other->GetContents());
    tabs.push_back(other);
    others.push_back({.asleep = IsAsleep(*other),
                      .last_focused = unit ? unit->GetLastFocusedTime() : base::Time(),
                      .in_collapsed_group = (other->GetGroup().has_value() &&
                                             model.IsGroupCollapsed(*other->GetGroup())) ||
                                            IsInCollapsedPinnedGroup(other)});
  }
  const std::optional<size_t> chosen = ChooseTabToShow(others);
  return chosen ? model.GetIndexOfTab(tabs[*chosen]) : -1;
}

void Sleep(tabs::TabInterface::Handle handle) {
  const auto pending = PendingSleep().find(handle);
  const bool hide = pending != PendingSleep().end() && pending->second == PinnedCloseOutcome::kHide;
  if (pending != PendingSleep().end()) {
    PendingSleep().erase(pending);
  }
  tabs::TabInterface* tab = handle.Get();
  if (!tab || !tab->IsPinned() || tab->IsSplit() || !tab->GetBrowserWindowInterface()) {
    return;
  }
  TabStripModel* model = tab->GetBrowserWindowInterface()->GetTabStripModel();
  if (!model || model->closing_all() || model->GetIndexOfTab(tab) == TabStripModel::kNoTab) {
    return;
  }

  // The page on screen can't sleep: show another tab first, or a new one if there is none.
  if (model->GetActiveTab() == tab) {
    if (const int other = TabToShowInstead(*model, tab); other != -1) {
      model->ActivateTabAt(
          other, TabStripUserGestureDetails(TabStripUserGestureDetails::GestureType::kOther));
    } else {
      model->delegate()->AddTabAt(GURL(), -1, /*foreground=*/true);
    }
    if (model->GetActiveTab() == tab) {
      return;
    }
  }

  content::WebContents* contents = tab->GetContents();
  if (IsAsleep(*tab)) {
    return;
  }
  // A page that may hold unsaved input is only hidden: it stays loaded, and closing it again
  // closes it (asking first).
  if (hide) {
    HiddenNotUnloaded().insert(handle);
    return;
  }
  TabLifecycleUnitExternal* unit = TabLifecycleUnitExternal::FromWebContents(contents);
  if (!unit) {
    return;
  }
  // The new, unloaded contents doesn't inherit a mute set on the tab (by an extension, say).
  const bool muted = contents->IsAudioMuted();
  const TabMutedReason muted_reason = GetTabAudioMutedReason(contents);
  const LastMuteMetadata* mute_metadata = LastMuteMetadata::FromWebContents(contents);
  const std::string muted_by = mute_metadata ? mute_metadata->extension_id : std::string();

  // Chromium's own discard, as chrome.tabs.discard does it (no eligibility policy for this
  // reason): the page unloads and loads again when the tab is next shown.
  if (!unit->DiscardTab(::mojom::LifecycleUnitDiscardReason::EXTERNAL)) {
    return;
  }
  contents = tab->GetContents();
  // Load again whenever the tab is shown, not only once Chromium sees it focused in the active
  // window (it never discards the shown tab, so this can't reload it straight away).
  contents->GetController().SetNeedsReload();
  if (muted) {
    SetTabAudioMuted(contents, true, muted_reason, muted_by);
  }
  // Redraw the tab's row as asleep right away (INVALIDATE_TYPE_LOAD is not deferred), so its
  // button is "−" before the user can click it again.
  contents->NotifyNavigationStateChanged(static_cast<content::InvalidateTypes>(
      content::INVALIDATE_TYPE_TAB | content::INVALIDATE_TYPE_LOAD));
}

}  // namespace

bool IsAsleep(const tabs::TabInterface& tab) {
  content::WebContents* contents = tab.GetContents();
  return contents->WasDiscarded() || contents->GetController().NeedsReload();
}

bool IsHiddenAfterClose(const tabs::TabInterface& tab) {
  return HiddenNotUnloaded().contains(tab.GetHandle());
}

namespace {

// What closing `tab` with `close_types` does (DecidePinnedClose), reading the page's state only
// when it matters.
PinnedCloseOutcome CloseOutcome(const TabStripModel& model,
                                const tabs::TabInterface& tab,
                                uint32_t close_types) {
  content::WebContents* contents = tab.GetContents();
  PinnedCloseState state{
      .user_gesture = (close_types & TabCloseTypes::CLOSE_USER_GESTURE) != 0,
      .closing_all = model.closing_all(),
      .pinned = tab.IsPinned(),
      .split = tab.IsSplit(),
      .asleep = IsAsleep(tab),
      .shown = model.GetActiveTab() == &tab,
      .hidden_by_earlier_close = HiddenNotUnloaded().contains(tab.GetHandle()),
      .sleep_pending = PendingSleep().contains(tab.GetHandle()),
  };
  // The page's state is only needed for a pinned tab that may stay (and is cheap to read).
  if (DecidePinnedClose(state) != PinnedCloseOutcome::kClose) {
    state.asks_before_leaving = contents->NeedToFireBeforeUnload();
    const base::WeakPtr<performance_manager::PageNode> page =
        performance_manager::PerformanceManager::GetPrimaryPageNodeForWebContents(contents);
    state.had_user_edits = page && (page->HadUserEdits() || page->HadFormInteraction());
  }
  return DecidePinnedClose(state);
}

// Closes a pinned tab whose page may hold unsaved input, after Chromium asks the page, as
// closing it by hand does: HiddenNotUnloaded makes lb::SleepInsteadOfClose let the close through.
void CloseAskingFirst(tabs::TabInterface::Handle handle) {
  tabs::TabInterface* tab = handle.Get();
  if (!tab || !tab->GetBrowserWindowInterface()) {
    return;
  }
  TabStripModel* model = tab->GetBrowserWindowInterface()->GetTabStripModel();
  const int index = model ? model->GetIndexOfTab(tab) : TabStripModel::kNoTab;
  if (index == TabStripModel::kNoTab || model->closing_all()) {
    return;
  }
  model->CloseWebContentsAt(index, TabCloseTypes::CLOSE_USER_GESTURE);
}

}  // namespace

void SleepPinnedTabs(TabStripModel& model, const std::vector<tabs::TabInterface*>& tabs) {
  std::set<split_tabs::SplitTabId> splits;
  for (const tabs::TabInterface* tab : tabs) {
    if (const std::optional<split_tabs::SplitTabId> split = tab ? tab->GetSplit() : std::nullopt) {
      splits.insert(*split);
    }
  }
  for (const split_tabs::SplitTabId& split : splits) {
    if (model.ContainsSplit(split)) {
      model.RemoveSplit(split);
    }
  }
  // Every one of them is pending before any sleeps, so none is shown instead of another.
  std::vector<tabs::TabHandle> to_sleep;
  std::vector<tabs::TabHandle> to_close;
  for (tabs::TabInterface* tab : tabs) {
    if (!tab || model.GetIndexOfTab(tab) == TabStripModel::kNoTab) {
      continue;
    }
    const PinnedCloseOutcome outcome = CloseOutcome(model, *tab, TabCloseTypes::CLOSE_USER_GESTURE);
    if (outcome == PinnedCloseOutcome::kSleep || outcome == PinnedCloseOutcome::kHide) {
      PendingSleep().emplace(tab->GetHandle(), outcome);
      // The shown one first: the others are still pending when it picks the tab to show.
      if (model.GetActiveTab() == tab) {
        to_sleep.insert(to_sleep.begin(), tab->GetHandle());
      } else {
        to_sleep.push_back(tab->GetHandle());
      }
    } else if (outcome == PinnedCloseOutcome::kCloseAskingFirst) {
      // Not shown with unsaved input: it can't sleep or hide, so it closes once its page agrees.
      HiddenNotUnloaded().insert(tab->GetHandle());
      to_close.push_back(tab->GetHandle());
    }
  }
  for (const tabs::TabHandle& handle : to_sleep) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                             base::BindOnce(&Sleep, handle));
  }
  for (const tabs::TabHandle& handle : to_close) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&CloseAskingFirst, handle));
  }
}

bool SleepInsteadOfClose(const TabStripModel& model,
                         const tabs::TabInterface& tab,
                         uint32_t close_types) {
  switch (const PinnedCloseOutcome outcome = CloseOutcome(model, tab, close_types)) {
    case PinnedCloseOutcome::kClose:
      return false;
    case PinnedCloseOutcome::kCloseAskingFirst:
      HiddenNotUnloaded().insert(tab.GetHandle());  // the page's "Leave" closes it again
      return false;
    case PinnedCloseOutcome::kAlreadySleeping:
      return true;
    case PinnedCloseOutcome::kSleep:
    case PinnedCloseOutcome::kHide:
      PendingSleep().emplace(tab.GetHandle(), outcome);
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&Sleep, tab.GetHandle()));
      return true;
  }
}

}  // namespace lb
