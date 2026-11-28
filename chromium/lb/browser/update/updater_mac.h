// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UPDATE_UPDATER_MAC_H_
#define LB_BROWSER_UPDATE_UPDATER_MAC_H_

#include "base/functional/callback_forward.h"

namespace lb {

// Starts the updater (docs/design.md §7.13) if this app was packaged with it: Sparkle in
// Contents/Frameworks and its feed and public key in Info.plist (desktop/scripts/
// package-macos.sh). Development builds have neither, so they never update. Sparkle checks the
// feed every four hours and shows its own update window once the user is in the browser, which is
// kept above the front browser window (lb::PlaceUpdateWindow); it downloads and checks the update
// and restarts the browser into it. `before_update_quit` runs just before that quit, which then
// needs no "Quit Little Browser?" question. Call once, on the UI thread.
void StartUpdater(base::RepeatingClosure before_update_quit);

}  // namespace lb

#endif  // LB_BROWSER_UPDATE_UPDATER_MAC_H_
