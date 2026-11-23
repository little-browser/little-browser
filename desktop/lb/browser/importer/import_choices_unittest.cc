// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/import_choices.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/test/icu_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

TEST(ImportChoicesTest, EachSourceOffersWhatItHas) {
  // docs/ux.md §13, "What comes along".
  EXPECT_EQ(OfferedImportItems(ImportSourceKind::kChromium),
            kImportBookmarks | kImportHistory | kImportSearchShortcuts | kImportAddresses |
                kImportSpaces | kImportExtensions | kImportCookies);
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
  EXPECT_EQ(seen.size(), 7u);
  // Open tabs come with spaces, not on their own.
  EXPECT_FALSE(seen.contains(kImportOpenTabs));
  EXPECT_EQ(ImportItemsInOrder().front(), kImportCookies);  // the opt-in comes first, to be seen
  EXPECT_EQ(ImportItemLabel(kImportCookies), u"Stay signed in (cookies)");
}

TEST(ImportChoicesTest, Labels) {
  EXPECT_EQ(NameWithProfile(u"Work", u"Client"), u"Work — Client");
  EXPECT_EQ(NameWithProfile(u"Home", u""), u"Home");
  EXPECT_EQ(NameWithProfile(u"", u"Client"), u"Untitled — Client");
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

TEST(ImportChoicesTest, BrowsersListProfilesOrSpaces) {
  EXPECT_EQ(ImportChoicesHeading(ImportSourceKind::kChromium), u"Profiles");
  EXPECT_EQ(ImportChoicesHeading(ImportSourceKind::kFirefox), u"Profiles");
  EXPECT_EQ(ImportChoicesHeading(ImportSourceKind::kArc), u"Spaces");
  for (ImportSourceKind kind : {ImportSourceKind::kSafari, ImportSourceKind::kSafariExport,
                                ImportSourceKind::kBookmarksFile}) {
    EXPECT_TRUE(ImportChoicesHeading(kind).empty());
  }
  // One profile isn't listed, only several.
  EXPECT_FALSE(ShowsImportChoices(0));
  EXPECT_FALSE(ShowsImportChoices(1));
  EXPECT_TRUE(ShowsImportChoices(2));
}

TEST(ImportChoicesTest, ImportNeedsAnItemAndAProfileOrSpace) {
  const uint32_t items = kImportBookmarks;
  // Several profiles: at least one picked.
  EXPECT_TRUE(CanImport(ImportSourceKind::kChromium, 3, 1, items));
  EXPECT_FALSE(CanImport(ImportSourceKind::kChromium, 3, 0, items));
  EXPECT_FALSE(CanImport(ImportSourceKind::kChromium, 3, 3, 0));
  // A single profile isn't listed: it is taken.
  EXPECT_TRUE(CanImport(ImportSourceKind::kFirefox, 1, 0, items));
  // Arc with no space found (its sidebar couldn't be read) has nothing to import.
  EXPECT_FALSE(CanImport(ImportSourceKind::kArc, 0, 0, kImportSpaces));
  // Files and Safari have no profiles.
  EXPECT_TRUE(CanImport(ImportSourceKind::kBookmarksFile, 0, 0, items));
  EXPECT_FALSE(CanImport(ImportSourceKind::kSafari, 0, 0, 0));
}

ExtensionGroup Group(std::u16string profile, std::vector<size_t> extensions) {
  ExtensionGroup group;
  group.profile = std::move(profile);
  group.extensions = std::move(extensions);
  return group;
}

TEST(ImportChoicesTest, ProfilesAreListedByNameElseFolderElseBrowser) {
  EXPECT_EQ(ProfileLabel(u"Work", "Profile 1", u"Chrome"), u"Work");
  EXPECT_EQ(ProfileLabel(u"", "Profile 1", u"Chrome"), u"Profile 1");
  // A profile in the data folder itself (Firefox's names, or a browser with one profile).
  EXPECT_EQ(ProfileLabel(u"", "", u"Chrome"), u"Chrome");
}

TEST(ImportChoicesTest, ProblemsNameTheirProfileOnlyAmongSeveral) {
  EXPECT_EQ(ProfileProblem("History: couldn't be read", "Work", true),
            "Work: History: couldn't be read");
  EXPECT_EQ(ProfileProblem("History: couldn't be read", "Work", false),
            "History: couldn't be read");
  EXPECT_EQ(ProfileProblem("History: couldn't be read", "", true), "History: couldn't be read");
}

TEST(ImportChoicesTest, ExtensionsAreGroupedByProfile) {
  EXPECT_TRUE(GroupExtensionsByProfile({}).empty());
  // One profile: one group with no heading.
  EXPECT_EQ(GroupExtensionsByProfile({u"Work", u"Work"}),
            std::vector<ExtensionGroup>({Group(u"", {0, 1})}));
  // Several: one group each, in the order the profiles first come, even when they interleave.
  EXPECT_EQ(GroupExtensionsByProfile({u"Default", u"Default", u"Work", u"Default", u"Home"}),
            std::vector<ExtensionGroup>(
                {Group(u"Default", {0, 1, 3}), Group(u"Work", {2}), Group(u"Home", {4})}));
}

TEST(ImportChoicesTest, OnlyAMadeUpFolderIsReadWithAMadeUpKeychain) {
  base::CommandLine dev(base::CommandLine::NO_PROGRAM);
  dev.AppendSwitch("use-mock-keychain");  // desktop/scripts/run.sh
  EXPECT_FALSE(ReadsMadeUpImportKeychain(dev));
  base::CommandLine demo = dev;
  demo.AppendSwitchPath(kImportHomeSwitch, base::FilePath("home"));
  EXPECT_TRUE(ReadsMadeUpImportKeychain(demo));
  EXPECT_FALSE(ReadsMadeUpImportKeychain(base::CommandLine(base::CommandLine::NO_PROGRAM)));
}

}  // namespace
}  // namespace lb
