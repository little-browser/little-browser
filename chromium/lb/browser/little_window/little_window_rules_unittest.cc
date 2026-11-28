// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/little_window/little_window_rules.h"

#include <cstdlib>

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace lb {
namespace {

const gfx::Rect kScreen(0, 25, 1728, 1080);  // below the menu bar
constexpr int kChrome = 66;                  // title bar and toolbar row

// The whole window a page rect from LittleWindowBounds becomes.
gfx::Rect Window(const gfx::Rect& page) {
  return gfx::Rect(page.origin(), gfx::Size(page.width(), page.height() + kChrome));
}

const gfx::Rect kParent(100, 100, 1400, 900);  // the window with tabs it opens over

TEST(LittleWindowRulesTest, MostOfItsParentCentredOnIt) {
  const gfx::Rect page = LittleWindowBounds(kScreen, kParent, std::nullopt, kChrome);
  EXPECT_EQ(Window(page), gfx::Rect(170, 145, 1260, 810));  // 90% each way
  EXPECT_EQ(Window(page).CenterPoint(), kParent.CenterPoint());
  EXPECT_EQ(page.height(), 810 - kChrome);
}

TEST(LittleWindowRulesTest, WithoutAParentMostOfTheScreen) {
  const gfx::Rect page = LittleWindowBounds(kScreen, std::nullopt, std::nullopt, kChrome);
  EXPECT_EQ(Window(page).size(), gfx::Size(1555, 972));
  // Centred, to the pixel an odd width allows.
  EXPECT_LE(std::abs(Window(page).CenterPoint().x() - kScreen.CenterPoint().x()), 1);
  EXPECT_EQ(Window(page).CenterPoint().y(), kScreen.CenterPoint().y());
}

TEST(LittleWindowRulesTest, NextOneStepsDownAndRight) {
  const gfx::Rect first = LittleWindowBounds(kScreen, kParent, std::nullopt, kChrome);
  const gfx::Rect second = LittleWindowBounds(kScreen, kParent, Window(first), kChrome);
  EXPECT_EQ(second, first + gfx::Vector2d(28, 28));
}

TEST(LittleWindowRulesTest, BackOverItsParentWhenTheStepLeavesTheScreen) {
  const gfx::Rect first = LittleWindowBounds(kScreen, kParent, std::nullopt, kChrome);
  const gfx::Rect near_edge(kScreen.right() - 1270, kScreen.bottom() - 820, 1260, 810);
  const gfx::Rect page = LittleWindowBounds(kScreen, kParent, near_edge, kChrome);
  EXPECT_EQ(page, first);
}

TEST(LittleWindowRulesTest, AParentPartlyOffTheScreenKeepsItOnTheScreen) {
  const gfx::Rect parent(-600, 25, 1400, 900);  // mostly off the screen's left edge
  const gfx::Rect page = LittleWindowBounds(kScreen, parent, std::nullopt, kChrome);
  EXPECT_TRUE(kScreen.Contains(Window(page)));
  // Sized from the part of the parent on the screen.
  EXPECT_EQ(Window(page).width(), 720);
}

TEST(LittleWindowRulesTest, SmallScreenKeepsAMargin) {
  const gfx::Rect small(0, 0, 800, 600);
  const gfx::Rect page = LittleWindowBounds(small, small, std::nullopt, kChrome);
  EXPECT_EQ(Window(page), gfx::Rect(40, 30, 720, 540));
  const gfx::Rect tiny(0, 0, 400, 300);
  // 90% would leave less than the 24 px margin: the margin wins.
  EXPECT_EQ(Window(LittleWindowBounds(tiny, tiny, std::nullopt, kChrome)).width(), 352);
}

TEST(LittleWindowRulesTest, NoRoomAtAll) {
  EXPECT_TRUE(
      LittleWindowBounds(gfx::Rect(0, 0, 30, 30), std::nullopt, std::nullopt, kChrome).IsEmpty());
}

TEST(LittleWindowRulesTest, OneRowAsHighAsItsTallerPart) {
  EXPECT_EQ(LittleWindowRowHeight(38, 34), 38);  // the window buttons
  EXPECT_EQ(LittleWindowRowHeight(38, 44), 44);  // the toolbar
  EXPECT_EQ(LittleWindowRowHeight(0, 0), 0);
  EXPECT_EQ(LittleWindowRowHeight(-1, -1), 0);
}

TEST(LittleWindowRulesTest, ToolbarBesideTheWindowButtons) {
  // The buttons at the leading edge: the toolbar starts after them, at the top, to the far edge.
  EXPECT_EQ(LittleWindowToolbarBounds(gfx::Rect(0, 0, 800, 600), 86, 0, 38, 34),
            gfx::Rect(86, 0, 714, 38));
  // Buttons at both edges (and an area not at the origin).
  EXPECT_EQ(LittleWindowToolbarBounds(gfx::Rect(10, 5, 800, 600), 86, 40, 38, 34),
            gfx::Rect(96, 5, 674, 38));
  // A taller toolbar makes the row taller.
  EXPECT_EQ(LittleWindowToolbarBounds(gfx::Rect(0, 0, 800, 600), 86, 0, 38, 44),
            gfx::Rect(86, 0, 714, 44));
}

TEST(LittleWindowRulesTest, ToolbarTakesTheWholeRowWithoutWindowButtons) {
  // Fullscreen: no buttons in the row.
  EXPECT_EQ(LittleWindowToolbarBounds(gfx::Rect(0, 0, 800, 600), 0, 0, 38, 34),
            gfx::Rect(0, 0, 800, 38));
}

TEST(LittleWindowRulesTest, ToolbarNeverNegative) {
  // A window narrower than its buttons: an empty toolbar, never a negative one.
  EXPECT_EQ(LittleWindowToolbarBounds(gfx::Rect(0, 0, 60, 600), 86, 0, 38, 34),
            gfx::Rect(60, 0, 0, 38));
  EXPECT_EQ(LittleWindowToolbarBounds(gfx::Rect(0, 0, 100, 600), 86, 40, 38, 34),
            gfx::Rect(86, 0, 0, 38));
  EXPECT_EQ(LittleWindowToolbarBounds(gfx::Rect(0, 0, 100, 600), -5, -5, 38, 34),
            gfx::Rect(0, 0, 100, 38));
}

TEST(LittleWindowRulesTest, OpenInSpaceLabel) {
  EXPECT_EQ(OpenInSpaceLabel("Work"), u"Open in Work");
  EXPECT_EQ(OpenInSpaceLabel("Ma vie privée"), u"Open in Ma vie privée");
  EXPECT_EQ(OpenInSpaceLabel(""), u"Open in a tab");
}

TEST(LittleWindowRulesTest, WebPagesAndFilesFromOtherAppsOpenInLittleWindows) {
  EXPECT_TRUE(OpensFromOtherAppInLittleWindow(GURL("https://example.com/a"), false));
  EXPECT_TRUE(OpensFromOtherAppInLittleWindow(GURL("http://example.com/"), false));
  EXPECT_TRUE(OpensFromOtherAppInLittleWindow(GURL("file:///Users/me/page.html"), false));
}

TEST(LittleWindowRulesTest, BrowserPagesAndUntrustedLinksFromOtherAppsOpenAsTabs) {
  EXPECT_FALSE(OpensFromOtherAppInLittleWindow(GURL("chrome://settings/"), false));
  EXPECT_FALSE(OpensFromOtherAppInLittleWindow(GURL("chrome-extension://abc/page.html"), false));
  EXPECT_FALSE(OpensFromOtherAppInLittleWindow(GURL("about:blank"), false));
  EXPECT_FALSE(OpensFromOtherAppInLittleWindow(GURL("not a url"), false));
  EXPECT_FALSE(OpensFromOtherAppInLittleWindow(GURL("https://example.com/"), true));
}

TEST(LittleWindowRulesTest, CommandLineLinksOpenInLittleWindows) {
  EXPECT_TRUE(OpensCommandLineInLittleWindows(
      {.links = 2, .little_window_links = 2, .can_open_window = true}));
}

TEST(LittleWindowRulesTest, CommandLineGoesToChromiumOtherwise) {
  // No links (a plain second launch opens a window as Chromium does).
  EXPECT_FALSE(OpensCommandLineInLittleWindows({.can_open_window = true}));
  // One of them opens as a tab (chrome://settings): Chromium opens them all, none twice.
  EXPECT_FALSE(OpensCommandLineInLittleWindows(
      {.links = 2, .little_window_links = 1, .can_open_window = true}));
  // An app launch.
  EXPECT_FALSE(OpensCommandLineInLittleWindows(
      {.links = 1, .little_window_links = 1, .app_launch = true, .can_open_window = true}));
  // No window can open in that profile (forced incognito asked for the regular one, shutdown):
  // Chromium decides, rather than the links being lost.
  EXPECT_FALSE(OpensCommandLineInLittleWindows({.links = 1, .little_window_links = 1}));
}

}  // namespace
}  // namespace lb
