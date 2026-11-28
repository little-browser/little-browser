// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/pinned_close.h"

#include "lb/core_bridge/tabs.rs.h"

namespace lb {

namespace {

// The rule is lb-core's (lb_core::tabs::decide_pinned_close).
core::PinnedCloseState ToCore(const PinnedCloseState& state) {
  return {
      .user_gesture = state.user_gesture,
      .closing_all = state.closing_all,
      .pinned = state.pinned,
      .split = state.split,
      .asleep = state.asleep,
      .shown = state.shown,
      .hidden_by_earlier_close = state.hidden_by_earlier_close,
      .sleep_pending = state.sleep_pending,
      .asks_before_leaving = state.asks_before_leaving,
      .had_user_edits = state.had_user_edits,
  };
}

}  // namespace

bool MayHoldUnsavedInput(const PinnedCloseState& state) {
  return core::may_hold_unsaved_input(ToCore(state));
}

PinnedCloseOutcome DecidePinnedClose(const PinnedCloseState& state) {
  switch (core::decide_pinned_close(ToCore(state))) {
    case core::PinnedCloseOutcome::Close:
      return PinnedCloseOutcome::kClose;
    case core::PinnedCloseOutcome::CloseAskingFirst:
      return PinnedCloseOutcome::kCloseAskingFirst;
    case core::PinnedCloseOutcome::Sleep:
      return PinnedCloseOutcome::kSleep;
    case core::PinnedCloseOutcome::Hide:
      return PinnedCloseOutcome::kHide;
    case core::PinnedCloseOutcome::AlreadySleeping:
      return PinnedCloseOutcome::kAlreadySleeping;
  }
  return PinnedCloseOutcome::kClose;
}

}  // namespace lb
