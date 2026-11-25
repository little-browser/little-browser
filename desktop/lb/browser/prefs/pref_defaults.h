// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_PREFS_PREF_DEFAULTS_H_
#define LB_BROWSER_PREFS_PREF_DEFAULTS_H_

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace lb {

// Changes the defaults of Chromium profile prefs that Little Browser sets differently from its
// base. Call after Chromium has registered them (lb::RegisterProfilePrefs runs last).
//   - Addresses aren't saved or filled, so a form gives no "Save address?" prompt, until the user
//     turns on Settings' "Save and fill addresses" (docs/design.md §7.8). Set here, not left to
//     ungoogled-chromium, which happens to turn it off too, so an update there can't turn it on.
void SetProfilePrefDefaults(user_prefs::PrefRegistrySyncable* registry);

}  // namespace lb

#endif  // LB_BROWSER_PREFS_PREF_DEFAULTS_H_
