// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/load_progress.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

TEST(LoadProgressTest, NothingShownWhileNothingLoads) {
  LoadProgress progress;
  EXPECT_EQ(progress.line(), LoadProgressLine());
  progress.Update({.loading = false, .progress = 1});
  EXPECT_EQ(progress.line(), LoadProgressLine());
}

TEST(LoadProgressTest, GrowsWithTheLoad) {
  LoadProgress progress;
  progress.Update({.loading = true, .progress = 0.1});
  EXPECT_EQ(progress.line(), (LoadProgressLine{.visible = true, .fraction = 0.1}));
  progress.Update({.loading = true, .progress = 0.6});
  EXPECT_EQ(progress.line().fraction, 0.6);
}

TEST(LoadProgressTest, NeverShrinksDuringALoad) {
  LoadProgress progress;
  progress.Update({.loading = true, .progress = 0.7});
  // A redirect makes Chromium's estimate start over.
  progress.Update({.loading = true, .progress = 0.1});
  EXPECT_EQ(progress.line().fraction, 0.7);
}

TEST(LoadProgressTest, FillsAndFadesWhenTheLoadEnds) {
  LoadProgress progress;
  progress.Update({.loading = true, .progress = 0.4});
  progress.Update({.loading = false, .progress = 1});
  EXPECT_EQ(progress.line(), (LoadProgressLine{.visible = true, .fraction = 1, .finishing = true}));
  // Another report of the ended load changes nothing.
  progress.Update({.loading = false, .progress = 1});
  EXPECT_TRUE(progress.line().finishing);
  progress.FadedOut();
  EXPECT_EQ(progress.line(), LoadProgressLine());
}

TEST(LoadProgressTest, ALoadThatStartsWhileTheLastFadesStartsOver) {
  LoadProgress progress;
  progress.Update({.loading = true, .progress = 0.5});
  progress.Update({.loading = false, .progress = 1});
  progress.Update({.loading = true, .progress = 0.1});
  EXPECT_EQ(progress.line(), (LoadProgressLine{.visible = true, .fraction = 0.1}));
  // The old fade ending doesn't hide the new load.
  progress.FadedOut();
  EXPECT_TRUE(progress.line().visible);
}

TEST(LoadProgressTest, AnotherPageShownTakesItsLoadAsItIs) {
  LoadProgress progress;
  progress.Update({.loading = true, .progress = 0.9});
  // A tab still loading, behind: its own progress, even if lower.
  progress.Reset({.loading = true, .progress = 0.3});
  EXPECT_EQ(progress.line(), (LoadProgressLine{.visible = true, .fraction = 0.3}));
  // A loaded tab: nothing, and no fade from the one before.
  progress.Reset({.loading = false, .progress = 1});
  EXPECT_EQ(progress.line(), LoadProgressLine());
}

TEST(LoadProgressTest, ProgressOutOfRangeIsClamped) {
  LoadProgress progress;
  progress.Update({.loading = true, .progress = -1});
  EXPECT_EQ(progress.line().fraction, 0);
  progress.Update({.loading = true, .progress = 3});
  EXPECT_EQ(progress.line().fraction, 1);
  progress.Reset({.loading = true, .progress = 2});
  EXPECT_EQ(progress.line().fraction, 1);
}

}  // namespace
}  // namespace lb
