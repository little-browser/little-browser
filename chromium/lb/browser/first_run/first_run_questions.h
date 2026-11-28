// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_FIRST_RUN_FIRST_RUN_QUESTIONS_H_
#define LB_BROWSER_FIRST_RUN_FIRST_RUN_QUESTIONS_H_

namespace lb {

// The window a first-run question (docs/ux.md section 12: the import offer, the search engine,
// the password manager) would open over. Each question opens when the one before it closes, and
// closing the window or quitting closes the open one, from inside the window's teardown.
struct FirstRunQuestionWindow {
  // The window is closing: its delete is scheduled, or it is already being torn down.
  bool closing = false;
  // The app is quitting.
  bool app_quitting = false;
};

// Whether a first-run question may open over `window` now: not over a closing window, and not
// while the app quits. A question not asked for this reason isn't recorded as asked, so the next
// window asks it.
bool CanAskFirstRunQuestion(const FirstRunQuestionWindow& window);

}  // namespace lb

#endif  // LB_BROWSER_FIRST_RUN_FIRST_RUN_QUESTIONS_H_
