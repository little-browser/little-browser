// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/password/password_manager_choice.h"

#include <algorithm>

namespace lb {

namespace {

constexpr PasswordManager kPasswordManagers[] = {
    {u"1Password", u"Closed source, but widely used.", "aeblfdkhhhdcdjpifhhbdiojplfjncoa"},
    {u"Bitwarden", u"Open source, can run on your own server, with a large user base.",
     "nngceckbapebfimnlniiiahkandclblb"},
    {u"Proton Pass", u"Open source, end-to-end encrypted.", "ghmbeldphafepmbegfdlkpapadhbakde"},
};

}  // namespace

base::span<const PasswordManager> PasswordManagers() {
  return kPasswordManagers;
}

PasswordManagerChoiceStep NextPasswordManagerChoiceStep(const PasswordManagerChoiceState& state) {
  if (state.off_the_record || state.offered_before) {
    return PasswordManagerChoiceStep::kSkip;
  }
  for (const PasswordManager& manager : PasswordManagers()) {
    if (std::ranges::find(state.installed_extension_ids, manager.extension_id) !=
        state.installed_extension_ids.end()) {
      return PasswordManagerChoiceStep::kSettled;
    }
  }
  return PasswordManagerChoiceStep::kOffer;
}

AfterPasswordManagerInstall AfterPasswordManagerInstallResult(PasswordManagerInstallResult result,
                                                              const PasswordManager& manager) {
  const std::u16string name = manager.name;
  switch (result) {
    case PasswordManagerInstallResult::kInstalled:
    case PasswordManagerInstallResult::kDeclined:
      return {};
    case PasswordManagerInstallResult::kInterrupted:
      return {.offer_again = true};
    case PasswordManagerInstallResult::kStoreUnreachable:
      return {.offer_again = true,
              .message = u"Little Browser couldn't reach the Chrome Web Store to add " + name +
                         u". It will offer password managers again the next time you open a "
                         u"window."};
    case PasswordManagerInstallResult::kNotInstallable:
      return {.offer_again = false,
              .message = name +
                         u" can't be added from the Chrome Web Store here. You can choose "
                         u"another password manager in the store."};
  }
  return {};
}

}  // namespace lb
