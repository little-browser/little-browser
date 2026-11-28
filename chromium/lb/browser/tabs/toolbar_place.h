// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_TABS_TOOLBAR_PLACE_H_
#define LB_BROWSER_TABS_TOOLBAR_PLACE_H_

namespace lb {

// Where a window's toolbar (back, forward, reload, the address, extensions, downloads and the
// menu) goes (docs/ux.md §2).
enum class ToolbarPlace {
  kSidebar,  // at the top of the sidebar, with the address on a row of its own
  kTopRow,   // Chromium's row above the page
};

// A normal window with the sidebar has its toolbar in the sidebar, and nothing above the page,
// also in fullscreen, where the sidebar stays. Little windows and other popups, and tabs on top,
// keep Chromium's row, and so does a sidebar hidden with expand on hover off (`column`): it is
// Chromium's column of icons then, too narrow for the toolbar.
ToolbarPlace ToolbarPlaceFor(bool normal_window, bool vertical_tabs, bool column);

// Whether the window's buttons (close, minimise, zoom) show. With the toolbar in the sidebar
// there is no row above the page for them: while the sidebar is hidden and hasn't slid in, they
// are hidden, as the sidebar they sit on is. In fullscreen they are macOS's, in the title bar it
// reveals at the top of the screen, and always show.
bool WindowButtonsShown(ToolbarPlace place, bool sidebar_hidden, bool slid_in, bool fullscreen);

// Whether a hidden sidebar stays slid in because a bubble (site info, an extension's popup, the
// menu, a permission prompt) hangs from a toolbar button in it: it must stay where the bubble
// points.
bool KeepsSidebarOutForBubble(ToolbarPlace place, bool sidebar_hidden, bool bubble_from_sidebar);

// In the sidebar the toolbar's row of buttons is the window's title row, as in Arc: the window
// buttons, then the sidebar toggle, back, forward, reload, and at the end Tab Search and the
// menu; the address has the row under it. The row's buttons start after the window buttons:
// how far in from the toolbar's leading edge (`toolbar_x` from the window's leading edge, the
// right in a right-to-left UI), when the window buttons (with their padding) end
// `window_buttons_end` from that edge (0 for none, as in fullscreen).
int ToolbarLeadingInset(int window_buttons_end, int toolbar_x);

}  // namespace lb

#endif  // LB_BROWSER_TABS_TOOLBAR_PLACE_H_
