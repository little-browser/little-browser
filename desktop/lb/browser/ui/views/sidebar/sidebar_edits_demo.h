// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_EDITS_DEMO_H_
#define LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_EDITS_DEMO_H_

#include <string>

class BrowserWindowInterface;

namespace lb {

// Demo checks for the hidden sidebar and for tab names and pinned links (docs/ux.md §2, §6).
// Each logs "lb demo: <mode> PASS ..." or "... FAIL ...".
//
// --lb-sidebar-demo=hidden-sidebar: hides the sidebar (the toggle's collapse); it must take only
// the edge band, the page leaving that band, and not be drawn. Then it slides in over the page, as
// the pointer at the edge makes it, to the sidebar's width; its edge is dragged 60 px wider, which
// must widen it in place, keep it hidden, become the sidebar's width and leave its resize area; let
// go, with the pointer leaving it, it slides out again. A stand-in pointer just left of the window
// must slide it in (when the window is active; otherwise the log says it wasn't checked), and
// taking it away must slide it out. With expand on hover off it must be Chromium's column of icons.
// The sidebar is put back as it was. With --lb-ui-snapshot-sidebar=<png> it captures the slid-in
// sidebar after the drag instead.
//
// --lb-sidebar-demo=tab-names: names a tab and checks its row, the tab menu's items, the command
// bar's rename and the session data; pins it and checks its link: on a page left from it, the
// link's title stays and Use Current Page as Link makes the page the link; a drag below "+ New
// Tab" and back keeps the link; put to sleep away from its link it keeps its name and link and
// wakes at its link; the space's saved tabs carry both. With --lb-ui-snapshot-sidebar=<png> it
// captures the named rows at the end.
//
// --lb-sidebar-demo=other-apps: links from other apps (docs/ux.md §8). Links handed over by macOS
// must open the web page in a little window and leave a browser page to open as a tab; a second
// launch's command line with a link must open it in a little window, and one that also has a page
// that opens as a tab (about:blank) must be left to Chromium.
//
// Returns false for any other mode.
bool MaybeRunSidebarEditsDemo(BrowserWindowInterface* browser, const std::string& mode);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_EDITS_DEMO_H_
