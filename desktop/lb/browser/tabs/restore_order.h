// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_TABS_RESTORE_ORDER_H_
#define LB_BROWSER_TABS_RESTORE_ORDER_H_

#include <optional>
#include <vector>

namespace lb {

// The order in which session restore adds a saved window's `tab_count` tabs: the tab to select
// first, then the others in their saved order. The first tab added to an empty window becomes
// its active tab, and on macOS showing a tab loads it, so restoring in saved order would wake the
// first tab (often a sleeping pinned tab) even when another one is selected. Adding each of the
// others at its saved index keeps the saved order. `selected` is nullopt when the window already
// has tabs (restore selects none then); out of range, it is ignored.
// Used by patches/lb/restore-selected-first.patch.
std::vector<int> TabRestoreOrder(int tab_count, std::optional<int> selected);

}  // namespace lb

#endif  // LB_BROWSER_TABS_RESTORE_ORDER_H_
