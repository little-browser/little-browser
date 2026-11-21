// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/search/search_choice.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

TEST(SearchChoiceTest, AskedOnFirstRun) {
  EXPECT_EQ(NextSearchChoiceStep({}), SearchChoiceStep::kAsk);
}

// Every later window and launch: straight on to the next question.
TEST(SearchChoiceTest, SkippedOnceOffered) {
  EXPECT_EQ(NextSearchChoiceStep({.offered_before = true}), SearchChoiceStep::kSkip);
}

TEST(SearchChoiceTest, SkippedInIncognito) {
  EXPECT_EQ(NextSearchChoiceStep({.off_the_record = true}), SearchChoiceStep::kSkip);
  EXPECT_EQ(NextSearchChoiceStep({.off_the_record = true, .open = true}), SearchChoiceStep::kSkip);
}

TEST(SearchChoiceTest, SkippedWhenAnEngineIsSet) {
  EXPECT_EQ(NextSearchChoiceStep({.no_search_engine = false}), SearchChoiceStep::kSkip);
}

TEST(SearchChoiceTest, SkippedWithoutSearchEngines) {
  EXPECT_EQ(NextSearchChoiceStep({.has_engines = false}), SearchChoiceStep::kSkip);
}

TEST(SearchChoiceTest, WaitsForTheEnginesToLoad) {
  EXPECT_EQ(NextSearchChoiceStep({.engines_loaded = false}), SearchChoiceStep::kWaitForEngines);
}

// A second window opened while the first shows the choice (it counts as offered by then): the
// password manager question waits for it rather than opening at the same time.
TEST(SearchChoiceTest, WaitsForTheChoiceOpenInAnotherWindow) {
  EXPECT_EQ(NextSearchChoiceStep({.open = true, .offered_before = true}),
            SearchChoiceStep::kWaitForOpenChoice);
  EXPECT_EQ(NextSearchChoiceStep({.open = true}), SearchChoiceStep::kWaitForOpenChoice);
}

}  // namespace
}  // namespace lb
