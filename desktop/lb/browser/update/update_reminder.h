// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UPDATE_UPDATE_REMINDER_H_
#define LB_BROWSER_UPDATE_UPDATE_REMINDER_H_

#include <optional>
#include <string>

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

}  // namespace lb

#endif  // LB_BROWSER_UPDATE_UPDATE_REMINDER_H_
