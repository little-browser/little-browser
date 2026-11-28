// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/common/page_names.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

using Names = std::vector<std::string>;

TEST(PageNamesTest, SplitsClassesAndSendsEachNameOnce) {
  PageNames names;
  names.AddElement("  ad-box\twide\nAdBox ", "top");
  names.AddElement("wide ad-box", "");
  PageNames::Batch first = names.TakeNew();
  EXPECT_EQ(first.classes, Names({"ad-box", "wide", "AdBox"}));
  EXPECT_EQ(first.ids, Names({"top"}));

  // Already sent: nothing new. A new name comes in the next batch alone.
  names.AddElement("wide", "top");
  EXPECT_TRUE(names.TakeNew().empty());
  names.AddElement("wide banner", "side");
  PageNames::Batch second = names.TakeNew();
  EXPECT_EQ(second.classes, Names({"banner"}));
  EXPECT_EQ(second.ids, Names({"side"}));
  // A class and an id may share a name: they are different rules.
  names.AddElement("side", "banner");
  PageNames::Batch third = names.TakeNew();
  EXPECT_EQ(third.classes, Names({"side"}));
  EXPECT_EQ(third.ids, Names({"banner"}));
}

TEST(PageNamesTest, EmptyAttributes) {
  PageNames names;
  names.AddElement("", "");
  names.AddElement("   ", "");
  EXPECT_TRUE(names.TakeNew().empty());
}

TEST(PageNamesTest, StopsAtTheLimit) {
  PageNames names;
  for (size_t i = 0; i < PageNames::kMaxNames + 10; ++i) {
    EXPECT_FALSE(names.full());
    names.AddElement("c" + std::to_string(i), "");
    if (names.full()) {
      break;
    }
  }
  EXPECT_TRUE(names.full());
  EXPECT_EQ(names.TakeNew().classes.size(), PageNames::kMaxNames);
  names.AddElement("another", "and-an-id");
  EXPECT_TRUE(names.TakeNew().empty());
}

TEST(PageNamesTest, LongNamesAreNeverSent) {
  PageNames names;
  const std::string longest(PageNames::kMaxNameLength, 'a');
  const std::string too_long(PageNames::kMaxNameLength + 1, 'b');
  names.AddElement(longest + " " + too_long, too_long);
  PageNames::Batch batch = names.TakeNew();
  EXPECT_EQ(batch.classes, Names({longest}));
  EXPECT_TRUE(batch.ids.empty());
}

TEST(PageNamesTest, WhatTheBrowserAccepts) {
  EXPECT_TRUE(PageNames::Fits({}, {}));
  EXPECT_TRUE(PageNames::Fits({"ad", std::string(PageNames::kMaxNameLength, 'a')}, {"top"}));
  EXPECT_FALSE(PageNames::Fits({std::string(PageNames::kMaxNameLength + 1, 'a')}, {}));
  EXPECT_FALSE(PageNames::Fits({}, {""}));
  std::vector<std::string> many(PageNames::kMaxNames, "x");
  EXPECT_TRUE(PageNames::Fits(many, {}));
  EXPECT_FALSE(PageNames::Fits(many, {"one-more"}));
}

}  // namespace
}  // namespace lb
