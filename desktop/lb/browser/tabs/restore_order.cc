// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/restore_order.h"

namespace lb {

std::vector<int> TabRestoreOrder(int tab_count, std::optional<int> selected) {
  std::vector<int> order;
  if (tab_count <= 0) {
    return order;
  }
  order.reserve(static_cast<size_t>(tab_count));
  const bool first = selected && *selected >= 0 && *selected < tab_count;
  if (first) {
    order.push_back(*selected);
  }
  for (int i = 0; i < tab_count; ++i) {
    if (!first || i != *selected) {
      order.push_back(i);
    }
  }
  return order;
}

}  // namespace lb
