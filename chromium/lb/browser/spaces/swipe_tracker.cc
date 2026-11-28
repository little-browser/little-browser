// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/spaces/swipe_tracker.h"

#include <cmath>

namespace lb {

SwipeTracker::Result SwipeTracker::OnScroll(float dx, float dy, base::TimeTicks now) {
  if (std::abs(dx) <= std::abs(dy)) {
    return {};  // vertical: ordinary scrolling
  }
  if (last_.is_null() || now - last_ > kGestureGap) {
    accumulated_ = 0;
    switched_ = false;  // a new gesture
  }
  last_ = now;
  if (switched_) {
    return {.handled = true};
  }
  accumulated_ += dx;
  if (std::abs(accumulated_) > kThreshold) {
    switched_ = true;
    return {.handled = true, .step = accumulated_ < 0 ? 1 : -1};
  }
  return {.handled = true};
}

}  // namespace lb
