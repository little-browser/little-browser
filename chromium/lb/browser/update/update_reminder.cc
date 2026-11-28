// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/update/update_reminder.h"

namespace lb {

bool ShouldOfferUpdate(const UpdateOfferState& state) {
  if (state.user_initiated || !state.later || state.later->version != state.version) {
    return true;
  }
  if (state.now < state.later->when) {
    return true;  // the clock went back
  }
  const base::TimeDelta wait = state.security ? kSecurityUpdateRemindAfter : kUpdateRemindAfter;
  return state.now - state.later->when >= wait;
}

}  // namespace lb
