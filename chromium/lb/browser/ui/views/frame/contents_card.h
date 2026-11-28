// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_FRAME_CONTENTS_CARD_H_
#define LB_BROWSER_UI_VIEWS_FRAME_CONTENTS_CARD_H_

#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"

namespace lb {

// The page as a card next to the sidebar (docs/ux.md §2: inset 8 px, rounded corners).
// Used by Chromium's tabbed browser layout (desktop/patches/lb/contents-card.patch) when
// vertical tabs are shown and the window is not fullscreen.
inline constexpr int kContentsCardInset = 8;
inline constexpr float kContentsCardRadius = 10.0f;

// Leaves the window's background visible to the right of and below the page. (Left: the
// sidebar already pads itself.) Above it too when the toolbar is in the sidebar (`toolbar_above`
// false, lb::ToolbarPlace::kSidebar): nothing else is there. Otherwise the toolbar sits on it.
inline gfx::Rect ContentsCardBounds(const gfx::Rect& bounds, bool toolbar_above = true) {
  gfx::Rect card = bounds;
  card.Inset(gfx::Insets::TLBR(toolbar_above ? 0 : kContentsCardInset, 0, kContentsCardInset,
                               kContentsCardInset));
  return card;
}

// Its rounded corners: the bottom ones, and the top ones too with no toolbar on it.
inline gfx::RoundedCornersF ContentsCardRadii(bool toolbar_above = true) {
  const float top = toolbar_above ? 0 : kContentsCardRadius;
  return gfx::RoundedCornersF(top, top, kContentsCardRadius, kContentsCardRadius);
}

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_FRAME_CONTENTS_CARD_H_
