// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/import_choices.h"

#include <set>

#include "base/test/icu_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

TEST(ImportChoicesTest, EachSourceOffersWhatItHas) {
  // docs/ux.md §13, "What comes along".
  EXPECT_EQ(OfferedImportItems(ImportSourceKind::kChromium),
            kImportBookmarks | kImportHistory | kImportSearchShortcuts | kImportAddresses |
                kImportOpenTabs | kImportExtensions | kImportCookies);
  EXPECT_EQ(OfferedImportItems(ImportSourceKind::kArc),
            kImportBookmarks | kImportHistory | kImportSearchShortcuts | kImportAddresses |
                kImportSpaces | kImportExtensions | kImportCookies);
  EXPECT_EQ(OfferedImportItems(ImportSourceKind::kFirefox),
            kImportBookmarks | kImportHistory | kImportSearchShortcuts);
  EXPECT_EQ(OfferedImportItems(ImportSourceKind::kSafariExport), kImportBookmarks | kImportHistory);
  EXPECT_EQ(OfferedImportItems(ImportSourceKind::kSafari), kImportBookmarks);
  EXPECT_EQ(OfferedImportItems(ImportSourceKind::kBookmarksFile),
            kImportBookmarks | kImportSearchShortcuts);
}

TEST(ImportChoicesTest, CookiesStartUnticked) {
  for (ImportSourceKind kind :
       {ImportSourceKind::kChromium, ImportSourceKind::kArc, ImportSourceKind::kFirefox,
        ImportSourceKind::kSafari, ImportSourceKind::kSafariExport,
        ImportSourceKind::kBookmarksFile}) {
    EXPECT_EQ(DefaultImportItems(kind) & kImportCookies, 0u);
    EXPECT_EQ(DefaultImportItems(kind) | kImportCookies, OfferedImportItems(kind) | kImportCookies);
  }
}

TEST(ImportChoicesTest, EveryItemIsListedOnceWithALabel) {
  std::set<ImportItem> seen;
  for (ImportItem item : ImportItemsInOrder()) {
    EXPECT_TRUE(seen.insert(item).second);
    EXPECT_FALSE(ImportItemLabel(item).empty());
  }
  EXPECT_EQ(seen.size(), 8u);
  EXPECT_EQ(ImportItemsInOrder().back(), kImportCookies);  // the opt-in comes last
  EXPECT_EQ(ImportItemLabel(kImportCookies), u"Stay signed in (cookies)");
}

TEST(ImportChoicesTest, Labels) {
  EXPECT_EQ(ImportSourceLabel(u"Chrome", u"Work"), u"Chrome — Work");
  EXPECT_EQ(ImportSourceLabel(u"Arc", u""), u"Arc");
  EXPECT_EQ(CookiesKeychainNote(u"Chrome Safe Storage"),
            u"macOS will ask to let Little Browser use “Chrome Safe Storage” from your "
            u"keychain.");
}

TEST(ImportChoicesTest, Summary) {
  base::test::ScopedRestoreICUDefaultLocale locale("en_US");
  ImportCounts counts;
  EXPECT_EQ(ImportSummary(counts), u"Nothing new to import");
  counts.bookmarks = 812;
  counts.history = 12400;
  counts.spaces = 3;
  counts.tabs = 41;
  EXPECT_EQ(ImportSummary(counts), u"812 bookmarks, 12,400 pages of history, 3 spaces, 41 tabs");

  ImportCounts one;
  one.bookmarks = one.history = one.spaces = one.tabs = one.search_shortcuts = one.addresses =
      one.cookies = 1;
  EXPECT_EQ(ImportSummary(one),
            u"1 bookmark, 1 page of history, 1 space, 1 tab, 1 search shortcut, 1 address, "
            u"1 cookie");
  one += counts;
  EXPECT_EQ(one.bookmarks, 813u);
  EXPECT_EQ(one.cookies, 1u);
}

TEST(ImportChoicesTest, NotFromStore) {
  EXPECT_EQ(NotFromStoreLine(0), u"");
  EXPECT_EQ(NotFromStoreLine(1), u"1 isn’t from the Chrome Web Store, so it can’t come along");
  EXPECT_EQ(NotFromStoreLine(2), u"2 aren’t from the Chrome Web Store, so they can’t come along");
}

TEST(ImportChoicesTest, ImportOfferOnlyOnTheFirstRunAndOtherWindowsWait) {
  EXPECT_EQ(NextImportOfferStep({.first_run = true}), ImportOfferStep::kFindSources);
  EXPECT_EQ(NextImportOfferStep({.first_run = false}), ImportOfferStep::kSkip);
  EXPECT_EQ(NextImportOfferStep({.offered = true, .first_run = true}), ImportOfferStep::kSkip);
  // A window opened while another shows the offer, the dialog or its installs waits for them.
  EXPECT_EQ(NextImportOfferStep({.offered = true, .first_run = true, .in_progress = true}),
            ImportOfferStep::kWait);
  EXPECT_EQ(NextImportOfferStep({.offered = true, .first_run = false, .in_progress = true}),
            ImportOfferStep::kWait);
}

TEST(ImportChoicesTest, ExtensionLabelsNameTheProfileWhenThereAreSeveral) {
  EXPECT_EQ(ExtensionLabel(u"uBlock Origin", u"Work", false), u"uBlock Origin");
  EXPECT_EQ(ExtensionLabel(u"uBlock Origin", u"Work", true), u"uBlock Origin — Work");
  EXPECT_EQ(ExtensionLabel(u"uBlock Origin", u"", true), u"uBlock Origin");
}

}  // namespace
}  // namespace lb
