// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_TABS_TAB_TO_SHOW_H_
#define LB_BROWSER_TABS_TAB_TO_SHOW_H_

#include <optional>
#include <vector>

#include "base/time/time.h"

namespace lb {

// One of the window's other tabs, as seen when the shown tab goes to sleep.
struct TabToShowCandidate {
  bool asleep = false;
  base::Time last_focused;
};

// Which of `others` to show when the shown pinned tab goes to sleep: the one focused last,
// preferring tabs that are awake so nothing has to reload. Ties go to the earlier tab. Returns
// nullopt when there are no other tabs.
std::optional<size_t> ChooseTabToShow(const std::vector<TabToShowCandidate>& others);

}  // namespace lb

#endif  // LB_BROWSER_TABS_TAB_TO_SHOW_H_
