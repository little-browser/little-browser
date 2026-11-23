// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_PROFILES_SPACE_AVATAR_DEMO_H_
#define LB_BROWSER_UI_VIEWS_PROFILES_SPACE_AVATAR_DEMO_H_

class BrowserWindowInterface;

namespace lb {

// --lb-sidebar-demo=space-avatar: the toolbar's profile button shows the emoji of the window's
// space. It must show the first space's, then the second's after a switch to it, then the new one
// after that space's icon is changed. Then the profile menu opens and stays open
// (--lb-ui-snapshot-dialog=<png> captures it, its header showing the emoji). Logs
// "lb demo: space-avatar PASS ..." or "... FAIL ...".
void RunSpaceAvatarDemo(BrowserWindowInterface* browser);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_PROFILES_SPACE_AVATAR_DEMO_H_
