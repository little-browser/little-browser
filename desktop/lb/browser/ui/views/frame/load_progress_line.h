// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_FRAME_LOAD_PROGRESS_LINE_H_
#define LB_BROWSER_UI_VIEWS_FRAME_LOAD_PROGRESS_LINE_H_

#include <optional>

#include "lb/browser/tabs/load_progress.h"
#include "ui/gfx/geometry/rect.h"

class BrowserWindowInterface;

namespace views {
class View;
}  // namespace views

namespace lb {

// The load progress line (docs/ux.md §2, rules in lb/browser/tabs/load_progress.h): along the
// toolbar's bottom edge, as wide as the page card under it, in the window's colour (the space's
// tint). It follows the shown tab's load, also with the sidebar hidden and in little windows. It
// sits in the toolbar's parent (the window's top container), not over the page, which on macOS
// could cover it; the sidebar slid in over the page covers it.

// Adds it to `toolbar` of a normal or little window (patches/lb/load-progress-ui.patch).
void AddLoadProgressLine(views::View* toolbar, BrowserWindowInterface* browser);

// For --lb-sidebar-demo=load-progress: what the line shows, where (in screen coordinates) and how
// opaque, or nullopt if the window has none.
struct LoadProgressLineShown {
  LoadProgressLine line;
  gfx::Rect bounds;
  gfx::Rect filled;  // the drawn part
  float opacity = 0;
};
std::optional<LoadProgressLineShown> GetLoadProgressLineForTesting(BrowserWindowInterface* browser);
// Shows `load` as the shown page's, with no animation, until the page's own load changes.
void SetLoadProgressForTesting(BrowserWindowInterface* browser, const PageLoad& load);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_FRAME_LOAD_PROGRESS_LINE_H_
