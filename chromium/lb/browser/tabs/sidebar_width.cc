// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/sidebar_width.h"

#include <algorithm>
#include <cmath>

#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace lb {

bool InSidebarOutsideZone(const gfx::Point& pointer,
                          const gfx::Rect& window,
                          const gfx::Rect& display) {
  if (window.IsEmpty()) {
    return false;
  }
  gfx::Rect zone(window.x() - kSidebarOutsideReach, window.y(), kSidebarOutsideReach,
                 window.height());
  zone.Intersect(display);
  return zone.Contains(pointer);
}

SidebarWidths LayOutSidebar(const ChromiumSidebarWidths& chromium) {
  SidebarWidths widths;
  const int range = chromium.uncollapsed - chromium.collapsed;
  // Hidden, the page leaves the edge band beside it only as a card, whose margins it matches; a
  // page without one (fullscreen) takes the whole width, the band invisible over it.
  const int hidden_inset = chromium.page_is_card ? kHiddenSidebarEdgeWidth : 0;
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
  } else {
    // Chromium animates between `collapsed` and `uncollapsed`; ours between the page's inset when
    // hidden and `uncollapsed`, at the same pace.
    const double shown =
        std::clamp(static_cast<double>(chromium.strip - chromium.collapsed) / range, 0.0, 1.0);
    widths.page_inset =
        hidden_inset +
        static_cast<int>(std::lround(shown * std::max(0, chromium.uncollapsed - hidden_inset)));
  }
  const bool beside_page = widths.page_inset > hidden_inset;
  const int base = std::max(kHiddenSidebarEdgeWidth, widths.page_inset);
  const double slide_in = std::clamp(chromium.slide_in, 0.0, 1.0);
  if (beside_page && slide_in > 0) {
    // Shown while slid in: Chromium slides it back as the page makes room, but it is already at
    // its width, so it stays there.
    widths.strip = std::max(base, chromium.uncollapsed);
  } else {
    widths.strip =
        base + static_cast<int>(std::lround(std::max(0, chromium.uncollapsed - base) * slide_in));
  }
  widths.drawn = beside_page || slide_in > 0;
  widths.full_height = !beside_page || slide_in > 0;
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
