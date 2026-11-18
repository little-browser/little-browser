// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_SPACES_SWIPE_TRACKER_H_
#define LB_BROWSER_SPACES_SWIPE_TRACKER_H_

#include "base/time/time.h"

namespace lb {

// Turns two-finger sideways scrolling over the sidebar into "switch space" steps (unit tested
// in swipe_tracker_unittest.cc).
//
// One switch per gesture: after a switch, the rest of the gesture (including the trackpad's
// momentum phase, which can last a second or more) is swallowed until the events pause.
class SwipeTracker {
 public:
  // Scroll distance that switches a space.
  static constexpr float kThreshold = 90;
  // A pause this long starts a new gesture.
  static constexpr base::TimeDelta kGestureGap = base::Milliseconds(300);

  struct Result {
    // The event belongs to a sideways swipe; don't scroll with it.
    bool handled = false;
    // +1: next space, -1: previous space, 0: no switch. Content moves with the fingers, so
    // swiping left (negative dx) reveals the next space.
    int step = 0;
  };

  Result OnScroll(float dx, float dy, base::TimeTicks now);

  // A fling (momentum) starting: its distance doesn't count towards a switch.
  void OnFlingStart() { accumulated_ = 0; }

 private:
  float accumulated_ = 0;
  base::TimeTicks last_;
  bool switched_ = false;
};

}  // namespace lb

#endif  // LB_BROWSER_SPACES_SWIPE_TRACKER_H_
