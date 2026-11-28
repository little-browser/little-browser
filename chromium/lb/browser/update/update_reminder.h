// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UPDATE_UPDATE_REMINDER_H_
#define LB_BROWSER_UPDATE_UPDATE_REMINDER_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/time/time.h"

namespace lb {

// How long "Later" on the update prompt holds a found update back (docs/design.md §7.13). A
// security update is held back for less, so the next check (every four hours) asks again.
inline constexpr base::TimeDelta kUpdateRemindAfter = base::Days(1);
inline constexpr base::TimeDelta kSecurityUpdateRemindAfter = base::Hours(1);

// The last "Later": which version it was for and when.
struct UpdateLater {
  std::string version;
  base::Time when;
};

// What decides whether an update check that found an update shows the prompt.
struct UpdateOfferState {
  // The version found (the feed's bundle version).
  std::string version;
  // The feed marks it critical: a security release, or newer than one.
  bool security = false;
  // The user asked for the check, rather than the schedule.
  bool user_initiated = false;
  std::optional<UpdateLater> later;
  base::Time now;
};

// Whether to show the prompt: always for the user's own check, a version not put off, or once
// the time since "Later" for this version has passed. A clock that went back shows it too, so a
// wrong clock can't hold an update back for good.
bool ShouldOfferUpdate(const UpdateOfferState& state);

// Sparkle shows a scheduled check's update itself once the user is in the browser: right away if
// the browser is in front and was just opened or the computer was idle, otherwise when the
// browser next becomes the active app. It opens its window in front, at the normal level, so the
// click that brings the browser forward can raise the clicked browser window over it. To keep it
// above the browser, each of Sparkle's update windows is a child window of the front browser
// window, which AppKit keeps it above; the browser's own dialogs, bubbles and menus, opened later
// as that window's children or at higher levels, still come above it.

// What PlaceUpdateWindow decides from.
struct UpdateWindowState {
  // The update window is on screen.
  bool visible = false;
  // It is a child window now, and of the front browser window.
  bool has_parent = false;
  bool parent_is_front = false;
  // Its parent is closing or being minimised: a child window would go with it.
  bool parent_going_away = false;
  // Its parent is off screen: closed (AppKit orders a closing window out before telling that it
  // closes) or hidden, which took the update window off screen with it.
  bool parent_hidden = false;
  // It went off screen with its parent before (kShowAgainLater), and hasn't closed or shown
  // since: Sparkle still shows it.
  bool hidden_with_parent = false;
  // There is a front browser window to attach it to: visible, not minimised and on the active
  // Space (attaching to a window on another Space would switch Spaces).
  bool front_usable = false;
};

enum class UpdateWindowMove {
  kKeep,           // leave it where it is
  kAttachToFront,  // make it a child of the front browser window (leaving any other parent)
  kDetach,         // no longer a child window
  // Off screen only because its parent went: a child of the front browser window, and on screen
  // again above it.
  kShowAgainAboveFront,
  // The same with no browser window to show it above yet: no longer a child, and shown again
  // (kShowAgainAboveFront, `hidden_with_parent`) once there is one.
  kShowAgainLater,
};

UpdateWindowMove PlaceUpdateWindow(const UpdateWindowState& state);

// Whether a window, by its window controller's class, is one of Sparkle's update windows: the
// prompt, and the download and install progress.
bool IsUpdateWindow(std::string_view window_controller_class);

}  // namespace lb

#endif  // LB_BROWSER_UPDATE_UPDATE_REMINDER_H_
