// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The native side of app.littlebrowser.lb.password.PasswordManagers: lb-core's password-manager
// choice through its C++ binding (lb/core_bridge/tabs.rs), which only converts types. The rule
// and the list are tested in lb-core.

#include <jni.h>

#include <cstdint>
#include <string>
#include <vector>

#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "lb/android/jni_headers/PasswordManagers_jni.h"
#include "lb/browser/rust_strings.h"
#include "lb/core_bridge/tabs.rs.h"

namespace lb {

static std::vector<std::string> JNI_PasswordManagers_List(JNIEnv* env) {
  std::vector<std::string> fields;
  for (const core::PasswordManagerInfo& manager : core::password_managers()) {
    fields.emplace_back(std::string(manager.name));
    fields.emplace_back(std::string(manager.about));
    fields.emplace_back(std::string(manager.android_package));
  }
  return fields;
}

static int32_t JNI_PasswordManagers_NextStep(JNIEnv* env,
                                            bool off_the_record,
                                            bool offered_before,
                                            const std::vector<std::string>& installed) {
  const core::PasswordManagerChoiceState state{
      .off_the_record = off_the_record,
      .offered_before = offered_before,
      .installed = ToRustStrings(installed),
  };
  // PasswordManagers.Step's values, by name: the bridge's enum may be reordered.
  switch (core::password_manager_choice_step(state)) {
    case core::PasswordManagerChoiceStep::Offer:
      return 0;
    case core::PasswordManagerChoiceStep::Skip:
      return 1;
    case core::PasswordManagerChoiceStep::Settled:
      return 2;
  }
  return 1;
}

}  // namespace lb

DEFINE_JNI(PasswordManagers)
