// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_PREFS_LB_PREFS_H_
#define LB_BROWSER_PREFS_LB_PREFS_H_

#include "build/build_config.h"

class PrefRegistrySimple;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace lb {

// Our prefs that every Chromium platform keeps, in the same form, so devices read each other's
// (docs/android-ui.md, "One state format").

// Local State (browser-wide): the spaces, and the saved tabs of each space no window shows (kept
// apart from lb.spaces so saving tabs doesn't notify every window that observes the spaces).
inline constexpr char kSpacesPref[] = "lb.spaces";
inline constexpr char kSpaceTabsPref[] = "lb.space_tabs";
// Local State: when the ad blocker's filter lists were last downloaded.
inline constexpr char kFilterListsUpdatedPref[] = "lb.filter_lists_updated";

// Profile prefs: the profile's last active space, and whether first run's search engine and
// password manager questions were asked in it.
inline constexpr char kActiveSpacePref[] = "lb.active_space";
inline constexpr char kSearchChoiceShownPref[] = "lb.search_choice_shown";
inline constexpr char kPasswordManagerChoiceShownPref[] = "lb.password_manager_choice_shown";

// Register the prefs above, on every platform (chromium/patches/sidebar-prefs.patch).
void RegisterLocalStatePrefs(PrefRegistrySimple* registry);
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

#if !BUILDFLAG(IS_ANDROID)
// The desktop's own: import's Local State, and its profile prefs with the defaults it sets for
// Chromium's tab-group prefs (defined with the sidebar,
// browser/ui/views/sidebar/sidebar_controller.cc). The defaults every platform shares are in
// pref_defaults.h.
void RegisterDesktopLocalStatePrefs(PrefRegistrySimple* registry);
void RegisterDesktopProfilePrefs(user_prefs::PrefRegistrySyncable* registry);
#endif

}  // namespace lb

#endif  // LB_BROWSER_PREFS_LB_PREFS_H_
