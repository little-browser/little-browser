// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/demo/demo_log.h"

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

// Each test uses its own modes: a mode is reported once per process.

TEST(DemoLogTest, ReportsAModeOnce) {
  EXPECT_TRUE(ReportDemo("demo-log-once", true, "first"));
  EXPECT_FALSE(ReportDemo("demo-log-once", false, "second"));
  EXPECT_TRUE(ReportDemo("demo-log-once-other", false, "another mode"));
}

TEST(DemoLogTest, DeadlineFailsAModeWithoutAResult) {
  base::test::SingleThreadTaskEnvironment env(base::test::TaskEnvironment::TimeSource::MOCK_TIME);
  FailDemoAfter("demo-log-deadline", base::Seconds(10));
  env.FastForwardBy(base::Seconds(10));
  // The deadline has reported it, so a late result is dropped.
  EXPECT_FALSE(ReportDemo("demo-log-deadline", true, "too late"));
}

TEST(DemoLogTest, ResultBeforeTheDeadlineWins) {
  base::test::SingleThreadTaskEnvironment env(base::test::TaskEnvironment::TimeSource::MOCK_TIME);
  FailDemoAfter("demo-log-in-time", base::Seconds(10));
  env.FastForwardBy(base::Seconds(5));
  EXPECT_TRUE(ReportDemo("demo-log-in-time", true, "in time"));
  env.FastForwardBy(base::Seconds(10));  // the deadline's FAIL is dropped
  EXPECT_FALSE(ReportDemo("demo-log-in-time", false, "again"));
}

TEST(DemoLogTest, PostDemoTaskRunsAfterTheDelay) {
  base::test::SingleThreadTaskEnvironment env(base::test::TaskEnvironment::TimeSource::MOCK_TIME);
  bool ran = false;
  PostDemoTask(base::BindOnce([](bool* ran) { *ran = true; }, &ran), base::Seconds(2));
  env.FastForwardBy(base::Seconds(1));
  EXPECT_FALSE(ran);
  env.FastForwardBy(base::Seconds(1));
  EXPECT_TRUE(ran);
}

}  // namespace
}  // namespace lb
