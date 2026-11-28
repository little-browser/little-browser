// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/tab_to_show.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

base::Time At(int seconds) {
  return base::Time::UnixEpoch() + base::Seconds(seconds);
}

TEST(TabToShowTest, NoOtherTabs) {
  EXPECT_EQ(ChooseTabToShow({}), std::nullopt);
}

TEST(TabToShowTest, FocusedLastWins) {
  EXPECT_EQ(ChooseTabToShow({{false, At(10)}, {false, At(30)}, {false, At(20)}}), 1u);
}

TEST(TabToShowTest, AwakeBeatsAsleepFocusedLater) {
  EXPECT_EQ(ChooseTabToShow({{true, At(30)}, {false, At(10)}}), 1u);
}

TEST(TabToShowTest, OnlyAsleepTabsTakesTheOneFocusedLast) {
  EXPECT_EQ(ChooseTabToShow({{true, At(10)}, {true, At(20)}}), 1u);
}

TEST(TabToShowTest, VisibleBeatsCollapsedGroupFocusedLater) {
  EXPECT_EQ(ChooseTabToShow({{false, At(30), /*in_collapsed_group=*/true}, {true, At(10)}}), 1u);
}

TEST(TabToShowTest, OnlyCollapsedGroupTabsTakesTheOneFocusedLast) {
  EXPECT_EQ(ChooseTabToShow({{false, At(10), true}, {false, At(20), true}}), 1u);
}

TEST(TabToShowTest, TieGoesToTheEarlierTab) {
  EXPECT_EQ(ChooseTabToShow({{false, At(10)}, {false, At(10)}}), 0u);
}

}  // namespace
}  // namespace lb
