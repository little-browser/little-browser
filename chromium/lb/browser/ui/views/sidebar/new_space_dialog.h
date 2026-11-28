// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_SIDEBAR_NEW_SPACE_DIALOG_H_
#define LB_BROWSER_UI_VIEWS_SIDEBAR_NEW_SPACE_DIALOG_H_

namespace lb {

class SidebarController;

// The new-space dialog (docs/ux.md §5): name, profile (an existing one or a new one), colour
// and icon; Cancel or Create. Browser-modal on the controller's window.
void ShowNewSpaceDialog(SidebarController* controller);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_SIDEBAR_NEW_SPACE_DIALOG_H_
