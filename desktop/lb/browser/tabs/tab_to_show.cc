// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/tab_to_show.h"

#include <tuple>

namespace lb {

std::optional<size_t> ChooseTabToShow(const std::vector<TabToShowCandidate>& others) {
  // Higher is better: visible before hidden in a collapsed group, awake before asleep, then the
  // one focused last.
  auto rank = [](const TabToShowCandidate& tab) {
    return std::tuple(!tab.in_collapsed_group, !tab.asleep, tab.last_focused);
  };
  std::optional<size_t> best;
  for (size_t i = 0; i < others.size(); ++i) {
    if (!best || rank(others[i]) > rank(others[*best])) {
      best = i;
    }
  }
  return best;
}

}  // namespace lb
