// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_TABS_SIDEBAR_WIDTH_H_
#define LB_BROWSER_TABS_SIDEBAR_WIDTH_H_

namespace lb {

// A hidden sidebar (docs/ux.md §2) is Chromium's collapsed vertical strip, which Chromium keeps
// as a column of icons beside the page. Ours takes no room: the page fills the window, and the
// strip is an invisible band this wide at the window's left edge, where the pointer slides it in
// (Chromium's expand on hover). With expand on hover turned off nothing would slide it in, so it
// stays Chromium's column of icons.
inline constexpr int kHiddenSidebarEdgeWidth = 6;

// Where Chromium's layout has the strip, as it would draw it.
struct ChromiumSidebarWidths {
  // The strip's width beside the page: `collapsed` when hidden, `uncollapsed` when shown, in
  // between while it animates from one to the other (the sidebar toggle).
  int strip = 0;
  // Chromium's width of a collapsed strip, and the width the user gave the shown sidebar.
  int collapsed = 0;
  int uncollapsed = 0;
  // How far a hidden sidebar has slid in over the page, 0 to 1 (Chromium's expand on hover).
  double slide_in = 0;
  // Chromium shows the strip and isn't animating it: its width is the strip's, also when a
  // narrow window gives it less than `uncollapsed`.
  bool shown = false;
  // Expand on hover is on, so a hidden sidebar can slide in.
  bool slides_in = true;
};

// Where Little Browser lays the strip out instead.
struct SidebarWidths {
  // How much of the window's width beside the page the strip takes: none while hidden.
  int page_inset = 0;
  // The strip's own width: the page inset, at least the edge band, and slid in over the page to
  // the user's width.
  int strip = 0;
  // Whether the strip is drawn: not while it is only the invisible edge band.
  bool drawn = false;
};

// The strip's widths as Little Browser lays them out. Hiding and showing keep Chromium's
// animation, mapped so that the page takes the whole window once the strip is hidden; sliding in
// goes to the width the user gave the sidebar, not Chromium's default. Showing a sidebar that is
// slid in keeps it at that width while the page makes room beside it.
SidebarWidths LayOutSidebar(const ChromiumSidebarWidths& chromium);

// The strip's resize area: whether it shows, and whether it is the full width (or Chromium's
// narrow one on a collapsed strip). It shows on a shown sidebar, on a slid-in one, on Chromium's
// column of icons (expand on hover off) and during a drag; `slid_in` is Chromium's expand on
// hover state.
struct SidebarResizeArea {
  bool visible = false;
  bool wide = false;
  bool operator==(const SidebarResizeArea&) const = default;
};
SidebarResizeArea SidebarResizeAreaFor(bool collapsed, bool slides_in, bool slid_in, bool resizing);

// Whether a drag of the strip's edge that starts now resizes a slid-in hidden sidebar (in place,
// staying hidden) rather than the shown one. Decided once, when the drag starts: the strip can
// collapse or slide in under the pointer while it goes on.
bool ResizesSlidInSidebar(bool collapsed, bool slid_in);

// The width a sidebar slid in over the page takes when the user drags its edge to `proposed`:
// between Chromium's `min` and `max`. Unlike the shown sidebar, it never snaps to hidden (it
// already is), so a drag to the left stops at `min`.
int SlidInSidebarWidth(int proposed, int min, int max);

}  // namespace lb

#endif  // LB_BROWSER_TABS_SIDEBAR_WIDTH_H_
