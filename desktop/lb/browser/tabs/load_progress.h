// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_TABS_LOAD_PROGRESS_H_
#define LB_BROWSER_TABS_LOAD_PROGRESS_H_

#include "base/time/time.h"

namespace lb {

// The load progress line (docs/ux.md §2): a thin line along the top edge of the page, under the
// toolbar, in every window, that grows as the shown page loads and fades out once it has.

// Its height, and how long it takes to catch up with the page's progress and to fade out.
inline constexpr int kLoadProgressHeight = 2;
inline constexpr base::TimeDelta kLoadProgressGrowDuration = base::Milliseconds(200);
inline constexpr base::TimeDelta kLoadProgressFadeDuration = base::Milliseconds(250);

// The shown page's load, as Chromium reports it: whether it is loading a new document (not a
// change within the page, such as a "#" link) and how far it has got, 0 to 1
// (WebContents::GetLoadProgress, an estimate that starts at 0.1).
struct PageLoad {
  bool loading = false;
  double progress = 0;
};

// What the line shows.
struct LoadProgressLine {
  bool visible = false;
  // How much of the page's width it fills, 0 to 1. It never shrinks during one load.
  double fraction = 0;
  // The load is done: the line is full and fades out (then LoadProgress::FadedOut).
  bool finishing = false;
  bool operator==(const LoadProgressLine&) const = default;
};

// The line's state, fed with the shown page's load.
class LoadProgress {
 public:
  // Another page is shown (a tab switch, a new window): the line shows its load as it is, with
  // no fade from the page shown before.
  void Reset(const PageLoad& load);
  // The shown page's load changed: a load that starts shows the line at its progress (also while
  // the last one fades); one going on only grows it; one that ends fills it and fades it out.
  void Update(const PageLoad& load);
  // The fade is over: the line is gone.
  void FadedOut();

  const LoadProgressLine& line() const { return line_; }

 private:
  LoadProgressLine line_;
};

}  // namespace lb

#endif  // LB_BROWSER_TABS_LOAD_PROGRESS_H_
