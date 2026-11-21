// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_PASSWORD_PASSWORD_MANAGER_CHOICE_H_
#define LB_BROWSER_PASSWORD_PASSWORD_MANAGER_CHOICE_H_

#include <optional>
#include <string>
#include <vector>

#include "base/containers/span.h"

namespace lb {

// A password manager offered on first run (docs/ux.md section 12): an extension from the Chrome
// Web Store.
struct PasswordManager {
  const char16_t* name;
  // One line about it, shown after the name.
  const char16_t* about;
  // Its Chrome Web Store id.
  const char* extension_id;
};

// The password managers offered, in alphabetical order so the order favours no one.
base::span<const PasswordManager> PasswordManagers();

// What decides whether the choice is offered.
struct PasswordManagerChoiceState {
  bool off_the_record = false;
  // The choice was offered to this profile before (whatever the answer).
  bool offered_before = false;
  // Ids of the extensions installed in the profile.
  std::vector<std::string> installed_extension_ids;
};

enum class PasswordManagerChoiceStep {
  kOffer,
  // Not now (incognito, or offered before).
  kSkip,
  // Never: one of the offered password managers is installed already. Recorded as offered, so the
  // choice doesn't turn up in some later window if that extension is removed.
  kSettled,
};

// Offer the choice once per profile, on first run, not in incognito, and not when one of the
// offered password managers is already installed.
PasswordManagerChoiceStep NextPasswordManagerChoiceStep(const PasswordManagerChoiceState& state);

// How the store install the choice started ended (Chromium's webstore_install::Result, grouped).
enum class PasswordManagerInstallResult {
  kInstalled,
  // The user said no in the install prompt.
  kDeclined,
  // The install stopped without an answer: the window closed, or an install is under way.
  kInterrupted,
  // The store couldn't be reached, or the download failed.
  kStoreUnreachable,
  // The store won't install it here (blocked by policy, blocklisted, not compatible, ...).
  kNotInstallable,
};

struct AfterPasswordManagerInstall {
  // Offer the choice again the next time a window opens, as if it had never been offered.
  bool offer_again = false;
  // Tell the user why nothing was added (the dialog has closed by then).
  std::optional<std::u16string> message;
};

// What follows an install of `manager`: nothing after an answer in the store's prompt; otherwise
// the choice comes back, and the user hears why nothing was added unless they closed the window.
AfterPasswordManagerInstall AfterPasswordManagerInstallResult(PasswordManagerInstallResult result,
                                                              const PasswordManager& manager);

}  // namespace lb

#endif  // LB_BROWSER_PASSWORD_PASSWORD_MANAGER_CHOICE_H_
