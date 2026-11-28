// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/tab_names.h"

#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace lb {
namespace {

TEST(TabNamesTest, SessionDataRoundTrips) {
  const TabCustom custom{u"Mail ✉", GURL("https://mail.example.com/inbox")};
  EXPECT_EQ(TabCustomFromSessionData(TabCustomToSessionData(custom)), custom);

  const TabCustom name_only{u"Notes", GURL()};
  EXPECT_EQ(TabCustomFromSessionData(TabCustomToSessionData(name_only)), name_only);

  const TabCustom link_only{u"", GURL("https://example.com/")};
  EXPECT_EQ(TabCustomFromSessionData(TabCustomToSessionData(link_only)), link_only);
}

TEST(TabNamesTest, NothingSetIsEmptyData) {
  EXPECT_EQ(TabCustomToSessionData(TabCustom()), "");
  EXPECT_FALSE(TabCustomFromSessionData(""));
}

TEST(TabNamesTest, BadSessionDataIsIgnored) {
  EXPECT_FALSE(TabCustomFromSessionData("not json"));
  EXPECT_FALSE(TabCustomFromSessionData("[1]"));
  EXPECT_FALSE(TabCustomFromSessionData(R"({"name": 3})"));
  // An invalid link is dropped; the name stays.
  EXPECT_FALSE(TabCustomFromSessionData(R"({"pinned_url": "not a url"})"));
  EXPECT_EQ(TabCustomFromSessionData(R"({"name": "Docs", "pinned_url": "::"})"),
            (TabCustom{u"Docs", GURL()}));
  // A name is cleaned as typed names are.
  EXPECT_EQ(TabCustomFromSessionData(R"({"name": "  a\n b "})"), (TabCustom{u"a b", GURL()}));
}

TEST(TabNamesTest, CleanTabName) {
  EXPECT_EQ(CleanTabName(u"  Work   mail "), u"Work mail");
  EXPECT_EQ(CleanTabName(u"line\none"), u"line one");
  EXPECT_EQ(CleanTabName(u" \t\n "), u"");
  EXPECT_EQ(CleanTabName(u"Ünïcode 😀"), u"Ünïcode 😀");
}

TEST(TabNamesTest, TitleToShow) {
  const std::u16string page = u"Inbox (3) - Mail";
  EXPECT_EQ(TabTitleToShow(TabCustom{u"Mail", GURL()}, page, false), u"Mail");
  EXPECT_EQ(TabTitleToShow(TabCustom(), page, false), page);
  EXPECT_EQ(TabTitleToShow(TabCustom{u"", GURL("https://a.com/")}, page, true), page);
}

TEST(TabNamesTest, HasLeftPinnedUrl) {
  const GURL link("https://mail.example.com/inbox");
  EXPECT_FALSE(HasLeftPinnedUrl(link, link));
  EXPECT_FALSE(HasLeftPinnedUrl(link, GURL("https://mail.example.com/inbox#message-4")));
  EXPECT_TRUE(HasLeftPinnedUrl(link, GURL("https://mail.example.com/sent")));
  EXPECT_TRUE(HasLeftPinnedUrl(link, GURL("https://mail.example.com/inbox?page=2")));
  EXPECT_TRUE(HasLeftPinnedUrl(link, GURL("http://mail.example.com/inbox")));
  // No link, or no page yet: nothing to go back to.
  EXPECT_FALSE(HasLeftPinnedUrl(GURL(), GURL("https://a.com/")));
  EXPECT_FALSE(HasLeftPinnedUrl(link, GURL()));
}

TEST(TabNamesTest, RecordFieldsRoundTripAndAreCleaned) {
  base::DictValue record;
  WriteTabCustom({u"Mail", GURL("https://mail.example.com/inbox")}, record);
  EXPECT_EQ(ReadTabCustom(record), (TabCustom{u"Mail", GURL("https://mail.example.com/inbox")}));
  // Nothing set: nothing written.
  base::DictValue empty;
  WriteTabCustom(TabCustom(), empty);
  EXPECT_TRUE(empty.empty());
  // Read back as typed names are kept, with an invalid link dropped.
  EXPECT_EQ(ReadTabCustom(base::DictValue().Set("name", " a \n b ").Set("pinned_url", "::")),
            (TabCustom{u"a b", GURL()}));
}

TEST(TabNamesTest, NameFromInput) {
  EXPECT_EQ(NameFromInput(u"  Mail ", u"Inbox (3) - Mail"), u"Mail");
  // The page's own title, as the rename bar starts with it, leaves the tab unnamed, so its row
  // keeps following the page's title.
  EXPECT_EQ(NameFromInput(u"Inbox (3) - Mail", u"Inbox (3) - Mail"), u"");
  EXPECT_EQ(NameFromInput(u" Inbox  (3) - Mail ", u"Inbox (3) - Mail"), u"");
  EXPECT_EQ(NameFromInput(u"", u"Inbox"), u"");
}

TEST(TabNamesTest, PinnedUrlOrPage) {
  const GURL link("https://mail.example.com/inbox");
  const GURL page("https://mail.example.com/sent");
  EXPECT_EQ(PinnedUrlOrPage(TabCustom{u"", link}, page), link);
  EXPECT_EQ(PinnedUrlOrPage(TabCustom(), page), page);
}

TEST(TabNamesTest, SettleTabLinkPinnedTakesItsCommittedPage) {
  const GURL page("https://mail.example.com/inbox");
  EXPECT_EQ(SettleTabLink(TabCustom{u"Mail", GURL()}, /*pinned=*/true, page),
            (TabCustom{u"Mail", page}));
}

TEST(TabNamesTest, SettleTabLinkKeepsALink) {
  const GURL link("https://mail.example.com/inbox");
  EXPECT_EQ(SettleTabLink(TabCustom{u"", link}, true, GURL("https://mail.example.com/sent")),
            (TabCustom{u"", link}));
}

TEST(TabNamesTest, SettleTabLinkBlankPageWaitsForARealOne) {
  // Pinned while blank: no link yet...
  TabCustom custom = SettleTabLink(TabCustom(), true, GURL("about:blank"));
  EXPECT_FALSE(custom.pinned_url.is_valid());
  custom = SettleTabLink(custom, true, GURL("chrome://newtab/"));
  EXPECT_FALSE(custom.pinned_url.is_valid());
  custom = SettleTabLink(custom, true, GURL());
  EXPECT_FALSE(custom.pinned_url.is_valid());
  // ...until it commits a real page, which becomes the link and stays when it moves on.
  custom = SettleTabLink(custom, true, GURL("https://a.example/"));
  EXPECT_EQ(custom.pinned_url, GURL("https://a.example/"));
  custom = SettleTabLink(custom, true, GURL("https://b.example/"));
  EXPECT_EQ(custom.pinned_url, GURL("https://a.example/"));
}

TEST(TabNamesTest, SettleTabLinkUnpinnedHasNoLink) {
  EXPECT_EQ(SettleTabLink(TabCustom{u"Mail", GURL("https://a.example/")}, /*pinned=*/false,
                          GURL("https://b.example/")),
            (TabCustom{u"Mail", GURL()}));
}

TEST(TabNamesTest, SettleTabLinkUnpinnedDoesNotWakeAtALink) {
  EXPECT_EQ(SettleTabLink(TabCustom{u"", GURL("https://a.example/"), /*wakes_at_link=*/true},
                          /*pinned=*/false, GURL("https://b.example/")),
            TabCustom());
}

TEST(TabNamesTest, WakesAtLinkRoundTripsAndNeedsALink) {
  const TabCustom closed_away{u"", GURL("https://mail.example.com/"), /*wakes_at_link=*/true};
  EXPECT_EQ(TabCustomFromSessionData(TabCustomToSessionData(closed_away)), closed_away);
  base::DictValue record;
  WriteTabCustom(TabCustom{u"Notes", GURL(), /*wakes_at_link=*/true}, record);
  EXPECT_FALSE(record.Find("wakes_at_link"));
  EXPECT_EQ(ReadTabCustom(base::DictValue().Set("name", "Notes").Set("wakes_at_link", true)),
            (TabCustom{u"Notes", GURL()}));
}

// Closing a pinned tab takes it back to its link (docs/ux.md §6).
TEST(TabNamesTest, PinnedTabClosedAwayFromItsLinkWakesAtIt) {
  const TabCustom custom{u"", GURL("https://mail.example.com/inbox")};
  EXPECT_TRUE(AfterPinnedSleep(custom, GURL("https://mail.example.com/message/7")).wakes_at_link);
  EXPECT_TRUE(AfterPinnedSleep(custom, GURL("https://other.example/")).wakes_at_link);
}

TEST(TabNamesTest, PinnedTabClosedOnItsLinkWakesAsItWas) {
  const TabCustom custom{u"", GURL("https://mail.example.com/inbox")};
  EXPECT_FALSE(AfterPinnedSleep(custom, GURL("https://mail.example.com/inbox")).wakes_at_link);
  EXPECT_FALSE(AfterPinnedSleep(custom, GURL("https://mail.example.com/inbox#top")).wakes_at_link);
  // Closed again on its link after a close away from it: the link is where it is anyway.
  EXPECT_FALSE(
      AfterPinnedSleep(TabCustom{u"", custom.pinned_url, /*wakes_at_link=*/true}, custom.pinned_url)
          .wakes_at_link);
  // No link: nothing to go back to.
  EXPECT_EQ(AfterPinnedSleep(TabCustom{u"Notes", GURL()}, GURL("https://a.example/")),
            (TabCustom{u"Notes", GURL()}));
}

TEST(TabNamesTest, ShowingATabLoadsItsLinkOnceAfterACloseAwayFromIt) {
  const TabCustom closed_away{u"Mail", GURL("https://mail.example.com/"), /*wakes_at_link=*/true};
  const TabShown shown = OnTabShown(closed_away);
  EXPECT_TRUE(shown.load_link);
  EXPECT_EQ(shown.custom, (TabCustom{u"Mail", GURL("https://mail.example.com/")}));
  EXPECT_FALSE(OnTabShown(shown.custom).load_link);
  EXPECT_FALSE(OnTabShown(TabCustom{u"Mail", GURL("https://mail.example.com/")}).load_link);
  EXPECT_FALSE(OnTabShown(TabCustom{u"Mail", GURL(), /*wakes_at_link=*/true}).load_link);
}

// A pinned tab asleep away from its link shows its link's title; awake, it follows the page it
// shows (docs/ux.md §6).
TEST(TabNamesTest, SleepingPinnedTabShowsItsLinksTitle) {
  const TabCustom custom{u"", GURL("https://mail.example.com/"), /*wakes_at_link=*/true,
                         u"Inbox - Mail"};
  EXPECT_EQ(TabTitleToShow(custom, u"Some article", /*asleep=*/true), u"Inbox - Mail");
  EXPECT_TRUE(HasOwnTitle(custom, /*asleep=*/true));
  // Awake: the page's title, and its "Loading…" title while it loads.
  const TabCustom awake{u"", custom.pinned_url, /*wakes_at_link=*/false, u"Inbox - Mail"};
  EXPECT_EQ(TabTitleToShow(awake, u"Some article", false), u"Some article");
  EXPECT_FALSE(HasOwnTitle(awake, false));
  // Woken in the background (tab menu Reload): still wakes at its link once closed, but awake,
  // its row follows the page it loaded.
  EXPECT_EQ(TabTitleToShow(custom, u"Some article", /*asleep=*/false), u"Some article");
  EXPECT_FALSE(HasOwnTitle(custom, /*asleep=*/false));
  // A name still wins.
  EXPECT_EQ(
      TabTitleToShow(TabCustom{u"Mail", custom.pinned_url, true, u"Inbox - Mail"}, u"x", true),
      u"Mail");
  // No link's title yet, or no link: the page's title.
  EXPECT_EQ(TabTitleToShow(TabCustom{u"", custom.pinned_url}, u"Some article", true),
            u"Some article");
  EXPECT_FALSE(HasOwnTitle(TabCustom{u"", custom.pinned_url}, true));
  EXPECT_EQ(TabTitleToShow(TabCustom{u"", GURL(), true, u"Inbox - Mail"}, u"Page", true), u"Page");
  EXPECT_FALSE(HasOwnTitle(TabCustom{u"", GURL(), true, u"Inbox - Mail"}, true));
}

TEST(TabNamesTest, PinnedTitleFollowsThePageOnlyOnItsLink) {
  const GURL link("https://mail.example.com/inbox");
  const TabCustom custom{u"", link, false, u"Inbox (2)"};
  // On its link (the part after "#" doesn't count), the page's title is the link's.
  EXPECT_EQ(TakePinnedTitle(custom, link, u"Inbox (3)").pinned_title, u"Inbox (3)");
  EXPECT_EQ(TakePinnedTitle(custom, GURL("https://mail.example.com/inbox#all"), u"Inbox (4)")
                .pinned_title,
            u"Inbox (4)");
  // Elsewhere, or before the page has set a title, the link's title stays.
  EXPECT_EQ(TakePinnedTitle(custom, GURL("https://news.example/"), u"News").pinned_title,
            u"Inbox (2)");
  EXPECT_EQ(TakePinnedTitle(custom, link, u"").pinned_title, u"Inbox (2)");
  // No link: nothing to keep.
  EXPECT_EQ(TakePinnedTitle(TabCustom{u"Notes", GURL()}, link, u"Inbox"),
            (TabCustom{u"Notes", GURL()}));
}

// Closed away from its link, then given the page it was on as its link: it is on its link, so
// showing it again must not load anything.
// A restored tab already on screen keeps the page it shows: no jump to its link later.
TEST(TabNamesTest, RestoredShownTabNoLongerWakesAtItsLink) {
  const TabCustom saved{u"", GURL("https://mail.example.com/"), /*wakes_at_link=*/true, u"Mail"};
  const GURL away("https://news.example/");
  EXPECT_FALSE(RestoredTabCustom(saved, /*pinned=*/true, away, /*shown=*/true).wakes_at_link);
  // Behind other tabs it still wakes at its link when it is shown.
  EXPECT_EQ(RestoredTabCustom(saved, /*pinned=*/true, away, /*shown=*/false), saved);
  // Unpinned: no link at all.
  EXPECT_EQ(RestoredTabCustom(saved, /*pinned=*/false, away, /*shown=*/false), TabCustom());
}

TEST(TabNamesTest, NewLinkNoLongerWakesAtTheOldOne) {
  const TabCustom slept{u"", GURL("https://mail.example.com/inbox"), /*wakes_at_link=*/true,
                        u"Inbox"};
  EXPECT_FALSE(WithPinnedUrl(slept, GURL("https://mail.example.com/message/7")).wakes_at_link);
  EXPECT_FALSE(WithPinnedUrl(slept, GURL("https://mail.example.com/inbox")).wakes_at_link);
}

TEST(TabNamesTest, NewLinkDropsTheOldLinksTitle) {
  const TabCustom custom{u"", GURL("https://mail.example.com/inbox"), false, u"Inbox"};
  EXPECT_EQ(WithPinnedUrl(custom, GURL("https://news.example/")),
            (TabCustom{u"", GURL("https://news.example/")}));
  EXPECT_EQ(WithPinnedUrl(custom, GURL("https://mail.example.com/inbox#top")).pinned_title,
            u"Inbox");
}

TEST(TabNamesTest, UnpinnedTabKeepsNoLinksTitle) {
  EXPECT_EQ(SettleTabLink(TabCustom{u"", GURL("https://a.example/"), false, u"A"},
                          /*pinned=*/false, GURL("https://a.example/")),
            TabCustom());
}

TEST(TabNamesTest, PinnedTitleRoundTripsAndNeedsALink) {
  const TabCustom custom{u"", GURL("https://mail.example.com/"), false, u"Inbox ✉"};
  EXPECT_EQ(TabCustomFromSessionData(TabCustomToSessionData(custom)), custom);
  EXPECT_EQ(ReadTabCustom(base::DictValue().Set("name", "N").Set("pinned_title", "T")),
            (TabCustom{u"N", GURL()}));
}

TEST(TabNamesTest, TabMenuTargets) {
  // Right-clicked outside the selection: just that tab.
  EXPECT_EQ(TabMenuTargets(false, 3, 0), 1u);
  // The shown tab alone.
  EXPECT_EQ(TabMenuTargets(true, 1, 0), 1u);
  // A split view is always selected whole: it counts as the one tab right-clicked.
  EXPECT_EQ(TabMenuTargets(true, 2, 2), 1u);
  // The split and another selected tab: two.
  EXPECT_EQ(TabMenuTargets(true, 3, 2), 2u);
  // Several selected tabs.
  EXPECT_EQ(TabMenuTargets(true, 3, 0), 3u);
}

TEST(TabNamesTest, PinnedUrlFromInput) {
  EXPECT_EQ(PinnedUrlFromInput(u"https://example.com/a?b=1"), GURL("https://example.com/a?b=1"));
  EXPECT_EQ(PinnedUrlFromInput(u"  example.com "), GURL("https://example.com/"));
  EXPECT_EQ(PinnedUrlFromInput(u"localhost:8080"), GURL("http://localhost:8080/"));
  EXPECT_FALSE(PinnedUrlFromInput(u"what is a link"));
  EXPECT_FALSE(PinnedUrlFromInput(u""));
}

TEST(TabNamesTest, MenuItemsForATodayTab) {
  const std::vector<TabNameMenuItem> items = TabNameMenuItems(1, /*pinned=*/false, false);
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].command, TabNameCommand::kRename);
  EXPECT_TRUE(items[0].enabled);
}

TEST(TabNamesTest, MenuItemsForAPinnedTab) {
  std::vector<TabNameMenuItem> items = TabNameMenuItems(1, /*pinned=*/true, /*left=*/false);
  ASSERT_EQ(items.size(), 3u);
  EXPECT_EQ(items[0].command, TabNameCommand::kRename);
  EXPECT_EQ(items[1].command, TabNameCommand::kEditPinnedUrl);
  EXPECT_TRUE(items[1].enabled);
  // On its link already: nothing to replace it with.
  EXPECT_EQ(items[2].command, TabNameCommand::kUsePageAsPinnedUrl);
  EXPECT_FALSE(items[2].enabled);

  // Off its link: no Back to Pinned Page, as closing the tab goes back to its link.
  items = TabNameMenuItems(1, /*pinned=*/true, /*left=*/true);
  ASSERT_EQ(items.size(), 3u);
  EXPECT_TRUE(items[2].enabled);
}

TEST(TabNamesTest, NoItemsForSeveralTabs) {
  EXPECT_TRUE(TabNameMenuItems(2, true, true).empty());
  EXPECT_TRUE(TabNameMenuItems(0, false, false).empty());
}

TEST(TabNamesTest, CommandIds) {
  for (TabNameCommand command : {TabNameCommand::kRename, TabNameCommand::kEditPinnedUrl,
                                 TabNameCommand::kUsePageAsPinnedUrl}) {
    const int id = TabNameCommandId(command);
    EXPECT_GE(id, kFirstTabNameCommandId);
    EXPECT_LT(id, 1000);  // below the tab menu's submenu ranges
    EXPECT_EQ(TabNameCommandFromId(id), command);
  }
  EXPECT_FALSE(TabNameCommandFromId(0));  // Chromium's CommandNewTabToRight
  EXPECT_FALSE(TabNameCommandFromId(kFirstTabNameCommandId + 3));
}

}  // namespace
}  // namespace lb
