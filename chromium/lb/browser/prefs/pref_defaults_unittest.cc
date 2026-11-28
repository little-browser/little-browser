// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/prefs/pref_defaults.h"

#include "base/feature_list.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/payments/core/payment_prefs.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

// The prefs SetProfilePrefDefaults changes, registered the way Chromium does: with their owners'
// registration where it is small, otherwise with upstream Chromium's default, so each test checks
// that our default replaces upstream's whatever the base build registers.
class PrefDefaultsTest : public testing::Test {
 protected:
  PrefDefaultsTest() {
    auto* registry = prefs_.registry();
    autofill::prefs::RegisterProfilePrefs(registry);
    content_settings::CookieSettings::RegisterProfilePrefs(registry);
    payments::RegisterProfilePrefs(registry);
    registry->RegisterBooleanPref(prefs::kSearchSuggestEnabled, true);
    registry->RegisterBooleanPref(prefs::kEnableHyperlinkAuditing, true);
    registry->RegisterBooleanPref(password_manager::prefs::kCredentialsEnableService, true);
    registry->RegisterBooleanPref(password_manager::prefs::kCredentialsEnableAutosignin, true);
    // Upstream's defaults where ungoogled-chromium's differ, so the desktop build's base doesn't
    // make a test pass on its own.
    registry->SetDefaultPrefValue(autofill::prefs::kAutofillProfileEnabled, base::Value(true));
    registry->SetDefaultPrefValue(autofill::prefs::kAutofillCreditCardEnabled, base::Value(true));
    registry->SetDefaultPrefValue(
        prefs::kCookieControlsMode,
        base::Value(static_cast<int>(content_settings::CookieControlsMode::kIncognitoOnly)));
    registry->SetDefaultPrefValue(payments::kCanMakePaymentEnabled, base::Value(true));
    SetProfilePrefDefaults(registry);
  }

  sync_preferences::TestingPrefServiceSyncable prefs_;
};

TEST_F(PrefDefaultsTest, ThirdPartyCookiesAreBlocked) {
  EXPECT_EQ(prefs_.GetInteger(prefs::kCookieControlsMode),
            static_cast<int>(content_settings::CookieControlsMode::kBlockThirdParty));
  prefs_.SetInteger(prefs::kCookieControlsMode,
                    static_cast<int>(content_settings::CookieControlsMode::kOff));
  EXPECT_EQ(prefs_.GetInteger(prefs::kCookieControlsMode),
            static_cast<int>(content_settings::CookieControlsMode::kOff));
}

TEST_F(PrefDefaultsTest, NoSearchSuggestionsAndNoPings) {
  EXPECT_FALSE(prefs_.GetBoolean(prefs::kSearchSuggestEnabled));
  EXPECT_FALSE(prefs_.GetBoolean(prefs::kEnableHyperlinkAuditing));
  prefs_.SetBoolean(prefs::kSearchSuggestEnabled, true);
  EXPECT_TRUE(prefs_.GetBoolean(prefs::kSearchSuggestEnabled));
}

// Addresses and cards aren't saved by default, so forms don't offer to save them; the user can turn
// them on.
TEST_F(PrefDefaultsTest, AddressesAndCardsAreNotSavedByDefault) {
  EXPECT_FALSE(autofill::prefs::IsAutofillProfileEnabled(&prefs_));
  EXPECT_FALSE(autofill::prefs::IsAutofillPaymentMethodsEnabled(&prefs_));

  // The user's own choice still wins.
  prefs_.SetBoolean(autofill::prefs::kAutofillProfileEnabled, true);
  EXPECT_TRUE(autofill::prefs::IsAutofillProfileEnabled(&prefs_));
}

TEST_F(PrefDefaultsTest, ChromiumDoesNotSavePasswords) {
  EXPECT_FALSE(prefs_.GetBoolean(password_manager::prefs::kCredentialsEnableService));
  EXPECT_FALSE(prefs_.GetBoolean(password_manager::prefs::kCredentialsEnableAutosignin));
}

TEST_F(PrefDefaultsTest, SitesCannotQuerySavedPaymentMethods) {
  EXPECT_FALSE(prefs_.GetBoolean(payments::kCanMakePaymentEnabled));
}

// HTTPS-First is on in Balanced mode by default (docs/design.md §5). We keep Chromium's own default
// rather than set it: while the user hasn't touched the setting, IsBalancedModeEnabled()
// (chrome/browser/ssl/https_upgrades_util.cc) follows these two features, not a pref. This fails if
// a Chromium or ungoogled-chromium update turns either off.
TEST(PrefDefaultsFeaturesTest, HttpsFirstBalancedModeIsOnByDefault) {
  EXPECT_TRUE(base::FeatureList::IsEnabled(features::kHttpsFirstBalancedMode));
  EXPECT_TRUE(base::FeatureList::IsEnabled(features::kHttpsFirstBalancedModeAutoEnable));
}

}  // namespace
}  // namespace lb
