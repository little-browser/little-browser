// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/prefs/lb_prefs.h"

#include <string>

#include "base/time/time.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_registry_simple.h"

namespace lb {

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kSpacesPref);
  registry->RegisterDictionaryPref(kSpaceTabsPref);
  registry->RegisterTimePref(kFilterListsUpdatedPref, base::Time());
}

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(kActiveSpacePref, std::string());
  registry->RegisterBooleanPref(kSearchChoiceShownPref, false);
  registry->RegisterBooleanPref(kPasswordManagerChoiceShownPref, false);
}

}  // namespace lb
