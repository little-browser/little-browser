// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/restore_order.h"

#include "lb/core_bridge/tabs.rs.h"

namespace lb {

// The rule is lb-core's (lb_core::tabs::tab_restore_order).
std::vector<int> TabRestoreOrder(int tab_count, std::optional<int> selected) {
  if (tab_count <= 0) {
    return {};
  }
  const int64_t chosen = selected && *selected >= 0 ? *selected : -1;
  std::vector<int> order;
  for (size_t index : core::tab_restore_order(static_cast<size_t>(tab_count), chosen)) {
    order.push_back(static_cast<int>(index));
  }
  return order;
}

}  // namespace lb
