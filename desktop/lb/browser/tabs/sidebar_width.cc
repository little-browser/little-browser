// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/sidebar_width.h"

#include <algorithm>
#include <cmath>

namespace lb {

SidebarWidths LayOutSidebar(const ChromiumSidebarWidths& chromium) {
  SidebarWidths widths;
  const int range = chromium.uncollapsed - chromium.collapsed;
  if (!chromium.slides_in) {
    // Nothing would slide a hidden sidebar in: Chromium's own, column of icons and all.
    widths.page_inset = std::max(0, chromium.strip);
    widths.strip = widths.page_inset;
    widths.drawn = true;
    return widths;
  }
  if (chromium.shown || range <= 0) {
    // Shown, or no room between the two widths to map: Chromium's own.
    widths.page_inset = std::max(0, chromium.strip);
  } else if (chromium.strip > chromium.collapsed) {
    // Chromium animates between `collapsed` and `uncollapsed`; ours between none and
    // `uncollapsed`, at the same pace.
    const double shown = static_cast<double>(chromium.strip - chromium.collapsed) / range;
    widths.page_inset = static_cast<int>(std::lround(std::min(shown, 1.0) * chromium.uncollapsed));
  }
  const int base = std::max(kHiddenSidebarEdgeWidth, widths.page_inset);
  const double slide_in = std::clamp(chromium.slide_in, 0.0, 1.0);
  if (widths.page_inset > 0 && slide_in > 0) {
    // Shown while slid in: Chromium slides it back as the page makes room, but it is already at
    // its width, so it stays there.
    widths.strip = std::max(base, chromium.uncollapsed);
  } else {
    widths.strip =
        base + static_cast<int>(std::lround(std::max(0, chromium.uncollapsed - base) * slide_in));
  }
  widths.drawn = widths.page_inset > 0 || slide_in > 0;
  return widths;
}

SidebarResizeArea SidebarResizeAreaFor(bool collapsed,
                                       bool slides_in,
                                       bool slid_in,
                                       bool resizing) {
  return {.visible = !collapsed || !slides_in || slid_in || resizing,
          .wide = !collapsed || slid_in};
}

bool ResizesSlidInSidebar(bool collapsed, bool slid_in) {
  return collapsed && slid_in;
}

int SlidInSidebarWidth(int proposed, int min, int max) {
  return std::clamp(proposed, min, std::max(min, max));
}

}  // namespace lb
