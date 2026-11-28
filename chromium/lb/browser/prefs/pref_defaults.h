// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_PREFS_PREF_DEFAULTS_H_
#define LB_BROWSER_PREFS_PREF_DEFAULTS_H_

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace lb {

// Our defaults for Chromium profile prefs, the same on every Chromium platform (docs/design.md §5
// and §7.8). Call after Chromium has registered them: chromium/patches/sidebar-prefs.patch calls it
// right after chrome::RegisterProfilePrefs. The user's own choice still wins over each.
//   - Third-party cookies blocked.
//   - No search suggestions: each keystroke would go to the search engine.
//   - No hyperlink auditing (<a ping>).
//   - Addresses and payment cards aren't saved or filled, so a form gives no "Save address?"
//     prompt, until the user turns on Settings' "Save and fill addresses".
//   - Chromium doesn't save passwords or sign in with them: that's a password manager's job.
//   - Sites can't check for saved payment methods.
// Prefs that Chromium registers later (sign-in, in its keyed services) or only on one platform
// keep their defaults in that platform's patches.
void SetProfilePrefDefaults(user_prefs::PrefRegistrySyncable* registry);

}  // namespace lb

#endif  // LB_BROWSER_PREFS_PREF_DEFAULTS_H_
