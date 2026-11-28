// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/update/update_reminder.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

base::Time At(int hours) {
  return base::Time::UnixEpoch() + base::Days(20000) + base::Hours(hours);
}

UpdateOfferState Found(const std::string& version, int now_hours) {
  UpdateOfferState state;
  state.version = version;
  state.now = At(now_hours);
  return state;
}

TEST(UpdateReminderTest, OffersAnUpdateNeverPutOff) {
  EXPECT_TRUE(ShouldOfferUpdate(Found("8103.90.1", 0)));
}

TEST(UpdateReminderTest, LaterHoldsAnUpdateBackForADay) {
  UpdateOfferState state = Found("8103.90.1", 4);
  state.later = UpdateLater{"8103.90.1", At(0)};
  EXPECT_FALSE(ShouldOfferUpdate(state));
  state.now = At(23);
  EXPECT_FALSE(ShouldOfferUpdate(state));
  state.now = At(24);
  EXPECT_TRUE(ShouldOfferUpdate(state));
  state.now = At(100);
  EXPECT_TRUE(ShouldOfferUpdate(state));
}

TEST(UpdateReminderTest, SecurityUpdateAsksAgainAtTheNextCheck) {
  UpdateOfferState state = Found("8103.90.1", 0);
  state.security = true;
  state.later = UpdateLater{"8103.90.1", At(0)};
  EXPECT_FALSE(ShouldOfferUpdate(state));  // the same check, or one right after it
  state.now = At(0) + kSecurityUpdateRemindAfter;
  EXPECT_TRUE(ShouldOfferUpdate(state));
  // Scheduled checks are four hours apart, so the next one asks.
  state.now = At(4);
  EXPECT_TRUE(ShouldOfferUpdate(state));
  EXPECT_LT(kSecurityUpdateRemindAfter, base::Hours(4));
  EXPECT_LT(kSecurityUpdateRemindAfter, kUpdateRemindAfter);
}

TEST(UpdateReminderTest, NewerVersionIsOfferedRightAway) {
  UpdateOfferState state = Found("8103.90.2", 1);
  state.later = UpdateLater{"8103.90.1", At(0)};
  EXPECT_TRUE(ShouldOfferUpdate(state));
}

TEST(UpdateReminderTest, UsersOwnCheckAlwaysOffers) {
  UpdateOfferState state = Found("8103.90.1", 1);
  state.later = UpdateLater{"8103.90.1", At(0)};
  state.user_initiated = true;
  EXPECT_TRUE(ShouldOfferUpdate(state));
}

TEST(UpdateReminderTest, ClockThatWentBackDoesNotHoldItBack) {
  UpdateOfferState state = Found("8103.90.1", 0);
  state.later = UpdateLater{"8103.90.1", At(48)};
  EXPECT_TRUE(ShouldOfferUpdate(state));
}

UpdateWindowState Shown() {
  UpdateWindowState state;
  state.visible = true;
  state.front_usable = true;
  return state;
}

TEST(UpdateReminderTest, ShownUpdateWindowGoesAboveTheFrontBrowserWindow) {
  EXPECT_EQ(PlaceUpdateWindow(Shown()), UpdateWindowMove::kAttachToFront);
}

TEST(UpdateReminderTest, UpdateWindowAboveTheFrontWindowStays) {
  UpdateWindowState state = Shown();
  state.has_parent = true;
  state.parent_is_front = true;
  EXPECT_EQ(PlaceUpdateWindow(state), UpdateWindowMove::kKeep);
}

TEST(UpdateReminderTest, UpdateWindowFollowsAnotherWindowComingToTheFront) {
  UpdateWindowState state = Shown();
  state.has_parent = true;
  state.parent_is_front = false;
  EXPECT_EQ(PlaceUpdateWindow(state), UpdateWindowMove::kAttachToFront);
}

TEST(UpdateReminderTest, UpdateWindowWaitsWithoutAUsableFrontWindow) {
  // None on screen, all minimised, or the front one on another Space.
  UpdateWindowState state = Shown();
  state.front_usable = false;
  EXPECT_EQ(PlaceUpdateWindow(state), UpdateWindowMove::kKeep);
  state.has_parent = true;  // keeps the window it is above
  EXPECT_EQ(PlaceUpdateWindow(state), UpdateWindowMove::kKeep);
}

TEST(UpdateReminderTest, UpdateWindowLeavesAParentThatGoesAway) {
  UpdateWindowState state = Shown();
  state.has_parent = true;
  state.parent_is_front = true;
  state.parent_going_away = true;
  EXPECT_EQ(PlaceUpdateWindow(state), UpdateWindowMove::kDetach);
  state.front_usable = false;
  EXPECT_EQ(PlaceUpdateWindow(state), UpdateWindowMove::kDetach);
}

TEST(UpdateReminderTest, HiddenUpdateWindowIsNoLongerAChild) {
  UpdateWindowState state;
  state.visible = false;
  state.has_parent = true;
  state.front_usable = true;
  EXPECT_EQ(PlaceUpdateWindow(state), UpdateWindowMove::kDetach);
  state.has_parent = false;
  EXPECT_EQ(PlaceUpdateWindow(state), UpdateWindowMove::kKeep);
}

TEST(UpdateReminderTest, UpdateWindowClosedWithItsParentComesBackAboveTheNextWindow) {
  // Closing the window it is above orders that window out first, and the update window with it.
  UpdateWindowState state;
  state.visible = false;
  state.has_parent = true;
  state.parent_hidden = true;
  state.front_usable = true;
  EXPECT_EQ(PlaceUpdateWindow(state), UpdateWindowMove::kShowAgainAboveFront);
  // No other window to go above yet: it waits for one.
  state.front_usable = false;
  EXPECT_EQ(PlaceUpdateWindow(state), UpdateWindowMove::kShowAgainLater);
  state.has_parent = false;
  state.parent_hidden = false;
  state.hidden_with_parent = true;
  EXPECT_EQ(PlaceUpdateWindow(state), UpdateWindowMove::kShowAgainLater);
  state.front_usable = true;
  EXPECT_EQ(PlaceUpdateWindow(state), UpdateWindowMove::kShowAgainAboveFront);
}

TEST(UpdateReminderTest, UpdateWindowSparkleHidItselfStaysHidden) {
  // Off screen with its parent on screen: Sparkle ordered it out.
  UpdateWindowState state;
  state.visible = false;
  state.has_parent = true;
  state.front_usable = true;
  EXPECT_EQ(PlaceUpdateWindow(state), UpdateWindowMove::kDetach);
  state.has_parent = false;
  EXPECT_EQ(PlaceUpdateWindow(state), UpdateWindowMove::kKeep);
}

TEST(UpdateReminderTest, SparklesPromptAndProgressAreUpdateWindows) {
  EXPECT_TRUE(IsUpdateWindow("SUUpdateAlert"));
  EXPECT_TRUE(IsUpdateWindow("SUStatusController"));
  EXPECT_FALSE(IsUpdateWindow("BrowserWindowController"));
  EXPECT_FALSE(IsUpdateWindow("SUUpdateAlertX"));
  EXPECT_FALSE(IsUpdateWindow(""));
}

}  // namespace
}  // namespace lb
