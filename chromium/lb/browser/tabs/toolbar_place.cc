// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/toolbar_place.h"

namespace lb {

ToolbarPlace ToolbarPlaceFor(bool normal_window, bool vertical_tabs, bool column) {
  return normal_window && vertical_tabs && !column ? ToolbarPlace::kSidebar : ToolbarPlace::kTopRow;
}

bool WindowButtonsShown(ToolbarPlace place, bool sidebar_hidden, bool slid_in, bool fullscreen) {
  return place == ToolbarPlace::kTopRow || fullscreen || !sidebar_hidden || slid_in;
}

bool KeepsSidebarOutForBubble(ToolbarPlace place, bool sidebar_hidden, bool bubble_from_sidebar) {
  return place == ToolbarPlace::kSidebar && sidebar_hidden && bubble_from_sidebar;
}

int ToolbarLeadingInset(int window_buttons_end, int toolbar_x) {
  return window_buttons_end > toolbar_x ? window_buttons_end - toolbar_x : 0;
}

}  // namespace lb
