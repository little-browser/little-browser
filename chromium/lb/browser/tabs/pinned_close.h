// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_TABS_PINNED_CLOSE_H_
#define LB_BROWSER_TABS_PINNED_CLOSE_H_

namespace lb {

// A tab as seen when something asks to close it.
struct PinnedCloseState {
  bool user_gesture = true;  // the user closed it (not Chromium, an extension or a window close)
  bool closing_all = false;  // the whole window is closing
  bool pinned = true;
  bool split = false;   // in a split view
  bool asleep = false;  // its page isn't loaded
  bool shown = true;    // the window's active tab
  // An earlier close only hid it (kHide), or let its page ask before leaving (kCloseAskingFirst).
  bool hidden_by_earlier_close = false;
  bool sleep_pending = false;  // closed already, and its sleep hasn't run yet
  // Its page has a beforeunload handler. Many pages register one and never ask, so this alone
  // doesn't mean the page holds unsaved input.
  bool asks_before_leaving = false;
  bool had_user_edits = false;  // the user has typed into the page (forms or contenteditable)
};

enum class PinnedCloseOutcome {
  kClose,             // closes as usual
  kCloseAskingFirst,  // closes as usual, the page asks first; its follow-up close isn't stopped
  kSleep,             // stays in place and unloads
  kHide,              // stays in place, loaded: unloading could lose unsaved input
  kAlreadySleeping,   // closed again before its sleep ran: nothing more to do
};

// Whether the page may hold unsaved input: it asks before it is left and the user has typed
// into it (as Chromium's memory saver checks before it discards a tab).
bool MayHoldUnsavedInput(const PinnedCloseState& state);

// What closing a tab does (docs/ux.md §6, Pinned). The user closing an awake pinned tab puts it
// to sleep; one that may hold unsaved input is only hidden, and closing it again, or while it
// isn't shown, really closes it. Anything else closes as usual.
PinnedCloseOutcome DecidePinnedClose(const PinnedCloseState& state);

}  // namespace lb

#endif  // LB_BROWSER_TABS_PINNED_CLOSE_H_
