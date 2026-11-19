// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/tab_to_show.h"

namespace lb {

std::optional<size_t> ChooseTabToShow(const std::vector<TabToShowCandidate>& others) {
  std::optional<size_t> best;
  for (size_t i = 0; i < others.size(); ++i) {
    if (!best) {
      best = i;
      continue;
    }
    const TabToShowCandidate& tab = others[i];
    const TabToShowCandidate& current = others[*best];
    if (tab.asleep != current.asleep) {
      if (!tab.asleep) {
        best = i;
      }
    } else if (tab.last_focused > current.last_focused) {
      best = i;
    }
  }
  return best;
}

}  // namespace lb
