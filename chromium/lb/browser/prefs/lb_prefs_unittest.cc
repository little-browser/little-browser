// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/prefs/lb_prefs.h"

#include "base/time/time.h"
#include "components/prefs/testing_pref_service.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

// Every platform registers the same Local State, empty to start with: no spaces, no saved tabs,
// filter lists never downloaded.
TEST(LbPrefsTest, LocalStateStartsEmpty) {
  TestingPrefServiceSimple prefs;
  RegisterLocalStatePrefs(prefs.registry());
  EXPECT_TRUE(prefs.GetList(kSpacesPref).empty());
  EXPECT_TRUE(prefs.GetDict(kSpaceTabsPref).empty());
  EXPECT_EQ(prefs.GetTime(kFilterListsUpdatedPref), base::Time());
}

// A new profile has no active space yet, and hasn't been asked first run's questions.
TEST(LbPrefsTest, ProfileStartsUnasked) {
  sync_preferences::TestingPrefServiceSyncable prefs;
  RegisterProfilePrefs(prefs.registry());
  EXPECT_EQ(prefs.GetString(kActiveSpacePref), "");
  EXPECT_FALSE(prefs.GetBoolean(kSearchChoiceShownPref));
  EXPECT_FALSE(prefs.GetBoolean(kPasswordManagerChoiceShownPref));
}

// The names are the saved form other devices and older profiles read: they never change.
TEST(LbPrefsTest, NamesAreStable) {
  EXPECT_STREQ(kSpacesPref, "lb.spaces");
  EXPECT_STREQ(kSpaceTabsPref, "lb.space_tabs");
  EXPECT_STREQ(kFilterListsUpdatedPref, "lb.filter_lists_updated");
  EXPECT_STREQ(kActiveSpacePref, "lb.active_space");
  EXPECT_STREQ(kSearchChoiceShownPref, "lb.search_choice_shown");
  EXPECT_STREQ(kPasswordManagerChoiceShownPref, "lb.password_manager_choice_shown");
}

}  // namespace
}  // namespace lb
