// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/little_window/little_window_rules.h"

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

TEST(LittleWindowRulesTest, FirstOneIsCentredWithItsChrome) {
  const gfx::Rect page = LittleWindowBounds(kScreen, std::nullopt, kChrome);
  EXPECT_EQ(page.size(), kLittleWindowSize);
  EXPECT_EQ(Window(page).CenterPoint(), kScreen.CenterPoint());
}

TEST(LittleWindowRulesTest, NextOneStepsDownAndRight) {
  const gfx::Rect first = LittleWindowBounds(kScreen, std::nullopt, kChrome);
  const gfx::Rect second = LittleWindowBounds(kScreen, Window(first), kChrome);
  EXPECT_EQ(second, first + gfx::Vector2d(28, 28));
}

TEST(LittleWindowRulesTest, BackToTheCentreWhenTheStepLeavesTheScreen) {
  const gfx::Rect near_edge(kScreen.right() - 970, kScreen.bottom() - 760, 960, 746);
  const gfx::Rect page = LittleWindowBounds(kScreen, near_edge, kChrome);
  EXPECT_EQ(Window(page).CenterPoint(), kScreen.CenterPoint());
}

TEST(LittleWindowRulesTest, StepsStopWhenTheWindowNotThePageWouldLeaveTheScreen) {
  // A 1440 x 900 screen: the page of the next step would still fit (173 + 680 <= 900), the window
  // with its chrome would not (173 + 746 > 900), so it goes back to the centre instead of being
  // pushed up onto the last one.
  const gfx::Rect screen(0, 25, 1440, 875);
  const gfx::Rect newest(300, 145, 960, 746);
  const gfx::Rect page = LittleWindowBounds(screen, newest, kChrome);
  EXPECT_EQ(Window(page).CenterPoint(), screen.CenterPoint());
  EXPECT_TRUE(screen.Contains(Window(page)));
}

TEST(LittleWindowRulesTest, SmallScreenGetsASmallerPage) {
  const gfx::Rect small(0, 0, 800, 600);
  const gfx::Rect page = LittleWindowBounds(small, std::nullopt, kChrome);
  EXPECT_EQ(page, gfx::Rect(24, 24, 752, 486));  // the window fits with 24 to spare
  EXPECT_EQ(Window(page).bottom(), 600 - 24);
}

TEST(LittleWindowRulesTest, NoRoomAtAll) {
  EXPECT_TRUE(LittleWindowBounds(gfx::Rect(0, 0, 30, 30), std::nullopt, kChrome).IsEmpty());
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
