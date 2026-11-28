// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_SIDEBAR_PINNED_GROUPS_DEMO_H_
#define LB_BROWSER_UI_VIEWS_SIDEBAR_PINNED_GROUPS_DEMO_H_

#include <string>

class BrowserWindowInterface;

namespace lb {

// --lb-sidebar-demo modes for pinned groups (docs/ux.md §6.1), each logging
// "lb demo: <mode> PASS" or "FAIL" (chromium/lb/README.md):
//   pinned-groups          on a fresh profile: pins four pages and groups two with the tab menu's
//                          Add to New Group (a header with its name being edited); renames,
//                          collapses (its tabs hide, the others stay) and expands it; a tab moved
//                          inside joins it and one moved away leaves; Close puts its tabs to
//                          sleep and keeps it, Close again removes it with its tabs; Unpin makes a
//                          today group with the same name and colour and its menu's Pin Group a
//                          pinned one again; it is saved with the space and survives a space
//                          switch. Then with mouse drags: a tab dropped on its header joins it,
//                          one dragged above it leaves (but stays in with Esc); its header dragged
//                          below "+ New Tab", an image of the group following while its tabs fold
//                          away and a line showing where it lands, makes it a today group; that
//                          group's header held above shows a line where it would be pinned and,
//                          dropped, makes it a pinned group again there. A collapsed pinned
//                          group's tabs count as collapsed, and the shown pinned tab's sleep shows
//                          a today tab rather than one of them. Then it quits. With
//                          --lb-ui-snapshot-pinned-drag=<png> it stops mid-drag of the header and
//                          captures the group's drag image.
//   pinned-groups-drops    on a fresh profile: a pinned group holding a split view, dragged by its
//                          header between two today tabs below a collapsed today group, becomes a
//                          today group there, the split whole and the collapsed group untouched;
//                          its header dragged back above the first pinned tab pins it there; its
//                          Close puts every tab to sleep, the split's too. Then Tab > Group Tab
//                          puts a tab in a group of its own, which a drag in place keeps it in
//                          and Group Tab again takes it out of; unpinning and pinning it in place
//                          keeps it out; and the group, collapsed while it holds the shown tab,
//                          unpins expanded.
//   pinned-groups-restart  run again on the profile pinned-groups left: the restored window has
//                          that pinned group back, with its name, colour and tabs (with
//                          --vmodule=pinned_group_tracker=1 the log shows each tab that session
//                          restore brought back in it).
// Returns false for any other mode.
bool MaybeRunPinnedGroupsDemo(BrowserWindowInterface* browser, const std::string& mode);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_SIDEBAR_PINNED_GROUPS_DEMO_H_
