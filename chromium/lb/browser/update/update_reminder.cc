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

UpdateWindowMove PlaceUpdateWindow(const UpdateWindowState& state) {
  if (!state.visible) {
    // Taken off screen by its parent, not by Sparkle: it comes back above the next window.
    if ((state.has_parent && state.parent_hidden) ||
        (!state.has_parent && state.hidden_with_parent)) {
      return state.front_usable ? UpdateWindowMove::kShowAgainAboveFront
                                : UpdateWindowMove::kShowAgainLater;
    }
    return state.has_parent ? UpdateWindowMove::kDetach : UpdateWindowMove::kKeep;
  }
  if (state.has_parent && state.parent_going_away) {
    return UpdateWindowMove::kDetach;  // attached again to the next front window
  }
  if (state.front_usable && !(state.has_parent && state.parent_is_front)) {
    return UpdateWindowMove::kAttachToFront;
  }
  return UpdateWindowMove::kKeep;  // above the front browser window already, or none to go above
}

bool IsUpdateWindow(std::string_view window_controller_class) {
  return window_controller_class == "SUUpdateAlert" ||
         window_controller_class == "SUStatusController";
}

}  // namespace lb
