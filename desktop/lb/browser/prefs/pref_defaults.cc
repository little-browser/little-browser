// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/prefs/pref_defaults.h"

#include "base/values.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/pref_registry/pref_registry_syncable.h"

namespace lb {

void SetProfilePrefDefaults(user_prefs::PrefRegistrySyncable* registry) {
  registry->SetDefaultPrefValue(autofill::prefs::kAutofillProfileEnabled, base::Value(false));
}

}  // namespace lb
