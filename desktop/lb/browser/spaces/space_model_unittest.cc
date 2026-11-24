// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/spaces/space_model.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace lb {
namespace {

Space MakeSpace(std::string id, std::string profile, int hue = FirstSpaceHue()) {
  Space space;
  space.id = std::move(id);
  space.name = "Space";
  space.icon = SpaceIcons()[0];
  space.hue = hue;
  space.profile = std::move(profile);
  return space;
}

TEST(SpaceModelTest, DictRoundTrip) {
  Space space = MakeSpace("a1", "Profile 1", 140);
  space.name = "Work";
  space.icon = SpaceIcons()[1];
  const base::DictValue dict = SpaceToDict(space);
  EXPECT_FALSE(dict.contains("tabs"));  // saved tabs live in their own pref

  const std::optional<Space> read = SpaceFromDict(dict, "Default");
  ASSERT_TRUE(read);
  EXPECT_EQ(read->id, "a1");
  EXPECT_EQ(read->name, "Work");
  EXPECT_EQ(read->icon, SpaceIcons()[1]);
  EXPECT_EQ(read->hue, 140);
  EXPECT_EQ(read->profile, "Profile 1");
  EXPECT_TRUE(read->profile_name.empty());
}

TEST(SpaceModelTest, FromDictNeedsAnId) {
  EXPECT_FALSE(SpaceFromDict(base::DictValue(), "Default"));
  EXPECT_FALSE(SpaceFromDict(base::DictValue().Set("id", ""), "Default"));
}

TEST(SpaceModelTest, FromDictFillsDefaults) {
  // Older entries had no profile (spaces were per profile); empty means the same.
  for (const char* profile : {static_cast<const char*>(""), static_cast<const char*>(nullptr)}) {
    base::DictValue dict = base::DictValue().Set("id", "x");
    if (profile) {
      dict.Set("profile", profile);
    }
    const std::optional<Space> read = SpaceFromDict(dict, "Default");
    ASSERT_TRUE(read);
    EXPECT_EQ(read->name, "Space");
    EXPECT_EQ(read->icon, SpaceIcons()[0]);
    EXPECT_EQ(read->hue, FirstSpaceHue());
    EXPECT_EQ(read->profile, "Default");
  }
}

TEST(SpaceModelTest, NextSpaceDefaults) {
  std::vector<Space> spaces;
  EXPECT_EQ(NextSpaceHue(spaces), FirstSpaceHue());
  EXPECT_EQ(NextSpaceIcon(spaces), SpaceIcons()[0]);
  EXPECT_EQ(NextSpaceName(spaces), "Space 1");

  spaces.push_back(MakeSpace("a", "Default", 300));
  EXPECT_EQ(NextSpaceHue(spaces), (300 + 137) % 360);  // wraps around the wheel
  EXPECT_EQ(NextSpaceIcon(spaces), SpaceIcons()[1]);
  EXPECT_EQ(NextSpaceName(spaces), "Space 2");
}

TEST(SpaceModelTest, FindActiveSpace) {
  EXPECT_EQ(FindActiveSpace({}, "a"), nullptr);
  std::vector<Space> spaces = {MakeSpace("a", "Default"), MakeSpace("b", "Default")};
  spaces[1].icon = SpaceIcons()[1];
  EXPECT_EQ(FindActiveSpace(spaces, "b"), &spaces[1]);
  EXPECT_EQ(FindActiveSpace(spaces, "a"), &spaces[0]);
  // A window whose space is gone (or not chosen yet) shows the first.
  EXPECT_EQ(FindActiveSpace(spaces, "gone"), &spaces[0]);
  EXPECT_EQ(FindActiveSpace(spaces, ""), &spaces[0]);
}

TEST(SpaceModelTest, NextSpaceIconCycles) {
  std::vector<Space> spaces;
  for (size_t i = 0; i < SpaceIcons().size(); ++i) {
    spaces.push_back(MakeSpace("s" + std::to_string(i), "Default"));
  }
  EXPECT_EQ(NextSpaceIcon(spaces), SpaceIcons()[0]);
}

TEST(SpaceModelTest, NeighbouringHuesDiffer) {
  std::vector<Space> spaces;
  std::set<int> hues;
  for (int i = 0; i < 8; ++i) {
    const int hue = NextSpaceHue(spaces);
    EXPECT_GE(hue, 0);
    EXPECT_LT(hue, 360);
    hues.insert(hue);
    spaces.push_back(MakeSpace("s" + std::to_string(i), "Default", hue));
  }
  EXPECT_EQ(hues.size(), 8u);
}

class ChooseSpaceForWindowTest : public testing::Test {
 protected:
  std::optional<std::string> Choose(std::string_view profile,
                                    std::string_view last_active,
                                    bool restoring = false) {
    return ChooseSpaceForWindow(
        spaces_, profile, last_active,
        [this](const std::string& id) { return shown_.contains(id); }, restoring);
  }

  std::vector<Space> spaces_ = {MakeSpace("home", "Default"), MakeSpace("work", "Default"),
                                MakeSpace("client", "Profile 1")};
  std::set<std::string> shown_;
};

TEST_F(ChooseSpaceForWindowTest, PrefersLastActiveSpace) {
  EXPECT_EQ(Choose("Default", "work"), "work");
}

TEST_F(ChooseSpaceForWindowTest, LastActiveMustBelongToTheProfile) {
  EXPECT_EQ(Choose("Default", "client"), "home");
}

TEST_F(ChooseSpaceForWindowTest, SkipsSpacesShownInOtherWindows) {
  shown_ = {"work"};
  EXPECT_EQ(Choose("Default", "work"), "home");
}

TEST_F(ChooseSpaceForWindowTest, NullWhenEverySpaceIsShown) {
  shown_ = {"home", "work"};
  EXPECT_EQ(Choose("Default", "home"), std::nullopt);
}

TEST_F(ChooseSpaceForWindowTest, SessionRestoreReusesAShownSpace) {
  // Restore puts the window on its saved space right after; creating a space would leak one.
  shown_ = {"home", "work"};
  EXPECT_EQ(Choose("Default", "home", /*restoring=*/true), "home");
}

TEST_F(ChooseSpaceForWindowTest, NullForAProfileWithoutSpaces) {
  EXPECT_EQ(Choose("Profile 2", "", /*restoring=*/true), std::nullopt);
}

TEST(SpaceModelTest, SpaceColoursMapToTheirGroupColours) {
  using tab_groups::TabGroupColorId;
  const std::map<std::u16string, TabGroupColorId> expected = {
      {u"Blue", TabGroupColorId::kBlue},     {u"Teal", TabGroupColorId::kCyan},
      {u"Green", TabGroupColorId::kGreen},   {u"Amber", TabGroupColorId::kYellow},
      {u"Orange", TabGroupColorId::kOrange}, {u"Red", TabGroupColorId::kRed},
      {u"Pink", TabGroupColorId::kPink},     {u"Purple", TabGroupColorId::kPurple},
  };
  for (const SpaceColour& colour : SpaceColours()) {
    EXPECT_EQ(TabGroupColorForHue(colour.hue), expected.at(colour.name)) << colour.hue;
  }
}

TEST(SpaceModelTest, AnyHueGetsTheNearestGroupColour) {
  using tab_groups::TabGroupColorId;
  EXPECT_EQ(TabGroupColorForHue(355), TabGroupColorId::kRed);  // wraps around
  EXPECT_EQ(TabGroupColorForHue(-10), TabGroupColorId::kRed);
  EXPECT_EQ(TabGroupColorForHue(720 + 140), TabGroupColorId::kGreen);
  EXPECT_EQ(TabGroupColorForHue(310), TabGroupColorId::kPink);
  // Every hue NextSpaceHue hands out gets a real colour, never grey.
  for (int hue = 0; hue < 360; ++hue) {
    EXPECT_NE(TabGroupColorForHue(hue), TabGroupColorId::kGrey) << hue;
  }
}

TEST(SpaceModelTest, HuesWrapAround) {
  EXPECT_EQ(NormalizeHue(0), 0);
  EXPECT_EQ(NormalizeHue(360), 0);
  EXPECT_EQ(NormalizeHue(-10), 350);
  EXPECT_EQ(NormalizeHue(725), 5);
  EXPECT_EQ(HueDistance(10, 350), 20);
  EXPECT_EQ(HueDistance(350, 10), 20);
  EXPECT_EQ(HueDistance(0, 180), 180);
  EXPECT_EQ(HueDistance(-100, 260), 0);
}

TEST(SpaceModelTest, NearestSpaceColourWrapsAndNormalises) {
  for (size_t i = 0; i < SpaceColours().size(); ++i) {
    EXPECT_EQ(NearestSpaceColour(SpaceColours()[i].hue), i);
  }
  auto name = [](int hue) { return SpaceColours()[NearestSpaceColour(hue)].name; };
  EXPECT_EQ(name(355), u"Red");      // wraps past 360
  EXPECT_EQ(name(-100), u"Purple");  // 260: a negative hue is normalised first
  EXPECT_EQ(name(200), u"Teal");
}

TEST(SpaceModelTest, BlankTabUrls) {
  EXPECT_TRUE(IsBlankTabUrl(GURL()));
  EXPECT_TRUE(IsBlankTabUrl(GURL("about:blank")));
  EXPECT_TRUE(IsBlankTabUrl(GURL("chrome://newtab/")));
  EXPECT_TRUE(IsBlankTabUrl(GURL("chrome://new-tab-page/")));
  EXPECT_FALSE(IsBlankTabUrl(GURL("chrome://welcome/")));
  EXPECT_FALSE(IsBlankTabUrl(GURL("https://example.com/")));
}

TEST(SavedTabsTest, RoundTrip) {
  base::ListValue list;
  list.Append(SavedTabToDict({GURL("https://a.com/"), u"A", /*pinned=*/true, /*active=*/false}));
  list.Append(SavedTabToDict({GURL("https://b.com/x"), u"B", /*pinned=*/false, /*active=*/true}));
  const std::vector<SavedTab> tabs = SavedTabsFromList(list);
  ASSERT_EQ(tabs.size(), 2u);
  EXPECT_EQ(tabs[0].url, GURL("https://a.com/"));
  EXPECT_EQ(tabs[0].title, u"A");
  EXPECT_TRUE(tabs[0].pinned);
  EXPECT_FALSE(tabs[0].active);
  EXPECT_EQ(tabs[1].url, GURL("https://b.com/x"));
  EXPECT_TRUE(tabs[1].active);
  EXPECT_EQ(ActiveSavedTab(tabs), 1u);
}

TEST(SavedTabsTest, OnlyTheActiveTabIsMarked) {
  // Records stay small and compare equal when nothing changed.
  EXPECT_FALSE(SavedTabToDict({GURL("https://a.com/"), u"A"}).contains("active"));
}

TEST(SavedTabsTest, SkipsMalformedRecords) {
  base::ListValue list;
  list.Append("not a dict");
  list.Append(base::DictValue().Set("title", "no url"));
  list.Append(base::DictValue().Set("url", ""));
  list.Append(base::DictValue().Set("url", "not a url"));
  list.Append(base::DictValue().Set("url", "https://ok.com/"));
  const std::vector<SavedTab> tabs = SavedTabsFromList(list);
  ASSERT_EQ(tabs.size(), 1u);
  EXPECT_EQ(tabs[0].url, GURL("https://ok.com/"));
  EXPECT_EQ(tabs[0].title, u"https://ok.com/");  // no title: the URL
}

TEST(SavedTabsTest, ActiveDefaultsToTheFirstTab) {
  EXPECT_EQ(ActiveSavedTab({}), 0u);
  EXPECT_EQ(ActiveSavedTab({{GURL("https://a.com/"), u"A"}, {GURL("https://b.com/"), u"B"}}), 0u);
}

TEST(SavedTabsTest, GroupRoundTrip) {
  SavedTab tab{GURL("https://a.com/"), u"A"};
  tab.group_id = "g1";
  tab.group_title = u"Research";
  tab.group_color = 4;
  tab.group_collapsed = true;
  tab.group_saved_guid = "0b5c3d4e-1f2a-4b3c-8d9e-0a1b2c3d4e5f";
  base::ListValue list;
  list.Append(SavedTabToDict(tab));
  list.Append(SavedTabToDict({GURL("https://b.com/"), u"B"}));
  const std::vector<SavedTab> tabs = SavedTabsFromList(list);
  ASSERT_EQ(tabs.size(), 2u);
  EXPECT_EQ(tabs[0].group_id, "g1");
  EXPECT_EQ(tabs[0].group_title, u"Research");
  EXPECT_EQ(tabs[0].group_color, 4);
  EXPECT_TRUE(tabs[0].group_collapsed);
  EXPECT_EQ(tabs[0].group_saved_guid, "0b5c3d4e-1f2a-4b3c-8d9e-0a1b2c3d4e5f");
  EXPECT_TRUE(tabs[1].group_id.empty());
  EXPECT_TRUE(tabs[1].group_saved_guid.empty());
  EXPECT_FALSE(SavedTabToDict(tabs[1]).contains("group"));
}

TEST(SavedTabsTest, GroupsAreRunsOfTheSameId) {
  auto tab = [](std::string group) {
    SavedTab t{GURL("https://a.com/"), u"A"};
    t.group_id = std::move(group);
    return t;
  };
  // [g1 g1] loose [g2] [g1]: the last g1 is a different run, so a separate group.
  const std::vector<SavedTab> tabs = {tab("g1"), tab("g1"), tab(""), tab("g2"), tab("g1")};
  const std::vector<std::vector<size_t>> groups = SavedTabGroups(tabs);
  ASSERT_EQ(groups.size(), 3u);
  EXPECT_EQ(groups[0], (std::vector<size_t>{0, 1}));
  EXPECT_EQ(groups[1], (std::vector<size_t>{3}));
  EXPECT_EQ(groups[2], (std::vector<size_t>{4}));
  EXPECT_TRUE(SavedTabGroups({tab(""), tab("")}).empty());
}

TEST(SavedTabsTest, SplitRoundTrip) {
  SavedTab tab{GURL("https://a.com/"), u"A"};
  tab.split_id = "s1";
  tab.split_layout = 1;
  tab.split_ratio = 0.3;
  base::ListValue list;
  list.Append(SavedTabToDict(tab));
  const std::vector<SavedTab> tabs = SavedTabsFromList(list);
  ASSERT_EQ(tabs.size(), 1u);
  EXPECT_EQ(tabs[0].split_id, "s1");
  EXPECT_EQ(tabs[0].split_layout, 1);
  EXPECT_DOUBLE_EQ(tabs[0].split_ratio, 0.3);
  EXPECT_FALSE(SavedTabToDict({GURL("https://b.com/"), u"B"}).contains("split"));
}

TEST(SavedTabsTest, NameAndPinnedUrlRoundTrip) {
  SavedTab tab{GURL("https://mail.example.com/sent"), u"Sent - Mail", /*pinned=*/true};
  tab.name = u"Mail";
  tab.pinned_url = GURL("https://mail.example.com/inbox");
  base::ListValue list;
  list.Append(SavedTabToDict(tab));
  list.Append(SavedTabToDict({GURL("https://b.com/"), u"B"}));
  const std::vector<SavedTab> tabs = SavedTabsFromList(list);
  ASSERT_EQ(tabs.size(), 2u);
  EXPECT_EQ(tabs[0].name, u"Mail");
  EXPECT_EQ(tabs[0].pinned_url, GURL("https://mail.example.com/inbox"));
  EXPECT_EQ(tabs[0].url, GURL("https://mail.example.com/sent"));
  // Nothing set: nothing written, so records stay small.
  EXPECT_TRUE(tabs[1].name.empty());
  EXPECT_FALSE(tabs[1].pinned_url.is_valid());
  EXPECT_FALSE(list[1].GetDict().contains("name"));
  EXPECT_FALSE(list[1].GetDict().contains("pinned_url"));
}

TEST(SavedTabsTest, NameIsReadAsTypedNamesAreKept) {
  base::ListValue list;
  list.Append(base::DictValue().Set("url", "https://a.com/").Set("name", "  Two \n lines "));
  const std::vector<SavedTab> tabs = SavedTabsFromList(list);
  ASSERT_EQ(tabs.size(), 1u);
  EXPECT_EQ(tabs[0].name, u"Two lines");
}

TEST(SavedTabsTest, InvalidPinnedUrlIsDropped) {
  base::ListValue list;
  list.Append(base::DictValue().Set("url", "https://a.com/").Set("pinned_url", "nope"));
  const std::vector<SavedTab> tabs = SavedTabsFromList(list);
  ASSERT_EQ(tabs.size(), 1u);
  EXPECT_FALSE(tabs[0].pinned_url.is_valid());
}

TEST(SavedTabsTest, BadSplitRatioFallsBackToHalf) {
  base::ListValue list;
  list.Append(base::DictValue()
                  .Set("url", "https://a.com/")
                  .Set("split", base::DictValue().Set("id", "s1").Set("ratio", 7.0)));
  const std::vector<SavedTab> saved = SavedTabsFromList(list);
  ASSERT_EQ(saved.size(), 1u);
  EXPECT_DOUBLE_EQ(saved[0].split_ratio, 0.5);
}

TEST(SavedTabsTest, SplitsArePairsOfTheSameId) {
  auto tab = [](std::string split) {
    SavedTab t{GURL("https://a.com/"), u"A"};
    t.split_id = std::move(split);
    return t;
  };
  // [s1 s1] loose [s2] (a lone half is dropped) [s3 s3 s3] (not a pair: dropped).
  const std::vector<SavedTab> tabs = {tab("s1"), tab("s1"), tab(""),  tab("s2"),
                                      tab("s3"), tab("s3"), tab("s3")};
  const std::vector<std::vector<size_t>> splits = SavedTabSplits(tabs);
  ASSERT_EQ(splits.size(), 1u);
  EXPECT_EQ(splits[0], (std::vector<size_t>{0, 1}));
}

TEST(MergeSavedTabsTest, ImportedTabsGoAfterTheirKind) {
  auto tab = [](const char* url, bool pinned, bool active = false) {
    SavedTab t;
    t.url = GURL(url);
    t.pinned = pinned;
    t.active = active;
    return t;
  };
  std::vector<SavedTab> merged =
      MergeSavedTabs({tab("https://p1/", true), tab("https://t1/", false, /*active=*/true)},
                     {tab("https://t2/", false, /*active=*/true), tab("https://p2/", true)});
  std::vector<std::string> urls;
  for (const SavedTab& t : merged) {
    urls.push_back(t.url.spec());
  }
  EXPECT_EQ(urls,
            (std::vector<std::string>{"https://p1/", "https://p2/", "https://t1/", "https://t2/"}));
  EXPECT_EQ(ActiveSavedTab(merged), 2u);  // still the space's own active tab
  EXPECT_FALSE(merged[3].active);
  EXPECT_TRUE(MergeSavedTabs({}, {}).empty());
}

TEST(SpaceModelTest, UntouchedTabsForImport) {
  // First run's window holds the welcome page: its space still counts as empty for import.
  EXPECT_TRUE(IsUntouchedTabUrl(GURL("chrome://welcome/")));
  EXPECT_TRUE(IsUntouchedTabUrl(GURL("chrome://newtab/")));
  EXPECT_TRUE(IsUntouchedTabUrl(GURL()));
  EXPECT_FALSE(IsUntouchedTabUrl(GURL("https://example.com/")));
  EXPECT_FALSE(IsUntouchedTabUrl(GURL("chrome://settings/")));
}

}  // namespace
}  // namespace lb
