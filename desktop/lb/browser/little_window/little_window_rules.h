// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_RULES_H_
#define LB_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_RULES_H_

#include <optional>
#include <string>
#include <string_view>

#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace lb {

// The little window (docs/ux.md section 8): a small window with no tabs, for one page.

// The size of its page when there is room for it. As for Chromium's popup windows, the size is the
// page's: the title bar and the toolbar row come on top.
inline constexpr gfx::Size kLittleWindowSize(960, 680);

// Where a new little window goes on a screen whose usable area is `work_area`: the window's
// origin and its page's size. The window is its page plus `chrome_height` (the title bar and the
// toolbar row). The page is kLittleWindowSize (smaller if the screen is); the window is centred,
// or, if another little window opened before it (`newest_little`, that window's bounds), a step
// down and to the right of it, while the whole window still fits on the screen.
gfx::Rect LittleWindowBounds(const gfx::Rect& work_area,
                             const std::optional<gfx::Rect>& newest_little,
                             int chrome_height);

// The button that moves the page into a window with tabs: "Open in Work", after the space that
// window shows. Without a space name (no window with tabs yet): "Open in a tab".
std::u16string OpenInSpaceLabel(std::string_view space_name);

}  // namespace lb

#endif  // LB_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_RULES_H_
