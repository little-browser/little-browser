// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/prefs/pref_defaults.h"

#include "components/autofill/core/common/autofill_prefs.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

// Imported and newly entered addresses are saved: without this, Chromium drops them silently.
TEST(PrefDefaultsTest, AddressesAreSavedByDefault) {
  sync_preferences::TestingPrefServiceSyncable prefs;
  autofill::prefs::RegisterProfilePrefs(prefs.registry());
  SetProfilePrefDefaults(prefs.registry());
  EXPECT_TRUE(autofill::prefs::IsAutofillProfileEnabled(&prefs));

  // The user's own choice still wins.
  prefs.SetBoolean(autofill::prefs::kAutofillProfileEnabled, false);
  EXPECT_FALSE(autofill::prefs::IsAutofillProfileEnabled(&prefs));
}

}  // namespace
}  // namespace lb
