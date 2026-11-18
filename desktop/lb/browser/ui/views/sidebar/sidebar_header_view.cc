// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/sidebar/sidebar_header_view.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_constants.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "lb/browser/spaces/swipe_tracker.h"
#include "lb/browser/ui/lb_ui_snapshot.h"
#include "lb/browser/ui/views/command_bar/command_bar.h"
#include "lb/browser/ui/views/sidebar/sidebar_footer_view.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/events/event.h"
#include "ui/events/event_handler.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "url/gurl.h"

namespace lb {

namespace {

constexpr int kPillHeight = 34;
constexpr int kTileHeight = 44;
constexpr float kRadius = 10.0f;
constexpr int kGap = 6;
constexpr size_t kTilesPerRow = 4;
constexpr size_t kMaxFavourites = 8;  // two rows; Cmd+1…8 later
// Below this width (the strip is collapsed) the header hides.
constexpr int kMinVisibleWidth = 150;

constexpr int kCmdRemoveFavourite = 1;
constexpr int kCompactTileHeight = 36;

// Two-finger sideways swipe on the sidebar switches space (docs/ux.md §5).
class SwipeHandler : public ui::EventHandler {
 public:
  explicit SwipeHandler(base::WeakPtr<SidebarController> controller)
      : controller_(std::move(controller)) {}

  void OnScrollEvent(ui::ScrollEvent* event) override {
    if (event->type() == ui::EventType::kScroll) {
      Handle(tracker_.OnScroll(event->x_offset(), event->y_offset(), event->time_stamp()), event);
    } else if (event->type() == ui::EventType::kScrollFlingStart) {
      tracker_.OnFlingStart();
    }
  }

  void OnMouseEvent(ui::MouseEvent* event) override {
    if (event->type() == ui::EventType::kMousewheel) {
      auto* wheel = static_cast<ui::MouseWheelEvent*>(event);
      Handle(tracker_.OnScroll(wheel->x_offset(), wheel->y_offset(), event->time_stamp()), event);
    }
  }

 private:
  // Swiping doesn't wrap around (with two spaces a wrap looks like bouncing back).
  void Handle(SwipeTracker::Result result, ui::Event* event) {
    if (result.handled) {
      event->SetHandled();
    }
    if (result.step == 0 || !controller_) {
      return;
    }
    const int target = static_cast<int>(controller_->GetActiveIndex()) + result.step;
    if (target >= 0 && target < static_cast<int>(controller_->GetSpaces().size())) {
      controller_->SwitchTo(static_cast<size_t>(target));
    }
  }

  base::WeakPtr<SidebarController> controller_;
  SwipeTracker tracker_;
};

// LabelButton with a settable label font (Chromium keeps label() protected).
class FontLabelButton : public views::LabelButton {
  METADATA_HEADER(FontLabelButton, views::LabelButton)

 public:
  using views::LabelButton::LabelButton;
  void SetLabelFont(const gfx::FontList& font_list) { label()->SetFontList(font_list); }
};

BEGIN_METADATA(FontLabelButton)
END_METADATA

std::u16string DisplayHost(const GURL& url) {
  if (!url.is_valid() || url.is_empty()) {
    return u"Search or enter address";
  }
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return base::UTF8ToUTF16(url.spec());
  }
  std::string_view host = url.host();
  if (base::StartsWith(host, "www.")) {
    host.remove_prefix(4);
  }
  return base::UTF8ToUTF16(host);
}

std::unique_ptr<FontLabelButton> MakeTile(std::u16string text,
                                          std::u16string tooltip,
                                          views::Button::PressedCallback cb) {
  auto tile = std::make_unique<FontLabelButton>(std::move(cb), std::move(text));
  tile->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  tile->SetMinSize(gfx::Size(0, kTileHeight));
  tile->SetBorder(nullptr);
  tile->SetBackground(views::CreateRoundedRectBackground(ui::kColorSysOmniboxContainer, kRadius));
  tile->SetEnabledTextColors(ui::kColorSysOnSurface);
  tile->SetLabelFont(
      gfx::FontList().DeriveWithSizeDelta(3).DeriveWithWeight(gfx::Font::Weight::BOLD));
  tile->SetTooltipText(tooltip);
  tile->GetViewAccessibility().SetName(tooltip);
  return tile;
}

}  // namespace

SidebarHeaderView::SidebarHeaderView(BrowserView* browser_view,
                                     std::unique_ptr<SidebarController> controller)
    : controller_(std::move(controller)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(2, 0, 10, 0),
      /*between_child_spacing=*/12));
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);

  // Address pill: the active tab's domain. Clicking it edits the address in the
  // command bar.
  auto* address = AddChildView(std::make_unique<FontLabelButton>(
      base::BindRepeating(&SidebarHeaderView::FocusAddress, base::Unretained(this))));
  address->SetLabelFont(gfx::FontList().DeriveWithWeight(gfx::Font::Weight::MEDIUM));
  address_ = address;
  address_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  address_->SetMinSize(gfx::Size(0, kPillHeight));
  address_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 12)));
  address_->SetBackground(
      views::CreateRoundedRectBackground(ui::kColorSysOmniboxContainer, kRadius));
  address_->SetEnabledTextColors(ui::kColorSysOnSurface);
  address_->GetViewAccessibility().SetName(u"Address");

  // Favourites: rows of four tiles, per profile.
  favourites_ = AddChildView(std::make_unique<views::View>());
  favourites_
      ->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), kGap))
      ->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);

  // Active space.
  space_label_ = AddChildView(std::make_unique<views::Label>());
  space_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  space_label_->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  space_label_->SetFontList(
      gfx::FontList().DeriveWithSizeDelta(-1).DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));
  space_label_->SetProperty(views::kMarginsKey, gfx::Insets::VH(0, 6));

  controller_->AddObserver(this);
  controller_->browser()->GetTabStripModel()->AddObserver(this);
  Observe(controller_->browser()->GetTabStripModel()->GetActiveWebContents());
  UpdateAddress();
  UpdateSpace();
  RebuildFavourites();
}

SidebarHeaderView::~SidebarHeaderView() {
  if (swipe_target_) {
    swipe_target_->RemovePreTargetHandler(swipe_handler_.get());
  }
  controller_->SetShowCommandBarCallback({});
  controller_->RemoveObserver(this);
}

void SidebarHeaderView::AddedToWidget() {
  command_bar_ = std::make_unique<CommandBar>(controller_.get(), GetWidget());
  controller_->SetShowCommandBarCallback(
      base::BindRepeating(&SidebarHeaderView::ShowCommandBar, weak_factory_.GetWeakPtr()));

  // Spaces: Ctrl+1…9, Ctrl+Option+←/→ (plain Ctrl+arrows belong to macOS Spaces).
  views::FocusManager* focus = GetFocusManager();
  for (int i = 0; i < 9; ++i) {
    focus->RegisterAccelerator(
        ui::Accelerator(static_cast<ui::KeyboardCode>(ui::VKEY_1 + i), ui::EF_CONTROL_DOWN),
        ui::AcceleratorManager::kNormalPriority, this);
  }
  for (ui::KeyboardCode key : {ui::VKEY_LEFT, ui::VKEY_RIGHT}) {
    focus->RegisterAccelerator(ui::Accelerator(key, ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN),
                               ui::AcceleratorManager::kNormalPriority, this);
  }

  if (parent() && !swipe_target_) {
    swipe_handler_ = std::make_unique<SwipeHandler>(controller_->GetWeakPtr());
    swipe_target_ = parent();
    swipe_target_->AddPreTargetHandler(swipe_handler_.get());
  }
  MaybeScheduleUiSnapshot(GetWidget());
}

void SidebarHeaderView::RemovedFromWidget() {
  if (views::FocusManager* focus = GetFocusManager()) {
    focus->UnregisterAccelerators(this);
  }
  controller_->SetShowCommandBarCallback({});
  command_bar_.reset();
}

bool SidebarHeaderView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  const ui::KeyboardCode key = accelerator.key_code();
  if (key >= ui::VKEY_1 && key <= ui::VKEY_9) {
    controller_->SwitchTo(static_cast<size_t>(key - ui::VKEY_1));
    return true;
  }
  if (key == ui::VKEY_LEFT || key == ui::VKEY_RIGHT) {
    controller_->SwitchBy(key == ui::VKEY_RIGHT ? 1 : -1);
    return true;
  }
  return false;
}

void SidebarHeaderView::ShowCommandBar(CommandBarMode mode) {
  if (command_bar_) {
    command_bar_->Show(mode);
  }
}

void SidebarHeaderView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  if (width() == 0) {
    return;
  }
  const bool compact = width() < kMinVisibleWidth;
  if (compact == compact_) {
    return;
  }
  compact_ = compact;
  address_->SetVisible(!compact_);
  space_label_->SetVisible(!compact_);
  RebuildFavourites();
}

void SidebarHeaderView::OnSidebarModelChanged() {
  UpdateSpace();
  RebuildFavourites();
}

void SidebarHeaderView::OnSpaceSwitched(int direction) {
  // The sidebar's contents slide in from the side of the new space and fade in (180 ms).
  ui::Layer* layer = parent() ? parent()->layer() : nullptr;
  if (!layer) {
    return;
  }
  gfx::Transform from;
  from.Translate(direction * 24.0f, 0);
  views::AnimationBuilder()
      .SetPreemptionStrategy(ui::LayerAnimator::IMMEDIATELY_SET_NEW_TARGET)
      .Once()
      .SetDuration(base::TimeDelta())
      .SetTransform(layer, from)
      .SetOpacity(layer, 0.0f)
      .Then()
      .SetDuration(base::Milliseconds(180))
      .SetTransform(layer, gfx::Transform(), gfx::Tween::EASE_OUT)
      .SetOpacity(layer, 1.0f, gfx::Tween::EASE_OUT);
}

void SidebarHeaderView::OnTabStripModelChanged(TabStripModel* tab_strip_model,
                                               const TabStripModelChange& change,
                                               const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    Observe(tab_strip_model->GetActiveWebContents());
    UpdateAddTile();
  }
  UpdateAddress();
}

void SidebarHeaderView::DidFinishNavigation(content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInPrimaryMainFrame()) {
    UpdateAddress();
    UpdateAddTile();
  }
}

void SidebarHeaderView::UpdateAddress() {
  content::WebContents* contents =
      controller_->browser()->GetTabStripModel()->GetActiveWebContents();
  const GURL url = contents ? contents->GetVisibleURL() : GURL();
  address_->SetText(DisplayHost(url));
  address_->SetTooltipText(base::UTF8ToUTF16(url.possibly_invalid_spec()));
}

void SidebarHeaderView::UpdateSpace() {
  const auto spaces = controller_->GetSpaces();
  const size_t active = controller_->GetActiveIndex();
  if (active >= spaces.size()) {
    return;
  }
  const Space& space = spaces[active];
  std::u16string text = base::UTF8ToUTF16(space.icon + "  " + space.name);
  // Profile badge for spaces outside the default profile (docs/ux.md §2, §5).
  if (!space.profile.empty() && space.profile != chrome::kInitialProfile) {
    text += u"   \U0001F512 " + space.profile_name;
  }
  space_label_->SetText(text);
}

void SidebarHeaderView::UpdateAddTile() {
  // The "+" tile depends on the current page; rebuild only when it comes or goes.
  if (controller_->CanAddCurrentTab() != shows_add_tile_) {
    RebuildFavourites();
  }
}

void SidebarHeaderView::RebuildFavourites() {
  favourites_->RemoveAllChildViews();
  shows_add_tile_ = controller_->CanAddCurrentTab();
  const auto favourites = controller_->GetFavourites();

  std::vector<std::unique_ptr<views::View>> tiles;
  for (size_t i = 0; i < favourites.size() && i < kMaxFavourites; ++i) {
    const Favourite& fav = favourites[i];
    const std::u16string title = base::UTF8ToUTF16(fav.title);
    auto it = favicons_.find(fav.url);
    const bool has_icon = it != favicons_.end() && !it->second.IsEmpty();
    std::u16string initial = title.empty() ? u"?" : title.substr(0, 1);
    auto tile = MakeTile(
        has_icon ? std::u16string() : initial, title,
        base::BindRepeating(&SidebarController::OpenFavourite, controller_->GetWeakPtr(), i));
    if (has_icon) {
      tile->SetImageModel(views::Button::STATE_NORMAL, ui::ImageModel::FromImage(it->second));
    } else if (it == favicons_.end()) {
      RequestFavicon(fav.url);
    }
    tile->SetID(static_cast<int>(i) + 1);
    tile->set_context_menu_controller(this);
    tiles.push_back(std::move(tile));
  }
  if (favourites.size() < kMaxFavourites && shows_add_tile_) {
    tiles.push_back(MakeTile(u"+", u"Add this page to favourites",
                             base::BindRepeating(&SidebarController::AddCurrentTabToFavourites,
                                                 controller_->GetWeakPtr())));
  }

  const size_t per_row = compact_ ? 1 : kTilesPerRow;
  if (compact_) {
    for (auto& tile : tiles) {
      static_cast<views::LabelButton*>(tile.get())->SetMinSize(gfx::Size(0, kCompactTileHeight));
    }
  }
  views::View* row = nullptr;
  for (size_t i = 0; i < tiles.size(); ++i) {
    if (i % per_row == 0) {
      row = favourites_->AddChildView(std::make_unique<views::View>());
      row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kGap));
    }
    auto* tile = row->AddChildView(std::move(tiles[i]));
    static_cast<views::BoxLayout*>(row->GetLayoutManager())->SetFlexForView(tile, 1);
  }
  // Keep the last row's tiles the same width as full rows.
  if (row && row->children().size() < per_row) {
    for (size_t i = row->children().size(); i < per_row; ++i) {
      auto* spacer = row->AddChildView(std::make_unique<views::View>());
      static_cast<views::BoxLayout*>(row->GetLayoutManager())->SetFlexForView(spacer, 1);
    }
  }
  favourites_->SetVisible(!tiles.empty());
  InvalidateLayout();
}

void SidebarHeaderView::RequestFavicon(const GURL& url) {
  favicons_[url] = gfx::Image();  // in flight / none
  favicon::FaviconService* service = FaviconServiceFactory::GetForProfile(
      controller_->browser()->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
  if (!service) {
    return;
  }
  service->GetFaviconImageForPageURL(url,
                                     base::BindOnce(
                                         [](base::WeakPtr<SidebarHeaderView> self, GURL url,
                                            const favicon_base::FaviconImageResult& result) {
                                           if (!self) {
                                             return;
                                           }
                                           if (result.image.IsEmpty()) {
                                             // None stored yet: ask again next time.
                                             self->favicons_.erase(url);
                                             return;
                                           }
                                           self->favicons_[url] = result.image;
                                           self->RebuildFavourites();
                                         },
                                         weak_factory_.GetWeakPtr(), url),
                                     &favicon_tracker_);
}

void SidebarHeaderView::FocusAddress() {
  controller_->ShowCommandBar(CommandBarMode::kEditAddress);
}

void SidebarHeaderView::ShowContextMenuForViewImpl(views::View* source,
                                                   const gfx::Point& point,
                                                   ui::mojom::MenuSourceType source_type) {
  context_index_ = source->GetID() - 1;
  if (context_index_ < 0) {
    return;
  }
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_model_->AddItem(kCmdRemoveFavourite, u"Remove from favourites");
  menu_runner_ =
      std::make_unique<views::MenuRunner>(menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  menu_runner_->RunMenuAt(GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft, source_type);
}

void SidebarHeaderView::ExecuteCommand(int command_id, int event_flags) {
  if (command_id == kCmdRemoveFavourite && context_index_ >= 0) {
    controller_->RemoveFavourite(static_cast<size_t>(context_index_));
  }
}

BEGIN_METADATA(SidebarHeaderView)
END_METADATA

void AddSidebar(views::View* content_area, BrowserView* browser_view, int horizontal_padding) {
  auto controller = std::make_unique<SidebarController>(browser_view->browser());
  base::WeakPtr<SidebarController> weak_controller = controller->GetWeakPtr();

  auto* header = content_area->AddChildViewAt(
      std::make_unique<SidebarHeaderView>(browser_view, std::move(controller)), 0);
  header->SetProperty(views::kMarginsKey, gfx::Insets::VH(0, horizontal_padding));

  auto* footer = content_area->AddChildView(std::make_unique<SidebarFooterView>(weak_controller));
  footer->SetProperty(views::kMarginsKey,
                      gfx::Insets::TLBR(6, horizontal_padding, 8, horizontal_padding));
}

}  // namespace lb
