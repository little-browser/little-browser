// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/sidebar/sidebar_header_view.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_constants.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "lb/browser/spaces/swipe_tracker.h"
#include "lb/browser/ui/lb_ui_snapshot.h"
#include "lb/browser/ui/views/command_bar/command_bar.h"
#include "lb/browser/ui/views/sidebar/sidebar_footer_view.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_impl_macros.h"
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
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "url/gurl.h"

namespace lb {

namespace {

constexpr int kPillHeight = 34;
constexpr float kRadius = 10.0f;

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

}  // namespace

SidebarHeaderView::SidebarHeaderView(std::unique_ptr<SidebarController> controller)
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
  // Collapsed strip: the header hides.
  const bool compact = IsSidebarCompact(controller_->browser(), width());
  if (compact == compact_) {
    return;
  }
  compact_ = compact;
  address_->SetVisible(!compact_);
  space_label_->SetVisible(!compact_);
}

void SidebarHeaderView::OnSidebarModelChanged() {
  UpdateSpace();
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
  }
  UpdateAddress();
}

void SidebarHeaderView::DidFinishNavigation(content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInPrimaryMainFrame()) {
    UpdateAddress();
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

void SidebarHeaderView::FocusAddress() {
  controller_->ShowCommandBar(CommandBarMode::kEditAddress);
}

BEGIN_METADATA(SidebarHeaderView)
END_METADATA

void AddSidebar(views::View* content_area,
                BrowserView* browser_view,
                int horizontal_padding,
                views::View* bottom_container) {
  // Chromium's new-tab button: "+ New Tab" above today's tabs replaces it (new_tab_row_view.h).
  // The container stays, since it is what fills the space between the tabs and the footer.
  for (views::View* child : bottom_container->children()) {
    child->SetVisible(false);
  }

  auto controller = std::make_unique<SidebarController>(browser_view->browser());
  base::WeakPtr<SidebarController> weak_controller = controller->GetWeakPtr();

  auto* header =
      content_area->AddChildViewAt(std::make_unique<SidebarHeaderView>(std::move(controller)), 0);
  header->SetProperty(views::kMarginsKey, gfx::Insets::VH(0, horizontal_padding));

  auto* footer = content_area->AddChildView(std::make_unique<SidebarFooterView>(weak_controller));
  footer->SetProperty(views::kMarginsKey,
                      gfx::Insets::TLBR(6, horizontal_padding, 8, horizontal_padding));
}

}  // namespace lb
