// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/sidebar/sidebar_footer_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/chrome_constants.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/menu_separator_types.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"

namespace lb {

namespace {

constexpr int kButtonSize = 30;
constexpr float kRadius = 8.0f;
constexpr int kMinVisibleWidth = 150;
constexpr int kCmdDeleteSpace = 1;
constexpr int kCmdRenameSpace = 2;
constexpr int kCmdIconBase = 100;
constexpr int kCmdColourBase = 200;

std::unique_ptr<views::LabelButton> MakeButton(std::u16string text,
                                               std::u16string name,
                                               views::Button::PressedCallback cb) {
  auto button = std::make_unique<views::LabelButton>(std::move(cb), std::move(text));
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  button->SetMinSize(gfx::Size(kButtonSize, kButtonSize));
  button->SetMaxSize(gfx::Size(kButtonSize, kButtonSize));
  button->SetBorder(nullptr);
  button->SetEnabledTextColors(ui::kColorSysOnSurfaceSubtle);
  button->SetTooltipText(name);
  button->GetViewAccessibility().SetName(name);
  return button;
}

}  // namespace

SidebarFooterView::SidebarFooterView(base::WeakPtr<SidebarController> controller)
    : controller_(std::move(controller)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  if (controller_) {
    controller_->AddObserver(this);
  }
  Rebuild();
}

SidebarFooterView::~SidebarFooterView() {
  if (controller_) {
    controller_->RemoveObserver(this);
  }
}

void SidebarFooterView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  if (width() == 0) {
    return;
  }
  const bool compact = width() < kMinVisibleWidth;
  if (compact != compact_) {
    compact_ = compact;
    Rebuild();
  }
}

void SidebarFooterView::OnSidebarModelChanged() {
  Rebuild();
}

void SidebarFooterView::Rebuild() {
  RemoveAllChildViews();
  if (!controller_) {
    return;
  }
  const auto spaces = controller_->GetSpaces();
  const size_t active = controller_->GetActiveIndex();
  for (size_t i = 0; i < spaces.size(); ++i) {
    std::u16string name = base::UTF8ToUTF16(spaces[i].name);
    if (spaces[i].profile != chrome::kInitialProfile && !spaces[i].profile_name.empty()) {
      name += u" (" + spaces[i].profile_name + u")";
    }
    auto button = MakeButton(base::UTF8ToUTF16(spaces[i].icon), name,
                             base::BindRepeating(&SidebarController::SwitchTo, controller_, i));
    if (i == active) {
      button->SetBackground(
          views::CreateRoundedRectBackground(ui::kColorSysOmniboxContainer, kRadius));
    }
    button->SetID(static_cast<int>(i) + 1);
    button->set_context_menu_controller(this);
    button->SetVisible(!compact_ || i == active);
    AddChildView(std::move(button));
  }
  AddChildView(MakeButton(u"+", u"New space",
                          base::BindRepeating(&SidebarController::NewSpace, controller_)))
      ->SetVisible(!compact_ && !controller_->IsPrivate());
  InvalidateLayout();
}

void SidebarFooterView::ShowContextMenuForViewImpl(views::View* source,
                                                   const gfx::Point& point,
                                                   ui::mojom::MenuSourceType source_type) {
  context_index_ = source->GetID() - 1;
  if (context_index_ < 0 || !controller_) {
    return;
  }
  const auto spaces = controller_->GetSpaces();
  const std::u16string name = base::UTF8ToUTF16(spaces[context_index_].name);
  icon_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (size_t i = 0; i < std::size(kSpaceIcons); ++i) {
    icon_menu_->AddItem(kCmdIconBase + static_cast<int>(i), base::UTF8ToUTF16(kSpaceIcons[i]));
  }
  colour_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (size_t i = 0; i < std::size(kSpaceColours); ++i) {
    colour_menu_->AddItem(kCmdColourBase + static_cast<int>(i), kSpaceColours[i].name);
  }
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_model_->AddItem(kCmdRenameSpace, u"Rename…");
  menu_model_->AddSubMenu(0, u"Icon", icon_menu_.get());
  menu_model_->AddSubMenu(0, u"Colour", colour_menu_.get());
  if (controller_->IsPrivate()) {
    return;
  }
  if (controller_->CanDeleteSpace(context_index_)) {
    menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    menu_model_->AddItem(kCmdDeleteSpace, u"Delete “" + name + u"”");
  }
  menu_runner_ =
      std::make_unique<views::MenuRunner>(menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  menu_runner_->RunMenuAt(GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft, source_type);
}

void SidebarFooterView::ExecuteCommand(int command_id, int event_flags) {
  if (!controller_ || context_index_ < 0) {
    return;
  }
  const size_t index = static_cast<size_t>(context_index_);
  if (command_id == kCmdDeleteSpace) {
    controller_->DeleteSpace(index);
  } else if (command_id == kCmdRenameSpace) {
    controller_->StartRename(index);
  } else if (command_id >= kCmdColourBase &&
             command_id < kCmdColourBase + static_cast<int>(std::size(kSpaceColours))) {
    controller_->SetSpaceHue(index, kSpaceColours[command_id - kCmdColourBase].hue);
  } else if (command_id >= kCmdIconBase &&
             command_id < kCmdIconBase + static_cast<int>(std::size(kSpaceIcons))) {
    controller_->SetSpaceIcon(index, kSpaceIcons[command_id - kCmdIconBase]);
  }
}

BEGIN_METADATA(SidebarFooterView)
END_METADATA

}  // namespace lb
