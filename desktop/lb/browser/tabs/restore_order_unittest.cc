// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/restore_order.h"

#include <algorithm>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

using Order = std::vector<int>;

// The window's tab order after adding the saved tabs in `order`, each at its saved index (clamped
// to the end, as TabStripModel does): it must be the saved order, whatever was added first.
Order ResultingStrip(const Order& order) {
  Order strip;
  for (int saved_index : order) {
    const auto at = std::min(static_cast<size_t>(saved_index), strip.size());
    strip.insert(strip.begin() + static_cast<std::ptrdiff_t>(at), saved_index);
  }
  return strip;
}

// The regression: all pinned tabs asleep and an unpinned tab shown. The selected tab must come
// first, or the first pinned tab becomes active for a moment and loads.
TEST(TabRestoreOrderTest, SelectedTabComesFirst) {
  EXPECT_EQ(TabRestoreOrder(4, 3), (Order{3, 0, 1, 2}));
  EXPECT_EQ(TabRestoreOrder(4, 1), (Order{1, 0, 2, 3}));
}

TEST(TabRestoreOrderTest, FirstTabSelectedKeepsSavedOrder) {
  EXPECT_EQ(TabRestoreOrder(3, 0), (Order{0, 1, 2}));
}

TEST(TabRestoreOrderTest, NoSelectionKeepsSavedOrder) {
  EXPECT_EQ(TabRestoreOrder(3, std::nullopt), (Order{0, 1, 2}));
}

TEST(TabRestoreOrderTest, SelectionOutOfRangeIsIgnored) {
  EXPECT_EQ(TabRestoreOrder(3, 3), (Order{0, 1, 2}));
  EXPECT_EQ(TabRestoreOrder(3, -1), (Order{0, 1, 2}));
}

TEST(TabRestoreOrderTest, NoTabs) {
  EXPECT_TRUE(TabRestoreOrder(0, 0).empty());
  EXPECT_TRUE(TabRestoreOrder(-1, std::nullopt).empty());
}

TEST(TabRestoreOrderTest, OneTab) {
  EXPECT_EQ(TabRestoreOrder(1, 0), (Order{0}));
}

TEST(TabRestoreOrderTest, EveryTabOnceAndSavedOrderKept) {
  for (int count = 1; count <= 6; ++count) {
    Order saved(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
      saved[static_cast<size_t>(i)] = i;
    }
    for (int selected = 0; selected < count; ++selected) {
      const Order order = TabRestoreOrder(count, selected);
      EXPECT_EQ(order.front(), selected);
      Order sorted = order;
      std::ranges::sort(sorted);
      EXPECT_EQ(sorted, saved) << count << " tabs, selected " << selected;
      EXPECT_EQ(ResultingStrip(order), saved) << count << " tabs, selected " << selected;
    }
  }
}

}  // namespace
}  // namespace lb
