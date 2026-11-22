// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_IMPORTER_IMPORTED_TABS_H_
#define LB_BROWSER_IMPORTER_IMPORTED_TABS_H_

#include <vector>

#include "lb/browser/spaces/space_model.h"
#include "lb/core_bridge/lib.rs.h"

namespace lb {

// The saved tabs an Arc space brings (docs/ux.md §13): its pinned tabs in order, each top-level
// pinned folder a pinned group in place with the folder's name, collapsed and coloured
// `group_color` (a tab_groups::TabGroupColorId); then its unpinned tabs as today tabs. Each group
// gets a new id, so importing again never joins an earlier import's group. Tabs whose URL isn't
// valid are left out.
std::vector<SavedTab> ArcSpaceTabs(const core::ArcSpace& space, int group_color);

// The saved tabs a Chrome-family profile's open tabs bring to its space (docs/ux.md §13): pinned
// ones pinned, each tab group with the source's title and colour, collapsed. Tabs whose URL isn't
// valid are left out.
std::vector<SavedTab> OpenTabsAsSavedTabs(const rust::Vec<core::OpenTab>& open_tabs);

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_IMPORTED_TABS_H_
