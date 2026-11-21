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

}  // namespace
}  // namespace lb
