// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_TABS_PINNED_TAB_SLEEP_H_
#define LB_BROWSER_UI_TABS_PINNED_TAB_SLEEP_H_

#include <cstdint>
#include <vector>

class TabStripModel;

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace lb {

// A pinned tab the user closes goes to sleep instead of closing: it keeps its place and its page
// unloads until it is clicked again. Called by TabStripModel::CloseTabs for each tab it is asked
// to close (patches/lb/pinned-tab-sleep.patch). Returns true when `tab` stays; the sleep itself
// runs right after the close call returns, since the model can't change while it closes tabs.
// Closing a pinned tab that is already asleep removes it; so do closing the window, closes that
// aren't the user's, and closing a pinned tab in a split view. A page that may hold unsaved input
// (typed into, and asking before it is left) is only hidden, not unloaded; closing it again
// closes it, after Chromium asks the page (as does closing it while it isn't shown). The rule is
// lb::DecidePinnedClose (lb/browser/tabs/pinned_close.h).
bool SleepInsteadOfClose(const TabStripModel& model,
                         const tabs::TabInterface& tab,
                         uint32_t close_types);

// Puts pinned `tabs` of `model` to sleep together, as closing each of them by hand would (a page
// that may hold unsaved input is only hidden, or, if it isn't shown, closed once the page agrees),
// for a pinned group's Close (docs/ux.md §6.1). A
// split view among them comes apart first, as a sleeping tab can't be shown in one. The tab shown
// instead of the shown one is never one of them.
void SleepPinnedTabs(TabStripModel& model, const std::vector<tabs::TabInterface*>& tabs);

// Whether `tab`'s page isn't loaded: put to sleep (discarded), or not loaded yet since a restart
// or a space switch. A sleeping pinned tab's row is faded with a "−" button (TabView).
bool IsAsleep(const tabs::TabInterface& tab);

// Whether a close only hid `tab`, as its page may hold unsaved input, or let the page ask before
// it is left: its next close really closes it.
bool IsHiddenAfterClose(const tabs::TabInterface& tab);

}  // namespace lb

#endif  // LB_BROWSER_UI_TABS_PINNED_TAB_SLEEP_H_
