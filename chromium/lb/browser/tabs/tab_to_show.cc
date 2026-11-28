// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/tab_to_show.h"

#include "lb/core_bridge/tabs.rs.h"

namespace lb {

// The rule is lb-core's (lb_core::tabs::choose_tab_to_show).
std::optional<size_t> ChooseTabToShow(const std::vector<TabToShowCandidate>& others) {
  std::vector<core::TabToShowCandidate> candidates;
  candidates.reserve(others.size());
  for (const TabToShowCandidate& tab : others) {
    candidates.push_back({
        .asleep = tab.asleep,
        .last_focused = tab.last_focused.ToDeltaSinceWindowsEpoch().InMicroseconds(),
        .in_collapsed_group = tab.in_collapsed_group,
    });
  }
  const int64_t best = core::choose_tab_to_show(
      rust::Slice<const core::TabToShowCandidate>(candidates.data(), candidates.size()));
  if (best < 0) {
    return std::nullopt;
  }
  return static_cast<size_t>(best);
}

}  // namespace lb
