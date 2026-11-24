// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_EDITS_DEMO_H_
#define LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_EDITS_DEMO_H_

#include <string>

class BrowserWindowInterface;

namespace lb {

// Demo checks for tab names and pinned links (docs/ux.md §6).
// Each logs "lb demo: <mode> PASS ..." or "... FAIL ...".
//
// --lb-sidebar-demo=tab-names: names a tab and checks its row, the tab menu's items, the command
// bar's rename and the session data; pins it and checks its link: a page left from it enables Use
// Current Page as Link and Back to Pinned Page, which go back to it or make the page the link; a
// drag below "+ New Tab" and back keeps the link; asleep (with a new page) it keeps its name and
// link; the space's saved tabs carry both. With --lb-ui-snapshot-sidebar=<png> it captures the
// named rows at the end.
//
// Returns false for any other mode.
bool MaybeRunSidebarEditsDemo(BrowserWindowInterface* browser, const std::string& mode);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_EDITS_DEMO_H_
