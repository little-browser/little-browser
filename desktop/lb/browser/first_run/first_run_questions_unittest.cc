// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/first_run/first_run_questions.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

TEST(FirstRunQuestionsTest, AskedInAnOpenWindow) {
  EXPECT_TRUE(CanAskFirstRunQuestion({}));
}

// Closing the window closes the open question from inside the window's teardown; the next one
// must not open over that window (it crashed, the window being half gone).
TEST(FirstRunQuestionsTest, NotAskedOverAClosingWindow) {
  EXPECT_FALSE(CanAskFirstRunQuestion({.closing = true}));
}

// Quitting closes the windows, and the open question with them; the next one must not open in
// any window, the one closing or one that waited for it.
TEST(FirstRunQuestionsTest, NotAskedWhileQuitting) {
  EXPECT_FALSE(CanAskFirstRunQuestion({.app_quitting = true}));
  EXPECT_FALSE(CanAskFirstRunQuestion({.closing = true, .app_quitting = true}));
}

}  // namespace
}  // namespace lb
