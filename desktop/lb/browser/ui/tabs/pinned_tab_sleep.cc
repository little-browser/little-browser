// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/tabs/pinned_tab_sleep.h"

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
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "lb/browser/tabs/tab_to_show.h"
#include "third_party/blink/public/mojom/frame/sudden_termination_disabler_type.mojom-shared.h"
#include "url/gurl.h"

namespace lb {

namespace {

using resource_coordinator::TabLifecycleUnitExternal;

// Pinned tabs closed but not put to sleep yet: closed together, or clicked twice in a row.
std::set<tabs::TabHandle>& PendingSleep() {
  static base::NoDestructor<std::set<tabs::TabHandle>> pending;
  return *pending;
}

// Whether the page asks before it is left (a beforeunload handler, which pages register while
// they hold unsaved input). Unloading it without asking could lose that input.
bool AsksBeforeLeaving(content::WebContents* contents) {
  bool asks = false;
  contents->GetPrimaryMainFrame()->ForEachRenderFrameHost([&](content::RenderFrameHost* frame) {
    asks |= frame->GetSuddenTerminationDisablerState(
        blink::mojom::SuddenTerminationDisablerType::kBeforeUnloadHandler);
  });
  return asks;
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
                      .last_focused = unit ? unit->GetLastFocusedTime() : base::Time()});
  }
  const std::optional<size_t> chosen = ChooseTabToShow(others);
  return chosen ? model.GetIndexOfTab(tabs[*chosen]) : -1;
}

void Sleep(tabs::TabInterface::Handle handle) {
  PendingSleep().erase(handle);
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
  // A page that may hold unsaved input is only hidden: it stays loaded until it is closed again.
  if (IsAsleep(*tab) || AsksBeforeLeaving(contents)) {
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

bool SleepInsteadOfClose(const TabStripModel& model,
                         const tabs::TabInterface& tab,
                         uint32_t close_types) {
  // A pinned tab that is already asleep closes for good (its "−" button). So does one in a split
  // view, which Chromium closes out of the split, and one being closed by Chromium itself.
  if (!(close_types & TabCloseTypes::CLOSE_USER_GESTURE) || model.closing_all() ||
      !tab.IsPinned() || tab.IsSplit() || IsAsleep(tab)) {
    return false;
  }
  // Already closed once and waiting to sleep (a double click): nothing more to do.
  if (!PendingSleep().insert(tab.GetHandle()).second) {
    return true;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                           base::BindOnce(&Sleep, tab.GetHandle()));
  return true;
}

}  // namespace lb
