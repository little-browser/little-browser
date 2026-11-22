// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/arc_tabs.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

core::ArcPinnedTab Pinned(const char* url, int folder = -1, const char* folder_name = "") {
  return core::ArcPinnedTab{url, url, folder, folder_name};
}

core::ArcSpace Space(std::vector<core::ArcPinnedTab> pinned, std::vector<core::Tab> unpinned) {
  core::ArcSpace space;
  for (core::ArcPinnedTab& t : pinned) {
    space.pinned.push_back(std::move(t));
  }
  for (core::Tab& t : unpinned) {
    space.unpinned.push_back(std::move(t));
  }
  return space;
}

TEST(ArcTabsTest, FoldersBecomePinnedGroupsInPlace) {
  const std::vector<SavedTab> tabs =
      ArcSpaceTabs(Space({Pinned("https://a.example/"), Pinned("https://b.example/", 0, "Docs"),
                          Pinned("https://c.example/", 0, "Docs"), Pinned("https://d.example/"),
                          Pinned("https://e.example/", 2, "Docs")},
                         {core::Tab{"https://today.example/", "Today"}}),
                   /*group_color=*/4);
  ASSERT_EQ(tabs.size(), 6u);
  for (size_t i = 0; i < 5; ++i) {
    EXPECT_TRUE(tabs[i].pinned) << i;
  }
  EXPECT_TRUE(tabs[0].group_id.empty());
  EXPECT_FALSE(tabs[1].group_id.empty());
  EXPECT_EQ(tabs[1].group_id, tabs[2].group_id);
  EXPECT_EQ(tabs[1].group_title, u"Docs");
  EXPECT_EQ(tabs[1].group_color, 4);
  EXPECT_FALSE(tabs[1].group_collapsed);
  EXPECT_TRUE(tabs[3].group_id.empty());
  // Another folder with the same name is a group of its own.
  EXPECT_FALSE(tabs[4].group_id.empty());
  EXPECT_NE(tabs[4].group_id, tabs[1].group_id);
  EXPECT_EQ(SavedTabGroups(tabs), (std::vector<std::vector<size_t>>{{1, 2}, {4}}));

  EXPECT_FALSE(tabs[5].pinned);
  EXPECT_TRUE(tabs[5].group_id.empty());
  EXPECT_EQ(tabs[5].title, u"Today");
}

TEST(ArcTabsTest, EachImportMakesNewGroups) {
  const core::ArcSpace space = Space({Pinned("https://b.example/", 0, "Docs")}, {});
  EXPECT_NE(ArcSpaceTabs(space, 0)[0].group_id, ArcSpaceTabs(space, 0)[0].group_id);
}

TEST(ArcTabsTest, InvalidAddressesAreLeftOut) {
  const std::vector<SavedTab> tabs =
      ArcSpaceTabs(Space({Pinned("not a url", 0, "Docs")}, {core::Tab{"also not", ""}}), 0);
  EXPECT_TRUE(tabs.empty());
}

}  // namespace
}  // namespace lb
