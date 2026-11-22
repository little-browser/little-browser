// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_SIDEBAR_TAB_SEARCH_DEMO_H_
#define LB_BROWSER_UI_VIEWS_SIDEBAR_TAB_SEARCH_DEMO_H_

class BrowserWindowInterface;

namespace lb {

// --lb-sidebar-demo=tab-search: tab search is always in the strip (patches/lb/
// tab-search-always.patch). With "unpinned" saved, a new window must clear it and show the button;
// the toggle command must leave it pinned; right-clicking the button must open no menu. Logs
// "lb demo: tab-search PASS ..." or "... FAIL ...".
void RunTabSearchDemo(BrowserWindowInterface* browser);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_SIDEBAR_TAB_SEARCH_DEMO_H_
