// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_LB_COMMAND_IDS_H_
#define LB_BROWSER_UI_LB_COMMAND_IDS_H_

// Little Browser's own command ids, in gaps of Chromium's (chrome/app/chrome_command_ids.h), so
// no Chromium header has to change. Check they are still free when moving to a new Chromium.

// Cmd+Option+N: the command bar, for a new little window (docs/ux.md section 8). Among the
// window commands (34000...).
#define IDC_NEW_LITTLE_WINDOW 34090

// A link's context menu: "Open Link in Little Window". Among the link items (50100...).
#define IDC_CONTENT_CONTEXT_OPENLINKLITTLEWINDOW 50114

#endif  // LB_BROWSER_UI_LB_COMMAND_IDS_H_
