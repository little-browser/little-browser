// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/pinned_close.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

using Outcome = PinnedCloseOutcome;

// The user closes the shown, awake pinned tab of an ordinary page.
PinnedCloseState Awake() {
  return PinnedCloseState();
}

PinnedCloseState WithUnsavedInput() {
  PinnedCloseState state;
  state.asks_before_leaving = true;
  state.had_user_edits = true;
  return state;
}

TEST(PinnedCloseTest, AwakePinnedTabSleeps) {
  EXPECT_EQ(DecidePinnedClose(Awake()), Outcome::kSleep);
}

TEST(PinnedCloseTest, AwakePinnedTabNotShownSleeps) {
  PinnedCloseState state = Awake();
  state.shown = false;
  EXPECT_EQ(DecidePinnedClose(state), Outcome::kSleep);
}

// Many pages register a beforeunload handler and never ask: they must still unload, stay
// pinned and keep their place, whether shown or not.
TEST(PinnedCloseTest, BeforeUnloadHandlerWithoutTypingSleeps) {
  PinnedCloseState state = Awake();
  state.asks_before_leaving = true;
  EXPECT_EQ(DecidePinnedClose(state), Outcome::kSleep);
  state.shown = false;
  EXPECT_EQ(DecidePinnedClose(state), Outcome::kSleep);
}

// Typing alone, on a page that doesn't ask before it is left, doesn't keep it loaded.
TEST(PinnedCloseTest, TypingWithoutBeforeUnloadHandlerSleeps) {
  PinnedCloseState state = Awake();
  state.had_user_edits = true;
  EXPECT_EQ(DecidePinnedClose(state), Outcome::kSleep);
}

TEST(PinnedCloseTest, UnsavedInputShownIsOnlyHidden) {
  EXPECT_EQ(DecidePinnedClose(WithUnsavedInput()), Outcome::kHide);
}

TEST(PinnedCloseTest, UnsavedInputClosedAgainAsksAndCloses) {
  PinnedCloseState state = WithUnsavedInput();
  state.shown = false;  // hiding it showed another tab
  state.hidden_by_earlier_close = true;
  EXPECT_EQ(DecidePinnedClose(state), Outcome::kCloseAskingFirst);
}

TEST(PinnedCloseTest, UnsavedInputNotShownAsksAndCloses) {
  PinnedCloseState state = WithUnsavedInput();
  state.shown = false;
  EXPECT_EQ(DecidePinnedClose(state), Outcome::kCloseAskingFirst);
}

// The page's "Leave" closes the tab again: that close must not be stopped.
TEST(PinnedCloseTest, CloseAfterThePageAskedIsNotStopped) {
  PinnedCloseState state = WithUnsavedInput();
  state.hidden_by_earlier_close = true;
  EXPECT_EQ(DecidePinnedClose(state), Outcome::kCloseAskingFirst);
}

TEST(PinnedCloseTest, AsleepPinnedTabCloses) {
  PinnedCloseState state = Awake();
  state.asleep = true;
  EXPECT_EQ(DecidePinnedClose(state), Outcome::kClose);
}

// A second close (a double click, or Cmd+W pressed twice) before the sleep has run.
TEST(PinnedCloseTest, ClosedAgainBeforeSleepingDoesNothingMore) {
  PinnedCloseState state = Awake();
  state.sleep_pending = true;
  EXPECT_EQ(DecidePinnedClose(state), Outcome::kAlreadySleeping);
}

TEST(PinnedCloseTest, ClosesThatArentTheUsersClose) {
  PinnedCloseState not_user = Awake();
  not_user.user_gesture = false;
  EXPECT_EQ(DecidePinnedClose(not_user), Outcome::kClose);

  PinnedCloseState window = Awake();
  window.closing_all = true;
  EXPECT_EQ(DecidePinnedClose(window), Outcome::kClose);

  PinnedCloseState unpinned = Awake();
  unpinned.pinned = false;
  EXPECT_EQ(DecidePinnedClose(unpinned), Outcome::kClose);

  PinnedCloseState split = Awake();
  split.split = true;
  EXPECT_EQ(DecidePinnedClose(split), Outcome::kClose);
}

TEST(PinnedCloseTest, UnpinnedTabWithUnsavedInputClosesAsUsual) {
  PinnedCloseState state = WithUnsavedInput();
  state.pinned = false;
  EXPECT_EQ(DecidePinnedClose(state), Outcome::kClose);
}

}  // namespace
}  // namespace lb
