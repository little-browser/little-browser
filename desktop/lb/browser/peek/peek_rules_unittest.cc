// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/peek/peek_rules.h"

#include "chrome/app/chrome_command_ids.h"
#include "lb/browser/ui/lb_command_ids.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace lb {
namespace {

LinkOpen ShiftClick(WindowKind window = WindowKind::kNormal) {
  return {WindowOpenDisposition::NEW_WINDOW, /*renderer_initiated=*/true, /*user_gesture=*/true,
          window};
}

TEST(PeekRulesTest, ShiftClickOpensInPeekInNormalAndLittleWindows) {
  EXPECT_TRUE(OpensInPeek(ShiftClick(WindowKind::kNormal)));
  EXPECT_TRUE(OpensInPeek(ShiftClick(WindowKind::kLittle)));
  EXPECT_FALSE(OpensInPeek(ShiftClick(WindowKind::kOther)));
}

TEST(PeekRulesTest, MenuItemOpenLinkInNewWindowStillOpensAWindow) {
  LinkOpen open = ShiftClick();
  open.renderer_initiated = false;  // the context menu's "Open Link in New Window"
  EXPECT_FALSE(OpensInPeek(open));
}

TEST(PeekRulesTest, ScriptWithoutAClickOpensNoPeek) {
  LinkOpen open = ShiftClick();
  open.user_gesture = false;
  EXPECT_FALSE(OpensInPeek(open));
}

TEST(PeekRulesTest, ScriptDispatchedShiftClickOpensNoPeek) {
  // During the user's click, the page dispatches a Shift+click of its own on a link it chose.
  LinkOpen open = ShiftClick();
  open.untrusted_event = true;
  EXPECT_FALSE(OpensInPeek(open));
}

TEST(PeekRulesTest, OtherDispositionsOpenNoPeek) {
  for (WindowOpenDisposition d :
       {WindowOpenDisposition::CURRENT_TAB, WindowOpenDisposition::NEW_FOREGROUND_TAB,
        WindowOpenDisposition::NEW_BACKGROUND_TAB, WindowOpenDisposition::NEW_POPUP,
        WindowOpenDisposition::SAVE_TO_DISK, WindowOpenDisposition::OFF_THE_RECORD}) {
    LinkOpen open = ShiftClick();
    open.disposition = d;
    EXPECT_FALSE(OpensInPeek(open)) << static_cast<int>(d);
  }
}

TEST(PeekRulesTest, LinksInThePeek) {
  LinkOpen same = ShiftClick();
  same.disposition = WindowOpenDisposition::CURRENT_TAB;
  EXPECT_EQ(ActionForLinkInPeek(same), PeekLinkAction::kLoadInPeek);
  EXPECT_EQ(ActionForLinkInPeek(ShiftClick()), PeekLinkAction::kReplacePeek);
  for (WindowOpenDisposition d :
       {WindowOpenDisposition::NEW_FOREGROUND_TAB, WindowOpenDisposition::NEW_BACKGROUND_TAB,
        WindowOpenDisposition::NEW_POPUP, WindowOpenDisposition::SAVE_TO_DISK,
        WindowOpenDisposition::OFF_THE_RECORD, WindowOpenDisposition::SINGLETON_TAB,
        WindowOpenDisposition::NEW_PICTURE_IN_PICTURE}) {
    LinkOpen open = ShiftClick();
    open.disposition = d;
    EXPECT_EQ(ActionForLinkInPeek(open), PeekLinkAction::kOpenInWindow) << static_cast<int>(d);
  }
}

TEST(PeekRulesTest, MenuItemOpenLinkInNewWindowInThePeekOpensAWindow) {
  LinkOpen open = ShiftClick();
  open.renderer_initiated = false;
  EXPECT_EQ(ActionForLinkInPeek(open), PeekLinkAction::kOpenInWindow);
}

TEST(PeekRulesTest, NewWindowWithoutTheUsersShiftClickDoesNotReplaceThePeek) {
  LinkOpen no_gesture = ShiftClick();
  no_gesture.user_gesture = false;
  EXPECT_EQ(ActionForLinkInPeek(no_gesture), PeekLinkAction::kOpenInWindow);
  LinkOpen scripted = ShiftClick();
  scripted.untrusted_event = true;
  EXPECT_EQ(ActionForLinkInPeek(scripted), PeekLinkAction::kOpenInWindow);
}

TEST(PeekRulesTest, ATabInFrontFromThePeekKeepsItsPageAsATab) {
  for (WindowOpenDisposition d :
       {WindowOpenDisposition::NEW_FOREGROUND_TAB, WindowOpenDisposition::SINGLETON_TAB,
        WindowOpenDisposition::SWITCH_TO_TAB, WindowOpenDisposition::NEW_SPLIT_VIEW}) {
    EXPECT_TRUE(PromotesPeekBeforeOpening(d, WindowKind::kNormal)) << static_cast<int>(d);
    // A little window's tabs open in the space's window: the Peek stays.
    EXPECT_FALSE(PromotesPeekBeforeOpening(d, WindowKind::kLittle)) << static_cast<int>(d);
  }
  for (WindowOpenDisposition d :
       {WindowOpenDisposition::NEW_BACKGROUND_TAB, WindowOpenDisposition::NEW_POPUP,
        WindowOpenDisposition::NEW_WINDOW, WindowOpenDisposition::SAVE_TO_DISK,
        WindowOpenDisposition::NEW_PICTURE_IN_PICTURE}) {
    EXPECT_FALSE(PromotesPeekBeforeOpening(d, WindowKind::kNormal)) << static_cast<int>(d);
  }
}

TEST(PeekRulesTest, OpenAsTabGoesToThisWindowOnlyIfItHasTabs) {
  EXPECT_EQ(TargetForOpenAsTab(WindowKind::kNormal), OpenAsTabTarget::kThisWindow);
  EXPECT_EQ(TargetForOpenAsTab(WindowKind::kLittle), OpenAsTabTarget::kSpaceWindow);
}

TEST(PeekRulesTest, SplitViewOnlyBesideAPageNotSplitAlready) {
  EXPECT_TRUE(CanOpenPeekInSplitView(WindowKind::kNormal, false));
  EXPECT_FALSE(CanOpenPeekInSplitView(WindowKind::kNormal, true));
  EXPECT_FALSE(CanOpenPeekInSplitView(WindowKind::kLittle, false));
}

TEST(PeekRulesTest, CommandsCloseThePeekOrActOnItsPage) {
  EXPECT_EQ(PeekCommandActionFor(IDC_CLOSE_TAB), PeekCommandAction::kClosePeek);
  EXPECT_EQ(PeekCommandActionFor(IDC_CLOSE_FIND_OR_STOP), PeekCommandAction::kClosePeek);
  for (int id :
       {IDC_RELOAD, IDC_RELOAD_BYPASSING_CACHE, IDC_RELOAD_CLEARING_CACHE, IDC_STOP, IDC_BACK,
        IDC_FORWARD, IDC_ZOOM_PLUS, IDC_ZOOM_MINUS, IDC_ZOOM_NORMAL, IDC_COPY_URL}) {
    EXPECT_EQ(PeekCommandActionFor(id), PeekCommandAction::kForPeekPage) << id;
  }
}

TEST(PeekRulesTest, PageCommandsThePeekCantRunDoNothing) {
  // They would otherwise act on the dimmed page under the Peek.
  for (int id : {IDC_FIND, IDC_FIND_NEXT, IDC_FIND_PREVIOUS, IDC_PRINT, IDC_BASIC_PRINT,
                 IDC_SAVE_PAGE, IDC_VIEW_SOURCE, IDC_DEV_TOOLS, IDC_DEV_TOOLS_CONSOLE,
                 IDC_DEV_TOOLS_INSPECT, IDC_DEV_TOOLS_TOGGLE, IDC_FOCUS_LOCATION, IDC_HOME,
                 IDC_BOOKMARK_THIS_TAB, IDC_DUPLICATE_TAB}) {
    EXPECT_EQ(PeekCommandActionFor(id), PeekCommandAction::kIgnore) << id;
  }
}

TEST(PeekRulesTest, WindowCommandsPassThrough) {
  for (int id : {IDC_NEW_TAB, IDC_NEW_WINDOW, IDC_NEW_INCOGNITO_WINDOW, IDC_SELECT_NEXT_TAB,
                 IDC_CLOSE_WINDOW, IDC_EXIT, IDC_OPTIONS, IDC_SHOW_HISTORY, IDC_FULLSCREEN,
                 IDC_NEW_LITTLE_WINDOW}) {
    EXPECT_EQ(PeekCommandActionFor(id), PeekCommandAction::kPass) << id;
  }
}

// A window with the sidebar on the left and the toolbar on top of the page.
const gfx::Rect kWindow(0, 0, 1100, 900);
const gfx::Rect kPage(100, 50, 1000, 800);

TEST(PeekRulesTest, EscClosesThePeekBeforeThePageSeesIt) {
  EXPECT_TRUE(ClosesPeekBeforePage({.escape = true, .key_down = true}));
}

TEST(PeekRulesTest, EscComingUpOrWithModifiersLeavesThePeek) {
  EXPECT_FALSE(ClosesPeekBeforePage({.escape = true}));
  EXPECT_FALSE(ClosesPeekBeforePage({.escape = true, .modified = true, .key_down = true}));
}

TEST(PeekRulesTest, OtherKeysGoToThePage) {
  EXPECT_FALSE(ClosesPeekBeforePage({.key_down = true}));
  EXPECT_FALSE(ClosesPeekBeforePage({.modified = true, .key_down = true}));
}

TEST(PeekRulesTest, CardIsAsTallAsTheWindowAndALittleNarrowerThanThePage) {
  const gfx::Rect card = PeekCardBounds(kPage, kWindow);
  // 8.5% of the page's width on each side; the window's height less the label's row above (32),
  // down to the window's bottom edge.
  EXPECT_EQ(card, gfx::Rect(185, 32, 830, 868));
  EXPECT_EQ(card.bottom(), kWindow.bottom());
  EXPECT_EQ(card.CenterPoint().x(), kPage.CenterPoint().x());
}

TEST(PeekRulesTest, SiteLabelIsAboveTheCard) {
  const gfx::Rect card = PeekCardBounds(kPage, kWindow);
  const gfx::Rect label = PeekLabelBounds(card);
  EXPECT_EQ(label, gfx::Rect(185, 6, 830, kPeekLabelHeight));
  EXPECT_LT(label.bottom(), card.y());
  EXPECT_GE(label.y(), kWindow.y());
}

TEST(PeekRulesTest, NarrowPageKeepsRoomForTheButtons) {
  // 8.5% of 400 is 34: less than a button and its margins, so 56 on each side.
  const gfx::Rect window(0, 0, 400, 300);
  EXPECT_EQ(PeekCardBounds(window, window), gfx::Rect(56, 32, 288, 268));
  EXPECT_EQ(PeekCardBounds(gfx::Rect(0, 0, 100, 300), window).width(), 0);
  EXPECT_TRUE(PeekCardBounds(gfx::Rect(), gfx::Rect()).IsEmpty());
}

TEST(PeekRulesTest, ButtonsInAColumnRightOfTheCard) {
  const gfx::Rect page = kPage;
  const gfx::Rect card = PeekCardBounds(page, kWindow);
  const gfx::Rect first = PeekControlBounds(page, card, 0);
  const gfx::Rect second = PeekControlBounds(page, card, 1);
  EXPECT_EQ(first, gfx::Rect(1015 + (85 - 32) / 2, 44, 32, 32));
  EXPECT_EQ(second, first + gfx::Vector2d(0, 40));
  EXPECT_GT(first.x(), card.right());
  EXPECT_LE(first.right(), page.right());
}

TEST(PeekRulesTest, SplitViewIsTheRightHalf) {
  EXPECT_EQ(PeekSplitBounds(gfx::Rect(100, 50, 1001, 800)), gfx::Rect(600, 50, 501, 800));
}

TEST(PeekRulesTest, GrowsFromThePointer) {
  const gfx::Rect page(0, 0, 1000, 800);
  const gfx::Rect card = PeekCardBounds(page, page);
  const gfx::Rect start = PeekStartBounds(page, card, gfx::Point(300, 200));
  EXPECT_EQ(start.CenterPoint(), gfx::Point(300, 200));
  EXPECT_EQ(start.size(), gfx::Size(124, 115));  // 15% of 830 x 768
}

TEST(PeekRulesTest, StartStaysInsideThePage) {
  const gfx::Rect page(0, 0, 1000, 800);
  const gfx::Rect card = PeekCardBounds(page, page);
  const gfx::Rect corner = PeekStartBounds(page, card, gfx::Point(2, 798));
  EXPECT_TRUE(page.Contains(corner));
  EXPECT_EQ(corner.x(), 0);
  EXPECT_EQ(corner.bottom(), 800);
}

TEST(PeekRulesTest, PointerOffThePageGrowsFromTheCentre) {
  // E.g. Shift+Enter on a link with the pointer over the sidebar.
  const gfx::Rect page(100, 100, 1000, 800);
  const gfx::Rect card = PeekCardBounds(page, page);
  EXPECT_EQ(PeekStartBounds(page, card, gfx::Point(5, 5)).CenterPoint(), page.CenterPoint());
}

TEST(PeekRulesTest, StartIsNeverEmpty) {
  const gfx::Rect page(0, 0, 4, 4);
  EXPECT_FALSE(PeekStartBounds(page, PeekCardBounds(page, page), gfx::Point(1, 1)).IsEmpty());
}

TEST(PeekRulesTest, SecureSiteIsItsDomainAlone) {
  const PeekSiteLabel label = PeekSiteLabelFor(
      GURL("https://www.kp.ru/daily/277833.5/5328415/?from=promo"), security_state::SECURE);
  EXPECT_EQ(label.text, u"kp.ru");
  EXPECT_EQ(label.warning, PeekSiteWarning::kNone);
  EXPECT_EQ(label.tooltip, u"https://www.kp.ru/daily/277833.5/5328415/?from=promo");
}

TEST(PeekRulesTest, PathCannotPassForAnotherSite) {
  const PeekSiteLabel label = PeekSiteLabelFor(
      GURL("https://evil.example/https://yourbank.example/login"), security_state::SECURE);
  EXPECT_EQ(label.text, u"evil.example");
}

TEST(PeekRulesTest, NotSecureAndDangerousSaySo) {
  const GURL url("http://example.com/page");
  const PeekSiteLabel http = PeekSiteLabelFor(url, security_state::WARNING);
  EXPECT_EQ(http.text, u"Not secure · example.com");
  EXPECT_EQ(http.warning, PeekSiteWarning::kNotSecure);
  const PeekSiteLabel bad = PeekSiteLabelFor(url, security_state::DANGEROUS);
  EXPECT_EQ(bad.text, u"Dangerous · example.com");
  EXPECT_EQ(bad.warning, PeekSiteWarning::kDangerous);
}

TEST(PeekRulesTest, NeutralPageHasNoWarning) {
  const PeekSiteLabel label = PeekSiteLabelFor(GURL("http://example.com/"), security_state::NONE);
  EXPECT_EQ(label.text, u"example.com");
  EXPECT_EQ(label.warning, PeekSiteWarning::kNone);
}

TEST(PeekRulesTest, SiteKeepsItsPortAndDropsCredentials) {
  const PeekSiteLabel label =
      PeekSiteLabelFor(GURL("https://user:pw@example.com:8443/x"), security_state::SECURE);
  EXPECT_EQ(label.text, u"example.com:8443");
  EXPECT_EQ(label.tooltip, u"https://example.com:8443/x");
}

TEST(PeekRulesTest, SafeInternationalNameIsShownAsWritten) {
  EXPECT_EQ(
      PeekSiteLabelFor(GURL("https://www.xn--frgbolaget-q5a.se/"), security_state::SECURE).text,
      u"färgbolaget.se");
}

TEST(PeekRulesTest, LocalFileShowsItsPath) {
  const PeekSiteLabel label =
      PeekSiteLabelFor(GURL("file:///tmp/peek/b.html"), security_state::NONE);
  EXPECT_EQ(label.text, u"file:///tmp/peek/b.html");
  EXPECT_EQ(label.warning, PeekSiteWarning::kNone);
}

}  // namespace
}  // namespace lb
