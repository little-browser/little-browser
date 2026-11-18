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
// sidebar already pads itself. Top: the toolbar sits on the page.)
inline gfx::Rect ContentsCardBounds(const gfx::Rect& bounds) {
  gfx::Rect card = bounds;
  card.Inset(gfx::Insets::TLBR(0, 0, kContentsCardInset, kContentsCardInset));
  return card;
}

inline gfx::RoundedCornersF ContentsCardRadii() {
  return gfx::RoundedCornersF(0, 0, kContentsCardRadius, kContentsCardRadius);
}

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_FRAME_CONTENTS_CARD_H_
