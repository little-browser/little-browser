// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/prefs/pref_defaults.h"

#include "base/values.h"
#include "chrome/common/pref_names.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/payments/core/payment_prefs.h"
#include "components/pref_registry/pref_registry_syncable.h"

namespace lb {

void SetProfilePrefDefaults(user_prefs::PrefRegistrySyncable* registry) {
  registry->SetDefaultPrefValue(
      prefs::kCookieControlsMode,
      base::Value(static_cast<int>(content_settings::CookieControlsMode::kBlockThirdParty)));
  registry->SetDefaultPrefValue(prefs::kSearchSuggestEnabled, base::Value(false));
  registry->SetDefaultPrefValue(prefs::kEnableHyperlinkAuditing, base::Value(false));
  registry->SetDefaultPrefValue(autofill::prefs::kAutofillProfileEnabled, base::Value(false));
  registry->SetDefaultPrefValue(autofill::prefs::kAutofillCreditCardEnabled, base::Value(false));
  registry->SetDefaultPrefValue(password_manager::prefs::kCredentialsEnableService,
                                base::Value(false));
  registry->SetDefaultPrefValue(password_manager::prefs::kCredentialsEnableAutosignin,
                                base::Value(false));
  registry->SetDefaultPrefValue(payments::kCanMakePaymentEnabled, base::Value(false));
}

}  // namespace lb
