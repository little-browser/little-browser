// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/pinned_groups.h"

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

using ::testing::ElementsAre;
using Groups = std::vector<std::string>;

TEST(PinnedGroupsTest, SessionDataRoundTrips) {
  const PinnedGroup group{.id = "g1", .title = u"Réading", .color = 5, .collapsed = true};
  const std::optional<PinnedGroup> back =
      PinnedGroupFromSessionData(PinnedGroupToSessionData(group));
  ASSERT_TRUE(back);
  EXPECT_EQ(back->id, "g1");
  EXPECT_EQ(back->title, u"Réading");
  EXPECT_EQ(back->color, 5);
  EXPECT_TRUE(back->collapsed);
}

TEST(PinnedGroupsTest, EmptyOrBadSessionDataIsNoGroup) {
  EXPECT_FALSE(PinnedGroupFromSessionData(""));
  EXPECT_FALSE(PinnedGroupFromSessionData("{"));
  EXPECT_FALSE(PinnedGroupFromSessionData("[]"));
  EXPECT_FALSE(PinnedGroupFromSessionData(R"({"title": "No id"})"));
  EXPECT_FALSE(PinnedGroupFromSessionData(R"({"id": ""})"));
  const std::optional<PinnedGroup> bare = PinnedGroupFromSessionData(R"({"id": "g"})");
  ASSERT_TRUE(bare);
  EXPECT_EQ(bare->title, u"");
  EXPECT_EQ(bare->color, 0);
  EXPECT_FALSE(bare->collapsed);
}

TEST(PinnedGroupsTest, NormalRunsStay) {
  EXPECT_EQ(NormalizePinnedGroups({}), Groups());
  EXPECT_EQ(NormalizePinnedGroups({"", "a", "a", "", "b", "b"}),
            Groups({"", "a", "a", "", "b", "b"}));
}

TEST(PinnedGroupsTest, ALooseTabInsideAGroupJoinsIt) {
  EXPECT_EQ(NormalizePinnedGroups({"a", "", "a"}), Groups({"a", "a", "a"}));
  EXPECT_EQ(NormalizePinnedGroups({"a", "", "", "a", ""}), Groups({"a", "a", "a", "a", ""}));
  EXPECT_EQ(NormalizePinnedGroups({"a", "a", "", "", "a"}), Groups({"a", "a", "a", "a", "a"}));
}

TEST(PinnedGroupsTest, ALooseTabBetweenTwoGroupsStaysLoose) {
  EXPECT_EQ(NormalizePinnedGroups({"a", "", "b"}), Groups({"a", "", "b"}));
}

TEST(PinnedGroupsTest, ATabMovedAwayFromItsGroupLeavesIt) {
  EXPECT_EQ(NormalizePinnedGroups({"a", "a", "b", "a"}), Groups({"a", "a", "b", ""}));
  EXPECT_EQ(NormalizePinnedGroups({"a", "b", "a", "a"}), Groups({"", "b", "a", "a"}));
}

TEST(PinnedGroupsTest, TheFirstLongestRunWins) {
  EXPECT_EQ(NormalizePinnedGroups({"a", "b", "a"}), Groups({"a", "b", ""}));
  EXPECT_EQ(NormalizePinnedGroups({"a", "b", "a", "b", "b"}), Groups({"a", "", "", "b", "b"}));
}

TEST(PinnedGroupsTest, AMovedTabJoinsTheGroupItLandsIn) {
  EXPECT_EQ(GroupAfterMove("", "a", "a", false), "a");
  EXPECT_EQ(GroupAfterMove("b", "a", "a", true), "a");
}

TEST(PinnedGroupsTest, AMovedTabNextToItsGroupStaysInIt) {
  EXPECT_EQ(GroupAfterMove("a", "a", "", true), "a");  // after its group's last tab
  EXPECT_EQ(GroupAfterMove("a", "", "a", true), "a");  // before its first
  EXPECT_EQ(GroupAfterMove("a", "b", "a", true), "a");
}

TEST(PinnedGroupsTest, AMovedTabAwayFromItsGroupLeavesIt) {
  EXPECT_EQ(GroupAfterMove("a", "", "", true), "");
  EXPECT_EQ(GroupAfterMove("a", "b", "", true), "");
  EXPECT_EQ(GroupAfterMove("a", "b", "c", true), "");
  // A new tab at a group's edge stays out of it.
  EXPECT_EQ(GroupAfterMove("", "a", "", false), "");
  EXPECT_EQ(GroupAfterMove("", "a", "b", false), "");
}

TEST(PinnedGroupsTest, TheFirstTabOfAGroupToComeBackKeepsIt) {
  EXPECT_EQ(GroupAfterMove("a", "b", "", false), "a");
  EXPECT_EQ(GroupAfterMove("a", "", "", false), "a");
}

// A group of a 30-high header and two 30-high tabs, from 100 to 190; a dragged tab is 30 high.
constexpr Span kGroup{100, 190};
constexpr int kHeader = 30;
Span DraggedAt(int top) {
  return {top, top + 30};
}

TEST(PinnedGroupsTest, ADraggedTabJoinsOnceItOverlapsByFortyPercentOfTheHeader) {
  EXPECT_FALSE(DraggedTabInPinnedGroup(DraggedAt(59), kGroup, kHeader, false, false));
  EXPECT_FALSE(DraggedTabInPinnedGroup(DraggedAt(81), kGroup, kHeader, false, false));  // 11
  EXPECT_TRUE(DraggedTabInPinnedGroup(DraggedAt(82), kGroup, kHeader, false, false));   // 12
  EXPECT_TRUE(DraggedTabInPinnedGroup(DraggedAt(178), kGroup, kHeader, false, false));  // 12
  EXPECT_FALSE(DraggedTabInPinnedGroup(DraggedAt(179), kGroup, kHeader, false, false));
}

TEST(PinnedGroupsTest, ACollapsedGroupTakesNoDraggedTab) {
  EXPECT_FALSE(DraggedTabInPinnedGroup(DraggedAt(100), kGroup, kHeader, true, false));
}

TEST(PinnedGroupsTest, ADraggedTabLeavesOnceAlmostOut) {
  EXPECT_TRUE(DraggedTabInPinnedGroup(DraggedAt(160), kGroup, kHeader, false, true));   // all in
  EXPECT_TRUE(DraggedTabInPinnedGroup(DraggedAt(163), kGroup, kHeader, false, true));   // 27
  EXPECT_FALSE(DraggedTabInPinnedGroup(DraggedAt(164), kGroup, kHeader, false, true));  // 26
  EXPECT_TRUE(DraggedTabInPinnedGroup(DraggedAt(97), kGroup, kHeader, false, true));
  EXPECT_FALSE(DraggedTabInPinnedGroup(DraggedAt(96), kGroup, kHeader, false, true));
  // A collapsed group's own tab stays while it overlaps.
  EXPECT_TRUE(DraggedTabInPinnedGroup(DraggedAt(100), {100, 130}, kHeader, true, true));
}

TEST(PinnedGroupsTest, AGroupsLastTabDraggedInPlaceStaysIn) {
  // The group's span counts the dragged tab's own place: header 100-130, the last tab 160-190.
  EXPECT_TRUE(DraggedTabInPinnedGroup(DraggedAt(160), kGroup, kHeader, false, true));
  EXPECT_TRUE(DraggedTabInPinnedGroup(DraggedAt(162), kGroup, kHeader, false, true));
  // A one-tab group: header 100-130, its tab 134-164.
  EXPECT_TRUE(DraggedTabInPinnedGroup(DraggedAt(134), {100, 164}, kHeader, false, true));
}

TEST(PinnedGroupsTest, ADroppedGroupGoesBeforeTheFirstTabBelowIt) {
  const std::vector<int> middles = {10, 30, 50};
  const Groups none = {"", "", ""};
  EXPECT_EQ(GroupDropIndex(middles, none, 0), 0u);
  EXPECT_EQ(GroupDropIndex(middles, none, 10), 0u);
  EXPECT_EQ(GroupDropIndex(middles, none, 11), 1u);
  EXPECT_EQ(GroupDropIndex(middles, none, 40), 2u);
  EXPECT_EQ(GroupDropIndex(middles, none, 99), 3u);
  EXPECT_EQ(GroupDropIndex({}, {}, 5), 0u);
}

TEST(PinnedGroupsTest, ADroppedGroupNeverLandsInsideAnother) {
  const std::vector<int> middles = {10, 30, 50, 70, 90};
  const Groups groups = {"", "a", "a", "a", "b"};
  EXPECT_EQ(GroupDropIndex(middles, groups, 20), 1u);  // before a
  EXPECT_EQ(GroupDropIndex(middles, groups, 40), 4u);  // inside a: after it
  EXPECT_EQ(GroupDropIndex(middles, groups, 60), 4u);
  EXPECT_EQ(GroupDropIndex(middles, groups, 80), 4u);  // between a and b
  EXPECT_EQ(GroupDropIndex(middles, groups, 95), 5u);
}

TEST(PinnedGroupsTest, TheDropLineGoesAboveTheRowTheGroupGoesBefore) {
  const std::vector<Span> rows = {{0, 30}, {30, 60}, {90, 120}};  // the last starts a group
  EXPECT_EQ(GroupDropLineY(rows, 0, 500), 0);
  EXPECT_EQ(GroupDropLineY(rows, 2, 500), 90);
  EXPECT_EQ(GroupDropLineY(rows, 3, 500), 120);
  EXPECT_EQ(GroupDropLineY({}, 0, 500), 500);
}

TEST(PinnedGroupsTest, AfterACollapsedGroupTheDropLineGoesBelowItsShownTab) {
  // A collapsed group's header (30-60) with its shown tab below it (60-90); its last tab is
  // hidden, so its row is the header's.
  const std::vector<Span> rows = {{30, 90}, {60, 90}, {30, 60}};
  EXPECT_EQ(GroupDropLineY(rows, 3, 500), 90);
}

TEST(PinnedGroupsTest, WhatAGroupDropDoes) {
  EXPECT_EQ(DecideGroupDrop(true, 100, 99), GroupDrop::kMovePinned);
  EXPECT_EQ(DecideGroupDrop(true, 100, 100), GroupDrop::kUnpin);
  EXPECT_EQ(DecideGroupDrop(false, 100, 99), GroupDrop::kPin);
  EXPECT_EQ(DecideGroupDrop(false, 100, 100), GroupDrop::kNothing);
  // No "+ New Tab" row (the collapsed strip, tabs on top): nothing.
  EXPECT_EQ(DecideGroupDrop(true, std::nullopt, 0), GroupDrop::kNothing);
  EXPECT_EQ(DecideGroupDrop(false, std::nullopt, 0), GroupDrop::kNothing);
}

TEST(PinnedGroupsTest, CloseSleepsTheAwakeTabsAndLeavesHiddenOnes) {
  const PinnedGroupClose close = DecidePinnedGroupClose(
      {{.asleep = false}, {.asleep = true}, {.hidden_after_close = true}, {.asleep = false}});
  EXPECT_FALSE(close.remove);
  EXPECT_THAT(close.tabs, ElementsAre(0u, 3u));
}

TEST(PinnedGroupsTest, CloseRemovesOnceEveryTabSleepsOrIsHidden) {
  PinnedGroupClose close = DecidePinnedGroupClose({{.asleep = true}, {.hidden_after_close = true}});
  EXPECT_TRUE(close.remove);
  EXPECT_THAT(close.tabs, ElementsAre(0u, 1u));
  close = DecidePinnedGroupClose({{.asleep = true}, {.asleep = true}});
  EXPECT_TRUE(close.remove);
  EXPECT_THAT(close.tabs, ElementsAre(0u, 1u));
}

TEST(PinnedGroupsTest, ANewGroupGoesWhereItsFirstItemIs) {
  // [A, (B), X, (C)]: B and C make the group; it goes where B is, after A.
  EXPECT_EQ(NewPinnedGroupPosition({"", "", "", ""}, {false, true, false, true}), 1u);
  EXPECT_EQ(NewPinnedGroupPosition({"", ""}, {true, true}), 0u);
}

TEST(PinnedGroupsTest, ANewGroupNeverGoesInsideAnother) {
  // [X1, (X2), X3] all in x: X2 leaves x for the new group, which goes after X3, so x stays whole.
  EXPECT_EQ(NewPinnedGroupPosition({"x", "", "x"}, {false, true, false}), 2u);
  // At a group's edge it stays there.
  EXPECT_EQ(NewPinnedGroupPosition({"", "x", "x"}, {true, false, false}), 0u);
  EXPECT_EQ(NewPinnedGroupPosition({"x", "x", ""}, {false, false, true}), 2u);
}

TEST(PinnedGroupsTest, RowsHaveAHeaderBeforeEachGroup) {
  const std::vector<PinnedGroup> groups = {{.id = "a"}, {.id = "b"}};
  const std::vector<PinnedRow> rows = PinnedRows({{""}, {"a"}, {"a"}, {"b"}, {""}}, groups);
  ASSERT_EQ(rows.size(), 7u);
  EXPECT_FALSE(rows[0].header);
  EXPECT_EQ(rows[0].item, 0u);
  EXPECT_FALSE(rows[0].indented);
  EXPECT_EQ(rows[1].header, 0u);
  EXPECT_EQ(rows[2].item, 1u);
  EXPECT_TRUE(rows[2].indented);
  EXPECT_EQ(rows[3].item, 2u);
  EXPECT_EQ(rows[4].header, 1u);
  EXPECT_EQ(rows[5].item, 3u);
  EXPECT_TRUE(rows[5].indented);
  EXPECT_EQ(rows[6].item, 4u);
  EXPECT_FALSE(rows[6].indented);
  for (const PinnedRow& row : rows) {
    EXPECT_TRUE(row.shown);
  }
}

TEST(PinnedGroupsTest, ACollapsedGroupShowsOnlyItsActiveTab) {
  const std::vector<PinnedGroup> groups = {{.id = "a", .collapsed = true}};
  std::vector<PinnedRow> rows = PinnedRows({{"a"}, {"a", true}, {"", false}}, groups);
  ASSERT_EQ(rows.size(), 4u);
  EXPECT_EQ(rows[0].header, 0u);
  EXPECT_FALSE(rows[1].shown);
  EXPECT_TRUE(rows[2].shown);
  EXPECT_TRUE(rows[3].shown);

  rows = PinnedRows({{"a"}, {"a"}}, groups);
  ASSERT_EQ(rows.size(), 3u);
  EXPECT_TRUE(rows[0].shown);  // the header
  EXPECT_FALSE(rows[1].shown);
  EXPECT_FALSE(rows[2].shown);
}

TEST(PinnedGroupsTest, AnUnknownGroupCountsAsLoose) {
  const std::vector<PinnedRow> rows = PinnedRows({{"gone"}}, {});
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_FALSE(rows[0].header);
  EXPECT_FALSE(rows[0].indented);
}

TEST(PinnedGroupsTest, MenuClosesUntilEveryTabSleeps) {
  EXPECT_THAT(PinnedGroupMenu(false),
              ElementsAre(PinnedGroupCommand::kRename, PinnedGroupCommand::kColor,
                          PinnedGroupCommand::kUnpin, PinnedGroupCommand::kUngroup,
                          PinnedGroupCommand::kClose));
  EXPECT_EQ(PinnedGroupMenu(true).back(), PinnedGroupCommand::kRemove);
}

}  // namespace
}  // namespace lb
