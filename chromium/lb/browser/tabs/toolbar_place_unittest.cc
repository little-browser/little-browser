// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/toolbar_place.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

TEST(ToolbarPlaceTest, ANormalWindowWithTheSidebarHasItInTheSidebar) {
  EXPECT_EQ(ToolbarPlaceFor(/*normal_window=*/true, /*vertical_tabs=*/true, /*column=*/false),
            ToolbarPlace::kSidebar);
}

TEST(ToolbarPlaceTest, PopupsAndTabsOnTopKeepChromiumsRow) {
  EXPECT_EQ(ToolbarPlaceFor(false, true, false), ToolbarPlace::kTopRow);  // a little window
  EXPECT_EQ(ToolbarPlaceFor(true, false, false), ToolbarPlace::kTopRow);  // tabs on top
}

TEST(ToolbarPlaceTest, ChromiumsColumnOfIconsKeepsChromiumsRow) {
  // Hidden with expand on hover off: a 56 px column, too narrow for the toolbar.
  EXPECT_EQ(ToolbarPlaceFor(true, true, /*column=*/true), ToolbarPlace::kTopRow);
}

TEST(ToolbarPlaceTest, WindowButtonsHideWithAHiddenSidebar) {
  EXPECT_TRUE(WindowButtonsShown(ToolbarPlace::kSidebar, /*sidebar_hidden=*/false, false, false));
  EXPECT_FALSE(WindowButtonsShown(ToolbarPlace::kSidebar, true, /*slid_in=*/false, false));
  EXPECT_TRUE(WindowButtonsShown(ToolbarPlace::kSidebar, true, /*slid_in=*/true, false));
  // With Chromium's row they are always there.
  EXPECT_TRUE(WindowButtonsShown(ToolbarPlace::kTopRow, true, false, false));
}

TEST(ToolbarPlaceTest, WindowButtonsShowInFullscreen) {
  // macOS's title bar at the top of the screen has them, also with the sidebar hidden.
  EXPECT_TRUE(WindowButtonsShown(ToolbarPlace::kSidebar, /*sidebar_hidden=*/true,
                                 /*slid_in=*/false, /*fullscreen=*/true));
}

TEST(ToolbarPlaceTest, ABubbleFromAHiddenSidebarsToolbarKeepsItOut) {
  EXPECT_TRUE(KeepsSidebarOutForBubble(ToolbarPlace::kSidebar, /*sidebar_hidden=*/true,
                                       /*bubble_from_sidebar=*/true));
  EXPECT_FALSE(KeepsSidebarOutForBubble(ToolbarPlace::kSidebar, true, false));
  EXPECT_FALSE(KeepsSidebarOutForBubble(ToolbarPlace::kSidebar, false, true));
  EXPECT_FALSE(KeepsSidebarOutForBubble(ToolbarPlace::kTopRow, true, true));
}

TEST(ToolbarPlaceTest, TheRowsButtonsStartAfterTheWindowButtons) {
  EXPECT_EQ(ToolbarLeadingInset(86, 8), 78);  // the toolbar a little in from the window's edge
  EXPECT_EQ(ToolbarLeadingInset(86, 0), 86);
}

TEST(ToolbarPlaceTest, NoInsetWithoutWindowButtonsInTheWay) {
  EXPECT_EQ(ToolbarLeadingInset(0, 8), 0);    // fullscreen: no window buttons
  EXPECT_EQ(ToolbarLeadingInset(86, 86), 0);  // a toolbar starting after them
  EXPECT_EQ(ToolbarLeadingInset(86, 120), 0);
}

}  // namespace
}  // namespace lb
