// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/password/password_manager_choice.h"

#include <string>
#include <vector>

#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "lb/browser/rust_strings.h"
#include "lb/core_bridge/tabs.rs.h"

namespace lb {

namespace {

// lb-core's list (lb_core::password_manager), kept for the life of the process so the entries'
// pointers stay valid.
struct PasswordManagerList {
  PasswordManagerList() {
    for (const core::PasswordManagerInfo& manager : core::password_managers()) {
      names.push_back(base::UTF8ToUTF16(std::string(manager.name)));
      abouts.push_back(base::UTF8ToUTF16(std::string(manager.about)));
      ids.push_back(std::string(manager.extension_id));
    }
    for (size_t i = 0; i < names.size(); ++i) {
      entries.push_back({names[i].c_str(), abouts[i].c_str(), ids[i].c_str()});
    }
  }
  std::vector<std::u16string> names;
  std::vector<std::u16string> abouts;
  std::vector<std::string> ids;
  std::vector<PasswordManager> entries;
};

}  // namespace

base::span<const PasswordManager> PasswordManagers() {
  static const base::NoDestructor<PasswordManagerList> list;
  return list->entries;
}

PasswordManagerChoiceStep NextPasswordManagerChoiceStep(const PasswordManagerChoiceState& state) {
  const core::PasswordManagerChoiceState choice{
      .off_the_record = state.off_the_record,
      .offered_before = state.offered_before,
      .installed = ToRustStrings(state.installed_extension_ids)};
  switch (core::password_manager_choice_step(choice)) {
    case core::PasswordManagerChoiceStep::Offer:
      return PasswordManagerChoiceStep::kOffer;
    case core::PasswordManagerChoiceStep::Skip:
      return PasswordManagerChoiceStep::kSkip;
    case core::PasswordManagerChoiceStep::Settled:
      return PasswordManagerChoiceStep::kSettled;
  }
  return PasswordManagerChoiceStep::kSkip;
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
