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
  EXPECT_EQ(TabTitleToShow(TabCustom{u"Mail", GURL()}, page), u"Mail");
  EXPECT_EQ(TabTitleToShow(TabCustom(), page), page);
  EXPECT_EQ(TabTitleToShow(TabCustom{u"", GURL("https://a.com/")}, page), page);
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
  ASSERT_EQ(items.size(), 4u);
  EXPECT_EQ(items[0].command, TabNameCommand::kRename);
  EXPECT_EQ(items[1].command, TabNameCommand::kEditPinnedUrl);
  EXPECT_TRUE(items[1].enabled);
  // On its link already: nothing to replace it with or go back to.
  EXPECT_EQ(items[2].command, TabNameCommand::kUsePageAsPinnedUrl);
  EXPECT_FALSE(items[2].enabled);
  EXPECT_EQ(items[3].command, TabNameCommand::kBackToPinnedUrl);
  EXPECT_FALSE(items[3].enabled);

  items = TabNameMenuItems(1, /*pinned=*/true, /*left=*/true);
  EXPECT_TRUE(items[2].enabled);
  EXPECT_TRUE(items[3].enabled);
}

TEST(TabNamesTest, NoItemsForSeveralTabs) {
  EXPECT_TRUE(TabNameMenuItems(2, true, true).empty());
  EXPECT_TRUE(TabNameMenuItems(0, false, false).empty());
}

TEST(TabNamesTest, CommandIds) {
  for (TabNameCommand command :
       {TabNameCommand::kRename, TabNameCommand::kEditPinnedUrl,
        TabNameCommand::kUsePageAsPinnedUrl, TabNameCommand::kBackToPinnedUrl}) {
    const int id = TabNameCommandId(command);
    EXPECT_GE(id, kFirstTabNameCommandId);
    EXPECT_LT(id, 1000);  // below the tab menu's submenu ranges
    EXPECT_EQ(TabNameCommandFromId(id), command);
  }
  EXPECT_FALSE(TabNameCommandFromId(0));  // Chromium's CommandNewTabToRight
  EXPECT_FALSE(TabNameCommandFromId(kFirstTabNameCommandId + 4));
}

}  // namespace
}  // namespace lb
