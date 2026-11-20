// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/pinned_close.h"

namespace lb {

bool MayHoldUnsavedInput(const PinnedCloseState& state) {
  return state.asks_before_leaving && state.had_user_edits;
}

PinnedCloseOutcome DecidePinnedClose(const PinnedCloseState& state) {
  // A sleeping pinned tab closes for good (its "−" button). So does one in a split view, which
  // Chromium closes out of the split, and one being closed by Chromium itself.
  if (!state.user_gesture || state.closing_all || !state.pinned || state.split || state.asleep) {
    return PinnedCloseOutcome::kClose;
  }
  // A close already hid it: this one is real.
  if (state.hidden_by_earlier_close) {
    return PinnedCloseOutcome::kCloseAskingFirst;
  }
  const bool unsaved = MayHoldUnsavedInput(state);
  // Not shown, so there is nothing to hide, and it can't be unloaded.
  if (!state.shown && unsaved) {
    return PinnedCloseOutcome::kCloseAskingFirst;
  }
  if (state.sleep_pending) {
    return PinnedCloseOutcome::kAlreadySleeping;
  }
  return unsaved ? PinnedCloseOutcome::kHide : PinnedCloseOutcome::kSleep;
}

}  // namespace lb
