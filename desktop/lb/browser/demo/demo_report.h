// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_DEMO_DEMO_REPORT_H_
#define LB_BROWSER_DEMO_DEMO_REPORT_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/time/time.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/view_utils.h"

class BrowserWindowInterface;
class Profile;
class TabView;

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace views {
class View;
}  // namespace views

namespace lb {

// Helpers for --lb-sidebar-demo modes that check behaviour in a running browser
// (desktop/lb/README.md).

// Logs "lb demo: <mode> PASS <detail>" or "... FAIL ...", once per mode: whichever comes second
// of a result and its deadline is dropped. Logged without --vmodule.
void ReportDemo(const std::string& mode, bool pass, const std::string& detail);

// Reports `mode` as failed if it has no result after `limit`.
void FailDemoAfter(const std::string& mode, base::TimeDelta limit);

// Runs `task` on the UI thread after `delay`.
void PostDemoTask(base::OnceClosure task, base::TimeDelta delay);

// Counts the first-run questions (the import offer, the search engine and password manager
// choices) as answered, so they don't open over the window first (and take the
// --lb-ui-snapshot-dialog snapshot). Call when the window opens, before they are asked.
void SkipFirstRunQuestionsForDemo(Profile* profile);

// Clicks `view` with the left mouse button, as the mouse does (press, then release), at `point` in
// its own coordinates, or its centre.
void ClickViewForDemo(views::View* view, std::optional<gfx::Point> point = std::nullopt);

// `root` and every view under it.
std::vector<views::View*> ViewTreeForDemo(views::View* root);
// The views of class `T` in `root`'s tree.
template <typename T>
std::vector<T*> FindViewsForDemo(views::View* root) {
  std::vector<T*> found;
  for (views::View* view : ViewTreeForDemo(root)) {
    if (auto* match = views::AsViewClass<T>(view)) {
      found.push_back(match);
    }
  }
  return found;
}
// `browser`'s window as a view tree (its BrowserView), or null.
views::View* WindowViewForDemo(BrowserWindowInterface* browser);

// The sidebar row of `tab`, or null.
TabView* FindTabViewForDemo(BrowserWindowInterface* browser, const tabs::TabInterface* tab);

// Drags `tab`'s row the way the mouse does, through the screen y coordinates `screen_ys` at the
// row's middle x, all within one task (so fast, as a quick real drag), then drops it (`drop`) or
// cancels it as Esc does.
void SimulateTabDragForDemo(BrowserWindowInterface* browser,
                            tabs::TabInterface* tab,
                            const std::vector<int>& screen_ys,
                            bool drop);

// The two halves of that, for a check while the drag is under way: presses `view` and drags it
// through `screen_ys` (false if the view went on the way), then ends the strip's drag.
bool StartViewDragForDemo(views::View* view, const std::vector<int>& screen_ys);
void EndTabDragForDemo(BrowserWindowInterface* browser, bool drop);

// The same with any view that starts a drag of the tab strip, such as a tab group's header.
void SimulateViewDragForDemo(BrowserWindowInterface* browser,
                             views::View* view,
                             const std::vector<int>& screen_ys,
                             bool drop);

// Screen y coordinates from `from` to `to` in a few steps, like a quick mouse move.
std::vector<int> DragPathForDemo(int from, int to);

}  // namespace lb

#endif  // LB_BROWSER_DEMO_DEMO_REPORT_H_
