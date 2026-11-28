// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/spaces/swipe_tracker.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

class SwipeTrackerTest : public testing::Test {
 protected:
  // A sideways scroll event `ms` after the previous one.
  SwipeTracker::Result Scroll(float dx, int ms = 16, float dy = 0) {
    now_ += base::Milliseconds(ms);
    return tracker_.OnScroll(dx, dy, now_);
  }

  SwipeTracker tracker_;
  base::TimeTicks now_ = base::TimeTicks() + base::Seconds(10);
};

TEST_F(SwipeTrackerTest, VerticalScrollIsIgnored) {
  const SwipeTracker::Result result = Scroll(10, 16, /*dy=*/40);
  EXPECT_FALSE(result.handled);
  EXPECT_EQ(result.step, 0);
}

TEST_F(SwipeTrackerTest, SmallSwipeDoesNotSwitch) {
  const SwipeTracker::Result result = Scroll(-50);
  EXPECT_TRUE(result.handled);
  EXPECT_EQ(result.step, 0);
}

TEST_F(SwipeTrackerTest, SwipeLeftGoesToNextSpace) {
  EXPECT_EQ(Scroll(-50).step, 0);
  EXPECT_EQ(Scroll(-50).step, 1);
}

TEST_F(SwipeTrackerTest, SwipeRightGoesToPreviousSpace) {
  EXPECT_EQ(Scroll(100).step, -1);
}

// The bug this guards against: the trackpad's momentum events kept accumulating after the
// switch and switched again, so the space bounced back.
TEST_F(SwipeTrackerTest, OneSwitchPerGestureIncludingMomentum) {
  EXPECT_EQ(Scroll(-100).step, 1);
  for (int i = 0; i < 60; ++i) {  // about a second of momentum, either direction
    const SwipeTracker::Result result = Scroll(i % 2 ? -40 : 40);
    EXPECT_TRUE(result.handled);
    EXPECT_EQ(result.step, 0);
  }
}

TEST_F(SwipeTrackerTest, PauseStartsANewGesture) {
  EXPECT_EQ(Scroll(-100).step, 1);
  const int pause_ms = SwipeTracker::kGestureGap.InMilliseconds() + 1;
  EXPECT_EQ(Scroll(-100, pause_ms).step, 1);
}

TEST_F(SwipeTrackerTest, FlingStartDiscardsDistance) {
  EXPECT_EQ(Scroll(-80).step, 0);
  tracker_.OnFlingStart();
  EXPECT_EQ(Scroll(-80).step, 0);
}

}  // namespace
}  // namespace lb
