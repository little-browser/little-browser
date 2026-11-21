// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_PASSWORD_PASSWORD_MANAGER_CHOICE_DIALOG_H_
#define LB_BROWSER_UI_VIEWS_PASSWORD_PASSWORD_MANAGER_CHOICE_DIALOG_H_

#include <string>

class BrowserWindowInterface;

namespace lb {

// Profile pref: the first-run password manager choice was offered (registered with the sidebar's
// prefs).
inline constexpr char kPasswordManagerChoiceShownPref[] = "lb.password_manager_choice_shown";

// Little Browser keeps no passwords; on first run the user can pick a password manager extension
// (docs/ux.md section 12). Shows the choice once per profile, unless one of the offered password
// managers is installed already (NextPasswordManagerChoiceStep). Picking one shows Chromium's
// install prompt, then installs it from the Chrome Web Store; if that fails before the user
// answers the prompt, the user is told and the choice comes back
// (AfterPasswordManagerInstallResult).
void MaybeShowPasswordManagerChoice(BrowserWindowInterface* browser);

// Whether the choice is open in any window (the --lb-sidebar-demo=password-manager check).
bool IsPasswordManagerChoiceOpenForTesting();

// Picks the password manager named `name` and presses "Add", as the user does. Returns false if
// the choice isn't open or has no such entry.
bool ChoosePasswordManagerForTesting(const std::u16string& name);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_PASSWORD_PASSWORD_MANAGER_CHOICE_DIALOG_H_
