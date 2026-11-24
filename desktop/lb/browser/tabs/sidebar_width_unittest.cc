// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/sidebar_width.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

constexpr int kCollapsed = 56;
constexpr int kWidth = 300;

SidebarWidths Lay(int strip, double slide_in = 0, int uncollapsed = kWidth) {
  return LayOutSidebar(
      {.strip = strip, .collapsed = kCollapsed, .uncollapsed = uncollapsed, .slide_in = slide_in});
}

TEST(SidebarWidthTest, ShownSidebarIsChromiums) {
  const SidebarWidths widths = LayOutSidebar(
      {.strip = kWidth, .collapsed = kCollapsed, .uncollapsed = kWidth, .shown = true});
  EXPECT_EQ(widths.page_inset, kWidth);
  EXPECT_EQ(widths.strip, kWidth);
  EXPECT_TRUE(widths.drawn);
}

TEST(SidebarWidthTest, ShownSidebarSqueezedByANarrowWindowKeepsItsWidth) {
  const SidebarWidths widths =
      LayOutSidebar({.strip = 200, .collapsed = kCollapsed, .uncollapsed = kWidth, .shown = true});
  EXPECT_EQ(widths.page_inset, 200);
  EXPECT_EQ(widths.strip, 200);
  EXPECT_TRUE(widths.drawn);
}

TEST(SidebarWidthTest, HiddenSidebarTakesNoRoomAndIsNotDrawn) {
  const SidebarWidths widths = Lay(kCollapsed);
  EXPECT_EQ(widths.page_inset, 0);
  EXPECT_EQ(widths.strip, kHiddenSidebarEdgeWidth);
  EXPECT_FALSE(widths.drawn);
}

TEST(SidebarWidthTest, HidingAnimatesFromTheWidthToNothing) {
  // Halfway through Chromium's animation, halfway through ours.
  const SidebarWidths half = Lay(kCollapsed + (kWidth - kCollapsed) / 2);
  EXPECT_EQ(half.page_inset, kWidth / 2);
  EXPECT_EQ(half.strip, kWidth / 2);
  EXPECT_TRUE(half.drawn);
  // Near the end the strip keeps the edge band, so the pointer can still reach it.
  const SidebarWidths end = Lay(kCollapsed + 1);
  EXPECT_EQ(end.page_inset, 1);
  EXPECT_EQ(end.strip, kHiddenSidebarEdgeWidth);
  EXPECT_TRUE(end.drawn);
}

TEST(SidebarWidthTest, InsetNeverPassesTheWidth) {
  EXPECT_EQ(Lay(kWidth + 40).page_inset, kWidth);
}

TEST(SidebarWidthTest, SlidesInToTheUsersWidthOverThePage) {
  const SidebarWidths half = Lay(kCollapsed, 0.5);
  EXPECT_EQ(half.page_inset, 0);
  EXPECT_EQ(half.strip, kHiddenSidebarEdgeWidth + (kWidth - kHiddenSidebarEdgeWidth) / 2);
  EXPECT_TRUE(half.drawn);

  const SidebarWidths full = Lay(kCollapsed, 1.0);
  EXPECT_EQ(full.page_inset, 0);
  EXPECT_EQ(full.strip, kWidth);

  // A wider sidebar slides in wider (Chromium's own goes to its default width).
  EXPECT_EQ(Lay(kCollapsed, 1.0, 380).strip, 380);
}

TEST(SidebarWidthTest, SlideInOutOfRangeIsClamped) {
  EXPECT_EQ(Lay(kCollapsed, 1.5).strip, kWidth);
  const SidebarWidths below = Lay(kCollapsed, -0.5);
  EXPECT_EQ(below.strip, kHiddenSidebarEdgeWidth);
  EXPECT_FALSE(below.drawn);
}

TEST(SidebarWidthTest, NoRangeKeepsChromiumsWidth) {
  const SidebarWidths widths =
      LayOutSidebar({.strip = 50, .collapsed = kCollapsed, .uncollapsed = kCollapsed});
  EXPECT_EQ(widths.page_inset, 50);
  EXPECT_EQ(widths.strip, 50);
}

TEST(SidebarWidthTest, ShowingASlidInSidebarKeepsItsWidth) {
  // While the page makes room, Chromium's slide-in value runs back to 0: the strip must stay at
  // the sidebar's width throughout, not dip and grow back.
  for (double shown : {0.1, 0.25, 0.5, 0.75, 0.9}) {
    const int strip = kCollapsed + static_cast<int>((kWidth - kCollapsed) * shown);
    const SidebarWidths widths = Lay(strip, /*slide_in=*/1.0 - shown);
    EXPECT_EQ(widths.strip, kWidth) << shown;
    EXPECT_TRUE(widths.drawn);
  }
}

TEST(SidebarWidthTest, WithoutExpandOnHoverAHiddenSidebarIsChromiumsColumn) {
  // Nothing would slide it in, so it stays visible and takes its room, as Chromium's does.
  const SidebarWidths widths = LayOutSidebar(
      {.strip = kCollapsed, .collapsed = kCollapsed, .uncollapsed = kWidth, .slides_in = false});
  EXPECT_EQ(widths.page_inset, kCollapsed);
  EXPECT_EQ(widths.strip, kCollapsed);
  EXPECT_TRUE(widths.drawn);
}

TEST(SidebarWidthTest, ResizeArea) {
  // Shown: Chromium's full-width area.
  EXPECT_EQ(SidebarResizeAreaFor(false, true, false, false),
            (SidebarResizeArea{.visible = true, .wide = true}));
  // Hidden and not slid in: none (the band only slides the sidebar in).
  EXPECT_EQ(SidebarResizeAreaFor(true, true, false, false), SidebarResizeArea());
  // Slid in, also once a resize is done (the strip stays collapsed): full width.
  EXPECT_EQ(SidebarResizeAreaFor(true, true, true, false),
            (SidebarResizeArea{.visible = true, .wide = true}));
  // A drag under way keeps it.
  EXPECT_TRUE(SidebarResizeAreaFor(true, true, false, true).visible);
  // Chromium's column of icons (expand on hover off): its narrow area.
  EXPECT_EQ(SidebarResizeAreaFor(true, false, false, false),
            (SidebarResizeArea{.visible = true, .wide = false}));
}

TEST(SidebarWidthTest, ResizesSlidInSidebar) {
  EXPECT_TRUE(ResizesSlidInSidebar(/*collapsed=*/true, /*slid_in=*/true));
  // A drag that starts on the shown sidebar stays Chromium's, even if the strip collapses and
  // slides in under the pointer before it ends.
  EXPECT_FALSE(ResizesSlidInSidebar(false, false));
  EXPECT_FALSE(ResizesSlidInSidebar(true, false));
}

TEST(SidebarWidthTest, SlidInSidebarResizesBetweenMinAndMax) {
  EXPECT_EQ(SlidInSidebarWidth(250, 126, 400), 250);
  // Dragged far to the left it stops at the narrowest width; it never hides.
  EXPECT_EQ(SlidInSidebarWidth(20, 126, 400), 126);
  EXPECT_EQ(SlidInSidebarWidth(900, 126, 400), 400);
  // A max below the min can't make it narrower than the min.
  EXPECT_EQ(SlidInSidebarWidth(300, 126, 100), 126);
}

}  // namespace
}  // namespace lb
