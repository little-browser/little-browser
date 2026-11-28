// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/spaces/space_store.h"

#include <string>
#include <vector>

#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/prefs/testing_pref_service.h"
#include "lb/browser/prefs/lb_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace lb {
namespace {

class SpaceStoreTest : public testing::Test {
 protected:
  SpaceStoreTest() { RegisterLocalStatePrefs(prefs_.registry()); }

  static Space MakeSpace(std::string id, std::string name) {
    Space space;
    space.id = std::move(id);
    space.name = std::move(name);
    space.icon = SpaceIcons()[0];
    space.profile = "Default";
    return space;
  }

  std::vector<std::string> Ids() {
    std::vector<std::string> ids;
    for (const Space& space : ReadSpaces(prefs_, "Default")) {
      ids.push_back(space.id);
    }
    return ids;
  }

  TestingPrefServiceSimple prefs_;
};

TEST_F(SpaceStoreTest, AppendedSpacesReadBackInOrder) {
  EXPECT_TRUE(ReadSpaces(prefs_, "Default").empty());
  AppendSpace(prefs_, MakeSpace("a", "Personal"));
  AppendSpace(prefs_, MakeSpace("b", "Work"));
  const std::vector<Space> spaces = ReadSpaces(prefs_, "Default");
  ASSERT_EQ(spaces.size(), 2u);
  EXPECT_EQ(spaces[0].id, "a");
  EXPECT_EQ(spaces[0].name, "Personal");
  EXPECT_EQ(spaces[1].name, "Work");
}

// Malformed entries (not a dict, no id) are skipped; a space without a profile gets the fallback.
TEST_F(SpaceStoreTest, ReadSkipsMalformedEntriesAndFillsTheProfile) {
  {
    ScopedListPrefUpdate update(prefs_, kSpacesPref);
    update->Append(base::Value("not a space"));
    update->Append(base::DictValue().Set("name", "no id"));
    update->Append(base::DictValue().Set("id", "c").Set("name", "Kept"));
  }
  const std::vector<Space> spaces = ReadSpaces(prefs_, "Profile 2");
  ASSERT_EQ(spaces.size(), 1u);
  EXPECT_EQ(spaces[0].id, "c");
  EXPECT_EQ(spaces[0].profile, "Profile 2");
}

TEST_F(SpaceStoreTest, SetFieldChangesOnlyThatSpace) {
  AppendSpace(prefs_, MakeSpace("a", "Personal"));
  AppendSpace(prefs_, MakeSpace("b", "Work"));
  SetSpaceField(prefs_, "b", "name", base::Value("Office"));
  SetSpaceField(prefs_, "b", "hue", base::Value(120));
  SetSpaceField(prefs_, "missing", "name", base::Value("Nobody"));
  const std::vector<Space> spaces = ReadSpaces(prefs_, "Default");
  EXPECT_EQ(spaces[0].name, "Personal");
  EXPECT_EQ(spaces[1].name, "Office");
  EXPECT_EQ(spaces[1].hue, 120);
}

TEST_F(SpaceStoreTest, RemoveTakesTheSpaceAndItsTabs) {
  AppendSpace(prefs_, MakeSpace("a", "Personal"));
  AppendSpace(prefs_, MakeSpace("b", "Work"));
  WriteSpaceTabs(prefs_, "b", {{GURL("https://example.org/"), u"Example"}});
  RemoveSpace(prefs_, "b");
  EXPECT_EQ(Ids(), std::vector<std::string>{"a"});
  EXPECT_TRUE(ReadSpaceTabs(prefs_, "b").empty());
  EXPECT_FALSE(prefs_.GetDict(kSpaceTabsPref).contains("b"));
}

TEST_F(SpaceStoreTest, TabsRoundTripPerSpace) {
  SavedTab pinned{GURL("https://mail.example/"), u"Mail", /*pinned=*/true};
  SavedTab today{GURL("https://news.example/"), u"News"};
  today.active = true;
  WriteSpaceTabs(prefs_, "a", {pinned, today});
  WriteSpaceTabs(prefs_, "b", {today});
  const std::vector<SavedTab> a = ReadSpaceTabs(prefs_, "a");
  ASSERT_EQ(a.size(), 2u);
  EXPECT_EQ(a[0].url, pinned.url);
  EXPECT_TRUE(a[0].pinned);
  EXPECT_TRUE(a[1].active);
  EXPECT_EQ(ReadSpaceTabs(prefs_, "b").size(), 1u);
  EXPECT_TRUE(ReadSpaceTabs(prefs_, "none").empty());
}

// Writing the list a space already has changes nothing, so Local State's observers stay asleep.
TEST_F(SpaceStoreTest, AnUnchangedListIsNotWrittenAgain) {
  const std::vector<SavedTab> tabs = {{GURL("https://example.org/"), u"Example"}};
  int changes = 0;
  PrefChangeRegistrar registrar;
  registrar.Init(&prefs_);
  registrar.Add(kSpaceTabsPref, base::BindRepeating([](int* n) { ++*n; }, &changes));
  EXPECT_TRUE(WriteSpaceTabs(prefs_, "a", tabs));
  EXPECT_FALSE(WriteSpaceTabs(prefs_, "a", tabs));
  EXPECT_EQ(changes, 1);
  EXPECT_TRUE(WriteSpaceTabs(prefs_, "a", {}));
  EXPECT_EQ(changes, 2);
}

// Added tabs go where MergeSavedTabs puts them: pinned after the pinned, the rest at the end.
TEST_F(SpaceStoreTest, AddedTabsMergeIntoTheSavedOnes) {
  WriteSpaceTabs(prefs_, "a", {{GURL("https://pinned.example/"), u"P", true},
                               {GURL("https://today.example/"), u"T"}});
  AddSpaceTabs(prefs_, "a", {{GURL("https://new-pinned.example/"), u"NP", true},
                             {GURL("https://new-today.example/"), u"NT"}});
  std::vector<std::string> hosts;
  for (const SavedTab& tab : ReadSpaceTabs(prefs_, "a")) {
    hosts.emplace_back(tab.url.host());
  }
  EXPECT_EQ(hosts, (std::vector<std::string>{"pinned.example", "new-pinned.example",
                                              "today.example", "new-today.example"}));
  AddSpaceTabs(prefs_, "empty", {{GURL("https://only.example/"), u"O"}});
  EXPECT_EQ(ReadSpaceTabs(prefs_, "empty").size(), 1u);
}

}  // namespace
}  // namespace lb
