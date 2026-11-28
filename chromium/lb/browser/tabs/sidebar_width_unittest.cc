// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/sidebar_width.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

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
  EXPECT_FALSE(widths.full_height);  // Chromium places it
}

TEST(SidebarWidthTest, ShownSidebarSqueezedByANarrowWindowKeepsItsWidth) {
  const SidebarWidths widths =
      LayOutSidebar({.strip = 200, .collapsed = kCollapsed, .uncollapsed = kWidth, .shown = true});
  EXPECT_EQ(widths.page_inset, 200);
  EXPECT_EQ(widths.strip, 200);
  EXPECT_TRUE(widths.drawn);
}

// In fullscreen the page has no card margins: hidden, it takes the whole width, the band over it.
TEST(SidebarWidthTest, HiddenSidebarWithoutACardLeavesThePageTheWholeWidth) {
  const SidebarWidths hidden = LayOutSidebar(
      {.strip = kCollapsed, .collapsed = kCollapsed, .uncollapsed = kWidth, .page_is_card = false});
  EXPECT_EQ(hidden.page_inset, 0);
  EXPECT_EQ(hidden.strip, kHiddenSidebarEdgeWidth);
  EXPECT_FALSE(hidden.drawn);
  EXPECT_TRUE(hidden.full_height);
  // Hiding still animates to it, and is drawn until the end.
  const SidebarWidths end = LayOutSidebar({.strip = kCollapsed + 1,
                                           .collapsed = kCollapsed,
                                           .uncollapsed = kWidth,
                                           .page_is_card = false});
  EXPECT_GT(end.page_inset, 0);
  EXPECT_TRUE(end.drawn);
  // Slid in over the page: as with a card.
  const SidebarWidths slid = LayOutSidebar({.strip = kCollapsed,
                                            .collapsed = kCollapsed,
                                            .uncollapsed = kWidth,
                                            .slide_in = 1.0,
                                            .page_is_card = false});
  EXPECT_EQ(slid.page_inset, 0);
  EXPECT_EQ(slid.strip, kWidth);
  EXPECT_TRUE(slid.drawn);
}

// The page leaves the edge band, where the window's background shows the pointer where to go.
TEST(SidebarWidthTest, HiddenSidebarLeavesOnlyTheEdgeBandAndIsNotDrawn) {
  const SidebarWidths widths = Lay(kCollapsed);
  EXPECT_EQ(widths.page_inset, kHiddenSidebarEdgeWidth);
  EXPECT_EQ(widths.strip, kHiddenSidebarEdgeWidth);
  EXPECT_FALSE(widths.drawn);
  // The whole left edge slides it in, beside the toolbar row too.
  EXPECT_TRUE(widths.full_height);
}

TEST(SidebarWidthTest, HidingAnimatesFromTheWidthToTheEdgeBand) {
  // Halfway through Chromium's animation, halfway through ours.
  const SidebarWidths half = Lay(kCollapsed + (kWidth - kCollapsed) / 2);
  EXPECT_EQ(half.page_inset, kHiddenSidebarEdgeWidth + (kWidth - kHiddenSidebarEdgeWidth) / 2);
  EXPECT_EQ(half.strip, half.page_inset);
  EXPECT_TRUE(half.drawn);
  // Just before the end it is still drawn, a little wider than the band.
  const SidebarWidths end = Lay(kCollapsed + 1);
  EXPECT_GT(end.page_inset, kHiddenSidebarEdgeWidth);
  EXPECT_EQ(end.strip, end.page_inset);
  EXPECT_TRUE(end.drawn);
  // Hiding is Chromium's animation, top and all.
  EXPECT_FALSE(half.full_height);
  EXPECT_FALSE(end.full_height);
}

TEST(SidebarWidthTest, InsetNeverPassesTheWidth) {
  EXPECT_EQ(Lay(kWidth + 40).page_inset, kWidth);
}

TEST(SidebarWidthTest, SlidesInToTheUsersWidthOverThePage) {
  const SidebarWidths half = Lay(kCollapsed, 0.5);
  EXPECT_EQ(half.page_inset, kHiddenSidebarEdgeWidth);
  EXPECT_EQ(half.strip, kHiddenSidebarEdgeWidth + (kWidth - kHiddenSidebarEdgeWidth) / 2);
  EXPECT_TRUE(half.drawn);

  const SidebarWidths full = Lay(kCollapsed, 1.0);
  EXPECT_EQ(full.page_inset, kHiddenSidebarEdgeWidth);
  EXPECT_EQ(full.strip, kWidth);
  // Slid in, it runs the window's full height, as Arc's does.
  EXPECT_TRUE(half.full_height);
  EXPECT_TRUE(full.full_height);

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
    // It stays full height until it is no longer slid in, so its top doesn't jump down.
    EXPECT_TRUE(widths.full_height) << shown;
  }
}

TEST(SidebarWidthTest, WithoutExpandOnHoverAHiddenSidebarIsChromiumsColumn) {
  // Nothing would slide it in, so it stays visible and takes its room, as Chromium's does.
  const SidebarWidths widths = LayOutSidebar(
      {.strip = kCollapsed, .collapsed = kCollapsed, .uncollapsed = kWidth, .slides_in = false});
  EXPECT_EQ(widths.page_inset, kCollapsed);
  EXPECT_EQ(widths.strip, kCollapsed);
  EXPECT_TRUE(widths.drawn);
  EXPECT_FALSE(widths.full_height);
}

// The pointer up to 60 pt left of the window, within its height, slides a hidden sidebar in.
TEST(SidebarWidthTest, OutsideZoneIsLeftOfTheWindowWithinItsHeight) {
  const gfx::Rect window(300, 100, 1200, 800);
  const gfx::Rect display(0, 0, 1920, 1080);
  EXPECT_TRUE(InSidebarOutsideZone({299, 100}, window, display));
  EXPECT_TRUE(InSidebarOutsideZone({300 - kSidebarOutsideReach, 899}, window, display));
  EXPECT_TRUE(InSidebarOutsideZone({270, 500}, window, display));
  // Farther left, above or below the window: not in it.
  EXPECT_FALSE(InSidebarOutsideZone({300 - kSidebarOutsideReach - 1, 500}, window, display));
  EXPECT_FALSE(InSidebarOutsideZone({270, 99}, window, display));
  EXPECT_FALSE(InSidebarOutsideZone({270, 900}, window, display));
  // The window itself is the strip's: not the zone.
  EXPECT_FALSE(InSidebarOutsideZone({300, 500}, window, display));
  EXPECT_FALSE(InSidebarOutsideZone({800, 500}, window, display));
  // Right of the window: nothing.
  EXPECT_FALSE(InSidebarOutsideZone({1520, 500}, window, display));
}

TEST(SidebarWidthTest, NoOutsideZoneWithoutAWindow) {
  EXPECT_FALSE(InSidebarOutsideZone({-10, 5}, gfx::Rect(), gfx::Rect(0, 0, 1920, 1080)));
}

// A window at its display's left edge has no zone, even with another display to its left, where
// the pointer would be on that display's right edge (its windows' scrollbars).
TEST(SidebarWidthTest, OutsideZoneStaysOnTheWindowsDisplay) {
  const gfx::Rect right_display(1920, 0, 1920, 1080);
  const gfx::Rect maximized(1920, 25, 1920, 1055);
  EXPECT_FALSE(InSidebarOutsideZone({1900, 500}, maximized, right_display));
  EXPECT_FALSE(InSidebarOutsideZone({1919, 500}, maximized, right_display));
  // Partly off: only the part on its display counts.
  const gfx::Rect near_edge(1950, 25, 1200, 800);
  EXPECT_TRUE(InSidebarOutsideZone({1930, 500}, near_edge, right_display));
  EXPECT_FALSE(InSidebarOutsideZone({1900, 500}, near_edge, right_display));
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
