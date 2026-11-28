// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_TOOLBAR_H_
#define LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_TOOLBAR_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/events/event_handler.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

class BrowserView;
class LocationBarView;

namespace ui {
class LocatedEvent;
}  // namespace ui

namespace views {
class View;
class Widget;
}  // namespace views

namespace lb {

// The toolbar in the sidebar (docs/ux.md §2; lb::ToolbarPlaceFor): a normal window with the sidebar
// has Chromium's whole ToolbarView at the top of the sidebar, its row of buttons beside the window
// buttons with the strip's sidebar toggle and Tab Search lent to it, and nothing above the page. The view itself moves, so everything that points at it (bubbles hanging
// from its buttons, the menu, extensions) keeps working. Its address field gets a row of its own
// under the buttons (sidebar-toolbar-ui.patch lays it out with these helpers), and a click on it
// opens the command bar on the page's address, as ⌘L does.

// Where the toolbar goes in the sidebar: added to the sidebar's content area (AddSidebar).
std::unique_ptr<views::View> CreateSidebarToolbarSlot();

// Puts `browser_view`'s toolbar where lb::ToolbarPlaceFor says, now or (Soon) once the current
// event is done: never during a layout. Hooks: the sidebar's creation, the strip's mode
// (ToolbarView::OnVerticalTabStripModeChanged), and its collapse and expand-on-hover state
// (UpdateSidebarWindowButtons).
void PlaceToolbar(BrowserView* browser_view);
void PlaceToolbarSoon(BrowserView* browser_view);

// Whether `toolbar` (Chromium's ToolbarView) is in the sidebar.
bool IsToolbarInSidebar(const views::View* toolbar);

// The toolbar's row of buttons in the sidebar is the window's title row (lb::ToolbarLeadingInset):
// how far its buttons start in from its leading edge, after the window buttons; and whether a
// press at `point` in `view` (the sidebar's child that holds the toolbar) is on the row but on
// none of its controls, so drags the window. `view` is the sidebar's child the toolbar is in.
int SidebarToolbarLeadingInset(views::View* toolbar);
bool IsSidebarToolbarCaption(views::View* view, const gfx::Point& point);
// The gap between the row's leading and trailing buttons, in the widget's root view (empty if
// `toolbar` isn't in the sidebar). For --lb-sidebar-demo=sidebar-toolbar.
gfx::Rect SidebarToolbarGapForTesting(views::View* toolbar);

// The address row under the buttons in the sidebar: the height it adds to the toolbar's bottom
// margin, and the address field's bounds in a toolbar of `toolbar_size`.
int SidebarAddressRowHeight(views::View* location_bar);
void LayOutSidebarAddressRow(views::View* toolbar, views::View* location_bar);

// A hidden sidebar (hidden-sidebar-ui.patch) with the toolbar in it: `region` is the sidebar,
// `sidebar_hidden` whether it is hidden (collapsed, sliding in), `slid_in` whether it is out over
// the page. The window's buttons show only while the sidebar does (lb::WindowButtonsShown); the
// state is kept, so moving the toolbar applies it again. Also moves the toolbar above the page
// while the sidebar is Chromium's column of icons (expand on hover off), and back.
void UpdateSidebarWindowButtons(views::View* region, bool sidebar_hidden, bool slid_in);
// Shows or hides `widget`'s window buttons (close, minimise, zoom). macOS: sidebar_toolbar_mac.mm.
void SetWindowButtonsShown(views::Widget* widget, bool shown);
// Whether they show. For --lb-sidebar-demo=sidebar-toolbar.
bool WindowButtonsShownForTesting(views::Widget* widget);
// Where `widget`'s close button is, in the widget's root view (empty if it has none). For
// --lb-sidebar-demo=peek, which checks a little window's buttons share its one row.
gfx::Rect CloseButtonBoundsForTesting(views::Widget* widget);

// Whether a hidden sidebar stays slid in because a popup hangs from it: a bubble anchored in
// `region` (site info, an extension's popup, a permission prompt, downloads) or a menu (the ⋯ menu,
// a context menu) that is open (lb::KeepsSidebarOutForBubble).
bool KeepsSidebarOutForPopup(views::View* region, bool sidebar_hidden);

// A click on an address field (`location_bar`'s omnibox) that runs `on_click` instead of
// selecting its text. It watches the location bar and takes only events on its field, so the site
// icon still shows the site's info. Shared with the little window's address.
class AddressClick : public ui::EventHandler {
 public:
  explicit AddressClick(base::RepeatingClosure on_click);
  ~AddressClick() override;

  void set_location_bar(LocationBarView* bar) { location_bar_ = bar; }

  // ui::EventHandler:
  void OnMouseEvent(ui::MouseEvent* event) override;
  void OnGestureEvent(ui::GestureEvent* event) override;

 private:
  bool OnAddress(const ui::LocatedEvent& event) const;

  base::RepeatingClosure on_click_;
  raw_ptr<LocationBarView> location_bar_ = nullptr;
};

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_TOOLBAR_H_
