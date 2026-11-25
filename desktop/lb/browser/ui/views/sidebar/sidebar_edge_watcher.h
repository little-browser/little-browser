// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_EDGE_WATCHER_H_
#define LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_EDGE_WATCHER_H_

#include <memory>

#include "base/functional/callback.h"

namespace lb {

// A hidden sidebar also slides in when the pointer rests just left of the window
// (lb::InSidebarOutsideZone, docs/ux.md §2). Out there the window's views get no pointer events,
// so this runs `on_move` whenever the pointer moves, and the strip (VerticalTabStripRegionView,
// patches/lb/hidden-sidebar-ui.patch) looks at where it is. On macOS it is a pair of event
// monitors for pointer moves (sidebar_edge_watcher_mac.mm), which only observe: clicks there still
// go to whatever is under the pointer, and they need no Accessibility permission. Only macOS
// defines Create; another platform adds its own file when it gets this.
class SidebarEdgeWatcher {
 public:
  static std::unique_ptr<SidebarEdgeWatcher> Create(base::RepeatingClosure on_move);

  SidebarEdgeWatcher(const SidebarEdgeWatcher&) = delete;
  SidebarEdgeWatcher& operator=(const SidebarEdgeWatcher&) = delete;
  virtual ~SidebarEdgeWatcher() = default;

 protected:
  SidebarEdgeWatcher() = default;
};

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_EDGE_WATCHER_H_
