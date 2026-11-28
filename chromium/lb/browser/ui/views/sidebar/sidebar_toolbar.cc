// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/sidebar/sidebar_toolbar.h"

#include <algorithm>
#include <utility>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/numerics/safe_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/custom_corners_background.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/browser/ui/views/tabs/hovercard/tab_hover_card_bubble_view.h"
#include "chrome/browser/ui/views/tabs/shared/tab_strip_combo_button.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_top_container.h"
#include "chrome/browser/ui/views/profiles/avatar_toolbar_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "lb/browser/tabs/toolbar_place.h"
#include "lb/browser/ui/views/frame/load_progress_line.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "ui/base/class_property.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"

namespace lb {

// One per toolbar in the sidebar: a click on its address opens the command bar on it, and a
// spacer pushes the trailing buttons to the row's end while the address has a row of its own.
// The strip's own top row lends the toolbar its sidebar toggle and its Tab Search button, which
// go back there when the toolbar leaves.
struct SidebarAddress {
  std::unique_ptr<AddressClick> click;
  raw_ptr<LocationBarView> bar = nullptr;
  raw_ptr<views::View> spacer = nullptr;
  raw_ptr<views::View> top_row = nullptr;
  raw_ptr<views::View> toggle = nullptr;
  raw_ptr<TabStripComboButton> tab_search = nullptr;
  size_t toggle_index = 0;
  size_t tab_search_index = 0;
  // Fullscreen takes the window buttons out of the row, and puts them back.
  base::CallbackListSubscription fullscreen_changed;
};

// A window's hidden sidebar state, for its window buttons (UpdateSidebarWindowButtons).
struct SidebarButtonsState {
  bool hidden = false;
  bool slid_in = false;
};

}  // namespace lb

DEFINE_UI_CLASS_PROPERTY_TYPE(lb::SidebarAddress*)
DEFINE_UI_CLASS_PROPERTY_TYPE(lb::SidebarButtonsState*)

namespace lb {

namespace {

DEFINE_UI_CLASS_PROPERTY_KEY(bool, kToolbarInSidebarKey, false)

// The address row: a gap above the field and below it.
constexpr int kAddressRowGap = 6;

class SidebarToolbarSlot : public views::View {
  METADATA_HEADER(SidebarToolbarSlot, views::View)

 public:
  SidebarToolbarSlot() { SetLayoutManager(std::make_unique<views::FillLayout>()); }
};

BEGIN_METADATA(SidebarToolbarSlot)
END_METADATA

SidebarToolbarSlot* FindSlot(views::View* view) {
  if (auto* slot = views::AsViewClass<SidebarToolbarSlot>(view)) {
    return slot;
  }
  for (views::View* child : view->children()) {
    if (SidebarToolbarSlot* slot = FindSlot(child)) {
      return slot;
    }
  }
  return nullptr;
}

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(SidebarAddress, kSidebarAddressKey)
DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(SidebarButtonsState, kSidebarButtonsKey)

BrowserView* BrowserViewOf(views::View* view) {
  for (; view; view = view->parent()) {
    if (auto* browser_view = views::AsViewClass<BrowserView>(view)) {
      return browser_view;
    }
  }
  return nullptr;
}

VerticalTabStripRegionView* RegionOf(views::View* view) {
  for (; view; view = view->parent()) {
    if (auto* region = views::AsViewClass<VerticalTabStripRegionView>(view)) {
      return region;
    }
  }
  return nullptr;
}

// Lends the strip's top row's toggle and Tab Search to the toolbar's row of buttons: the toggle
// first, Tab Search at the start of the trailing buttons (after the spacer).
void BorrowTopRowButtons(ToolbarView* toolbar, views::View* slot, SidebarAddress* address) {
  VerticalTabStripRegionView* region = RegionOf(slot);
  VerticalTabStripTopContainer* top = region ? region->GetTopContainer() : nullptr;
  if (!top || !address->spacer) {
    return;
  }
  address->top_row = top;
  if (views::View* toggle = top->GetCollapseButton()) {
    address->toggle_index = top->GetIndexOf(toggle).value();
    address->toggle = toolbar->AddChildViewAt(toggle, 0);
  }
  if (TabStripComboButton* tab_search = top->GetComboButton()) {
    address->tab_search_index = top->GetIndexOf(tab_search).value();
    address->tab_search = toolbar->AddChildViewAt(
        tab_search, toolbar->GetIndexOf(address->spacer).value() + 1);
    tab_search->SetOrientation(views::LayoutOrientation::kHorizontal);
  }
  top->InvalidateLayout();
}

void ReturnTopRowButtons(SidebarAddress* address) {
  if (!address->top_row) {
    return;
  }
  // In the reverse of the order they left in (Tab Search's index was taken after the toggle had
  // gone), so each goes back to its own place.
  if (address->tab_search) {
    address->top_row->AddChildViewAt(
        address->tab_search.get(),
        std::min(address->tab_search_index, address->top_row->children().size()));
  }
  if (address->toggle) {
    address->top_row->AddChildViewAt(
        address->toggle.get(), std::min(address->toggle_index, address->top_row->children().size()));
  }
  address->top_row->InvalidateLayout();
  address->toggle = nullptr;
  address->tab_search = nullptr;
  address->top_row = nullptr;
}

ToolbarPlace PlaceOf(BrowserView* browser_view) {
  return browser_view && IsToolbarInSidebar(browser_view->toolbar()) ? ToolbarPlace::kSidebar
                                                                     : ToolbarPlace::kTopRow;
}

void ApplyWindowButtons(BrowserView* browser_view) {
  if (!browser_view || !browser_view->GetWidget()) {
    return;
  }
  const SidebarButtonsState* state = browser_view->GetProperty(kSidebarButtonsKey);
  SetWindowButtonsShown(browser_view->GetWidget(),
                        WindowButtonsShown(PlaceOf(browser_view), state && state->hidden,
                                           state && state->slid_in, browser_view->IsFullscreen()));
}

// The toolbar's place for the window as it is now (ToolbarPlaceFor).
ToolbarPlace WantedPlace(BrowserView* browser_view) {
  auto* state = tabs::VerticalTabStripStateController::From(browser_view->browser());
  return ToolbarPlaceFor(
      browser_view->browser()->GetType() == BrowserWindowInterface::TYPE_NORMAL,
      browser_view->ShouldDrawVerticalTabStrip(),
      state && state->IsCollapsed() && !state->IsExpandOnHoverEnabled());
}

// Entering or leaving fullscreen: the row's inset for the window buttons is measured again in
// the toolbar's layout, and the buttons show as they should there.
void OnFullscreenChanged(base::WeakPtr<BrowserView> browser_view) {
  if (!browser_view || !IsToolbarInSidebar(browser_view->toolbar())) {
    return;
  }
  browser_view->toolbar()->InvalidateLayout();
  ApplyWindowButtons(browser_view.get());
}

void EditAddress(base::WeakPtr<BrowserView> browser_view) {
  if (!browser_view) {
    return;
  }
  if (SidebarController* controller = SidebarController::FromBrowser(browser_view->browser())) {
    controller->ShowCommandBar(CommandBarMode::kEditAddress);
  }
}

void MoveIntoSidebar(BrowserView* browser_view, ToolbarView* toolbar, views::View* slot) {
  slot->AddChildView(toolbar);
  toolbar->SetProperty(kToolbarInSidebarKey, true);
  if (auto* background = toolbar->background()
                             ? toolbar->background()->AsA<CustomCornersBackground>()
                             : nullptr) {
    background->SetVisible(false);  // the sidebar's own colour shows
  }
  if (LocationBarView* bar = toolbar->location_bar_view()) {
    bar->SetProperty(views::kViewIgnoredByLayoutKey, true);  // its own row (LayOutSidebarAddressRow)
    auto spacer = std::make_unique<views::View>();
    spacer->SetProperty(views::kFlexBehaviorKey,
                        views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                                                 views::MaximumFlexSizeRule::kUnbounded));
    auto* address = toolbar->SetProperty(kSidebarAddressKey, std::make_unique<SidebarAddress>());
    address->spacer =
        toolbar->AddChildViewAt(std::move(spacer), toolbar->GetIndexOf(bar).value());
    address->click = std::make_unique<AddressClick>(
        base::BindRepeating(&EditAddress, browser_view->GetAsWeakPtr()));
    address->click->set_location_bar(bar);
    address->bar = bar;
    bar->AddPreTargetHandler(address->click.get());
    BorrowTopRowButtons(toolbar, slot, address);
    if (auto* access = ExclusiveAccessManager::From(browser_view->browser())) {
      address->fullscreen_changed =
          access->fullscreen_controller()->RegisterOnFullscreenStateChanged(
              base::BindRepeating(&OnFullscreenChanged, browser_view->GetAsWeakPtr()));
    }
  }
}

void MoveIntoTopRow(BrowserView* browser_view, ToolbarView* toolbar) {
  if (SidebarAddress* address = toolbar->GetProperty(kSidebarAddressKey)) {
    ReturnTopRowButtons(address);
    if (address->bar) {
      address->bar->RemovePreTargetHandler(address->click.get());
    }
    if (address->spacer) {
      toolbar->RemoveChildViewT(address->spacer.get());
    }
    toolbar->ClearProperty(kSidebarAddressKey);
  }
  if (LocationBarView* bar = toolbar->location_bar_view()) {
    bar->SetProperty(views::kViewIgnoredByLayoutKey, false);
  }
  if (auto* background = toolbar->background()
                             ? toolbar->background()->AsA<CustomCornersBackground>()
                             : nullptr) {
    background->SetVisible(true);
  }
  toolbar->SetProperty(kToolbarInSidebarKey, false);
  // Chromium's place for it: the top container's first child, above the bookmarks bar.
  browser_view->top_container()->AddChildViewAt(toolbar, 0);
}

}  // namespace

std::unique_ptr<views::View> CreateSidebarToolbarSlot() {
  return std::make_unique<SidebarToolbarSlot>();
}

void PlaceToolbar(BrowserView* browser_view) {
  ToolbarView* toolbar = browser_view ? browser_view->toolbar() : nullptr;
  if (!toolbar) {
    return;
  }
  const ToolbarPlace place = WantedPlace(browser_view);
  // No profile button: the space's icon is in the footer and beside its name, and profiles are
  // in the ⋮ menu's Profile submenu.
  if (views::View* avatar = toolbar->avatar_toolbar_button()) {
    avatar->SetVisible(false);
  }
  SidebarToolbarSlot* slot = FindSlot(browser_view);
  const bool in_sidebar = IsToolbarInSidebar(toolbar);
  if (place == ToolbarPlace::kSidebar && slot && !in_sidebar) {
    MoveIntoSidebar(browser_view, toolbar, slot);
  } else if ((place == ToolbarPlace::kTopRow || !slot) && in_sidebar) {
    MoveIntoTopRow(browser_view, toolbar);
  } else {
    return;
  }
  MoveLoadProgressLine(browser_view, toolbar, IsToolbarInSidebar(toolbar));
  ApplyWindowButtons(browser_view);
  // Now, not at some later layout: the toolbar's margins (its address row, its inset for the
  // window buttons) are set in its own layout, and its height in the sidebar is the one it asks
  // for with them; then the window's, and the macOS title bar is measured again from the
  // toolbar's row of buttons (BrowserView::GetFrameElementInfo), so the window buttons sit on it.
  toolbar->InvalidateLayout();
  toolbar->DeprecatedLayoutImmediately();
  browser_view->InvalidateLayout();
  browser_view->DeprecatedLayoutImmediately();
  if (browser_view->browser_widget() && browser_view->browser_widget()->GetFrameView()) {
    browser_view->browser_widget()->GetFrameView()->OnTabStripStateChanged();
  }
}

void PlaceToolbarSoon(BrowserView* browser_view) {
  if (!browser_view) {
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<BrowserView> view) {
                       if (view) {
                         PlaceToolbar(view.get());
                       }
                     },
                     browser_view->GetAsWeakPtr()));
}

bool IsToolbarInSidebar(const views::View* toolbar) {
  return toolbar && toolbar->GetProperty(kToolbarInSidebarKey);
}

int SidebarAddressRowHeight(views::View* location_bar) {
  return location_bar ? location_bar->GetPreferredSize().height() + kAddressRowGap : 0;
}

void LayOutSidebarAddressRow(views::View* toolbar, views::View* location_bar) {
  if (!toolbar || !location_bar) {
    return;
  }
  const int height = location_bar->GetPreferredSize().height();
  // In line with the sidebar's rows.
  const int side = GetLayoutConstant(LayoutConstant::kVerticalTabStripHorizontalPadding);
  location_bar->SetBounds(side, toolbar->height() - height - kAddressRowGap,
                          std::max(0, toolbar->width() - 2 * side), height);
}

int SidebarToolbarLeadingInset(views::View* toolbar) {
  BrowserView* browser_view = BrowserViewOf(toolbar);
  if (!IsToolbarInSidebar(toolbar) || !browser_view || !toolbar->GetWidget() ||
      !browser_view->browser_widget() || !browser_view->browser_widget()->GetFrameView()) {
    return 0;
  }
  const BrowserLayoutParams params =
      browser_view->browser_widget()->GetFrameView()->GetBrowserLayoutParams();
  // The toolbar's distance from the window's leading edge, as laid out: views' bounds are
  // logical (from the leading edge, which in a right-to-left UI is the right, where the window
  // buttons are then too), and leave out layer transforms such as a space switch's slide.
  int toolbar_leading = 0;
  for (const views::View* view = toolbar; view; view = view->parent()) {
    toolbar_leading += view->x();
  }
  return ToolbarLeadingInset(
      base::ClampCeil(params.leading_exclusion.ContentWithPadding().width()), toolbar_leading);
}

bool IsSidebarToolbarCaption(views::View* view, const gfx::Point& point) {
  SidebarToolbarSlot* slot = view ? FindSlot(view) : nullptr;
  views::View* toolbar = slot && !slot->children().empty() ? slot->children().front() : nullptr;
  SidebarAddress* address = toolbar ? toolbar->GetProperty(kSidebarAddressKey) : nullptr;
  if (!IsToolbarInSidebar(toolbar) || !address) {
    return false;
  }
  const gfx::Point in_toolbar = views::View::ConvertPointToTarget(view, toolbar, point);
  if (!toolbar->GetLocalBounds().Contains(in_toolbar)) {
    return false;
  }
  for (views::View* child : toolbar->children()) {
    // Mirrored bounds: where the child is drawn, also in a right-to-left UI.
    if (child->GetVisible() && child->GetMirroredBounds().Contains(in_toolbar)) {
      return child == address->spacer;  // the gap between the leading and trailing buttons
    }
  }
  return true;  // padding, and the window buttons' space
}

gfx::Rect SidebarToolbarGapForTesting(views::View* toolbar) {
  SidebarAddress* address = toolbar ? toolbar->GetProperty(kSidebarAddressKey) : nullptr;
  return address && address->spacer
             ? address->spacer->ConvertRectToWidget(address->spacer->GetLocalBounds())
             : gfx::Rect();
}

void UpdateSidebarWindowButtons(views::View* region, bool sidebar_hidden, bool slid_in) {
  BrowserView* browser_view = BrowserViewOf(region);
  if (!browser_view) {
    return;
  }
  browser_view->SetProperty(kSidebarButtonsKey,
                            std::make_unique<SidebarButtonsState>(sidebar_hidden, slid_in));
  ApplyWindowButtons(browser_view);
  // Hidden with expand on hover off, the sidebar is Chromium's column of icons, too narrow for
  // the toolbar: it goes above the page, and comes back when the sidebar does.
  if (WantedPlace(browser_view) != PlaceOf(browser_view)) {
    PlaceToolbarSoon(browser_view);
  }
}

bool KeepsSidebarOutForPopup(views::View* region, bool sidebar_hidden) {
  BrowserView* browser_view = BrowserViewOf(region);
  views::Widget* widget = region ? region->GetWidget() : nullptr;
  if (!browser_view || !widget || !widget->GetNativeView()) {
    return false;
  }
  bool popup = views::MenuController::GetActiveInstance() != nullptr;
  for (views::Widget* child : views::Widget::GetAllChildWidgets(widget->GetNativeView())) {
    views::BubbleDialogDelegate* bubble =
        child->IsVisible() && child->widget_delegate()
            ? child->widget_delegate()->AsBubbleDialogDelegate()
            : nullptr;
    // A tab's hover card is no popup: it goes as the pointer leaves the tab.
    const bool hover_card =
        bubble && views::IsViewClass<TabHoverCardBubbleView>(
                      child->widget_delegate()->GetContentsView());
    popup = popup || (bubble && !hover_card && bubble->GetAnchorView() &&
                      region->Contains(bubble->GetAnchorView()));
  }
  return KeepsSidebarOutForBubble(PlaceOf(browser_view), sidebar_hidden, popup);
}

AddressClick::AddressClick(base::RepeatingClosure on_click) : on_click_(std::move(on_click)) {}
AddressClick::~AddressClick() = default;

void AddressClick::OnMouseEvent(ui::MouseEvent* event) {
  if (!event->IsLeftMouseButton() || !OnAddress(*event)) {
    return;
  }
  if (event->type() == ui::EventType::kMousePressed) {
    event->SetHandled();  // no text selection, no drag
  } else if (event->type() == ui::EventType::kMouseReleased) {
    event->SetHandled();
    on_click_.Run();
  }
}

void AddressClick::OnGestureEvent(ui::GestureEvent* event) {
  if (event->type() == ui::EventType::kGestureTap && OnAddress(*event)) {
    event->SetHandled();
    on_click_.Run();
  }
}

bool AddressClick::OnAddress(const ui::LocatedEvent& event) const {
  auto* target = static_cast<views::View*>(event.target());
  views::View* address = location_bar_ ? location_bar_->omnibox_view() : nullptr;
  if (!target || !address || !address->GetVisible()) {
    return false;
  }
  gfx::Point point = event.location();
  views::View::ConvertPointToTarget(target, address, &point);
  return address->GetLocalBounds().Contains(point);
}

}  // namespace lb
