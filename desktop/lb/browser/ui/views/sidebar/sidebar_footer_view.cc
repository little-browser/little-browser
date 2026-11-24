// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/sidebar/sidebar_footer_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_constants.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/menu_separator_types.h"
#include "ui/base/mojom/menu_source_type.mojom-shared.h"
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
// Menu items. The "+" menu's tab actions use Chromium's own command ids (IDC_NEW_TAB and so on),
// which are all far above these.
constexpr int kCmdDeleteSpace = 1;
constexpr int kCmdRenameSpace = 2;
constexpr int kCmdIconBase = 100;
constexpr int kCmdColourBase = 200;
constexpr int kCmdNewSpace = 301;

// UI strings.
constexpr char16_t kNewButtonText[] = u"+";
constexpr char16_t kNewButtonName[] = u"New tab, space, split view or tab group";
constexpr char16_t kNewSpace[] = u"New Space";
constexpr char16_t kNewTab[] = u"New Tab";
constexpr char16_t kNewSplitView[] = u"New Split View";
constexpr char16_t kNewTabGroup[] = u"New Tab Group";
constexpr char16_t kRenameSpace[] = u"Rename…";
constexpr char16_t kSpaceIcon[] = u"Icon";
constexpr char16_t kSpaceColour[] = u"Colour";
std::u16string DeleteSpaceLabel(const std::u16string& name) {
  return u"Delete “" + name + u"”";
}
std::u16string SpaceButtonName(const std::u16string& name, const std::u16string& profile_name) {
  return name + u" (" + profile_name + u")";
}

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
  // [balance] [spaces, centred] [+]. The spaces take the room left and give it up first: when
  // they don't fit, the balance goes and the spaces line up from the start, clipped, so "+" stays.
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  balance_ = AddChildView(std::make_unique<views::View>());
  balance_->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
  spaces_ = AddChildView(std::make_unique<views::View>());
  spaces_layout_ = spaces_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
  spaces_layout_->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  layout->SetFlexForView(spaces_, 1);
  new_button_ = AddChildView(
      MakeButton(kNewButtonText, kNewButtonName,
                 base::BindRepeating(&SidebarFooterView::ShowNewMenu, base::Unretained(this))));
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
  // What fits depends on the width only.
  if (width() == 0 || width() == previous_bounds.width()) {
    return;
  }
  // Collapsed strip: only the active space.
  const bool compact = controller_ && IsSidebarCompact(controller_->browser(), width());
  if (compact != compact_) {
    compact_ = compact;
    Rebuild();
  } else {
    UpdateOverflow();
  }
}

void SidebarFooterView::UpdateOverflow() {
  const bool show_new = !compact_ && controller_;
  const bool overflow = show_new && spaces_->GetPreferredSize().width() + 2 * kButtonSize > width();
  balance_->SetVisible(show_new && !overflow);
  spaces_layout_->set_main_axis_alignment(overflow ? views::BoxLayout::MainAxisAlignment::kStart
                                                   : views::BoxLayout::MainAxisAlignment::kCenter);
}

void SidebarFooterView::OnSidebarModelChanged() {
  Rebuild();
}

void SidebarFooterView::Rebuild() {
  spaces_->RemoveAllChildViews();
  new_button_->SetVisible(!compact_ && controller_);
  if (!controller_) {
    UpdateOverflow();
    return;
  }
  const auto spaces = controller_->GetSpaces();
  const size_t active = controller_->GetActiveIndex();
  for (size_t i = 0; i < spaces.size(); ++i) {
    std::u16string name = base::UTF8ToUTF16(spaces[i].name);
    if (spaces[i].profile != chrome::kInitialProfile && !spaces[i].profile_name.empty()) {
      name = SpaceButtonName(name, spaces[i].profile_name);
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
    spaces_->AddChildView(std::move(button));
  }
  UpdateOverflow();
}

void SidebarFooterView::ShowNewMenu() {
  if (!controller_) {
    return;
  }
  context_index_ = -1;
  BrowserWindowInterface* browser = controller_->browser();
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  // Spaces first, set apart from the tab actions below.
  if (!controller_->IsPrivate()) {
    menu_model_->AddItem(kCmdNewSpace, kNewSpace);
    menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  }
  menu_model_->AddItem(IDC_NEW_TAB, kNewTab);
  // A split pairs the open page with a new tab, so it needs a page that isn't split already.
  const tabs::TabInterface* active = browser->GetTabStripModel()->GetActiveTab();
  if (active && !active->IsSplit() && active->GetContents() &&
      !IsBlankTabUrl(active->GetContents()->GetLastCommittedURL())) {
    menu_model_->AddItem(IDC_NEW_SPLIT_TAB, kNewSplitView);
  }
  menu_model_->AddItem(IDC_CREATE_NEW_TAB_GROUP, kNewTabGroup);
  menu_runner_ =
      std::make_unique<views::MenuRunner>(menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);
  menu_runner_->RunMenuAt(GetWidget(), nullptr, new_button_->GetAnchorBoundsInScreen(),
                          views::MenuAnchorPosition::kBubbleTopRight,
                          ui::mojom::MenuSourceType::kNone);
}

bool SidebarFooterView::GetAcceleratorForCommandId(int command_id,
                                                   ui::Accelerator* accelerator) const {
  // The "+" menu's tab actions show Chromium's shortcuts.
  if (command_id != IDC_NEW_TAB && command_id != IDC_NEW_SPLIT_TAB &&
      command_id != IDC_CREATE_NEW_TAB_GROUP) {
    return false;
  }
  BrowserView* browser_view =
      controller_ ? BrowserView::GetBrowserViewForBrowser(controller_->browser()) : nullptr;
  return browser_view && browser_view->GetAcceleratorForCommandId(command_id, accelerator);
}

void SidebarFooterView::ShowContextMenuForViewImpl(views::View* source,
                                                   const gfx::Point& point,
                                                   ui::mojom::MenuSourceType source_type) {
  context_index_ = source->GetID() - 1;
  // Private windows have one space and nothing to change about it.
  if (context_index_ < 0 || !controller_ || controller_->IsPrivate()) {
    return;
  }
  const auto spaces = controller_->GetSpaces();
  const std::u16string name = base::UTF8ToUTF16(spaces[context_index_].name);
  icon_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (size_t i = 0; i < SpaceIcons().size(); ++i) {
    icon_menu_->AddItem(kCmdIconBase + static_cast<int>(i), base::UTF8ToUTF16(SpaceIcons()[i]));
  }
  colour_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (size_t i = 0; i < SpaceColours().size(); ++i) {
    colour_menu_->AddItem(kCmdColourBase + static_cast<int>(i), SpaceColours()[i].name);
  }
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_model_->AddItem(kCmdRenameSpace, kRenameSpace);
  menu_model_->AddSubMenu(0, kSpaceIcon, icon_menu_.get());
  menu_model_->AddSubMenu(0, kSpaceColour, colour_menu_.get());
  if (controller_->CanDeleteSpace(context_index_)) {
    menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    menu_model_->AddItem(kCmdDeleteSpace, DeleteSpaceLabel(name));
  }
  menu_runner_ =
      std::make_unique<views::MenuRunner>(menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  menu_runner_->RunMenuAt(GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft, source_type);
}

void SidebarFooterView::ExecuteCommand(int command_id, int event_flags) {
  if (!controller_) {
    return;
  }
  BrowserWindowInterface* browser = controller_->browser();
  switch (command_id) {
    case IDC_NEW_TAB:
      chrome::ExecuteCommand(browser, IDC_NEW_TAB);
      return;
    case IDC_CREATE_NEW_TAB_GROUP:
      controller_->NewTabGroup();  // remembers the group's placeholder tab
      return;
    case kCmdNewSpace:
      controller_->NewSpace();
      return;
    case IDC_NEW_SPLIT_TAB:
      chrome::NewSplitTab(browser, split_tabs::SplitTabLayout::kSideBySide,
                          split_tabs::SplitTabCreatedSource::kNewTabButton);
      return;
  }
  if (context_index_ < 0) {
    return;
  }
  const size_t index = static_cast<size_t>(context_index_);
  if (command_id == kCmdDeleteSpace) {
    controller_->DeleteSpace(index);
  } else if (command_id == kCmdRenameSpace) {
    controller_->StartRename(index);
  } else if (command_id >= kCmdColourBase &&
             command_id < kCmdColourBase + static_cast<int>(SpaceColours().size())) {
    controller_->SetSpaceHue(index, SpaceColours()[command_id - kCmdColourBase].hue);
  } else if (command_id >= kCmdIconBase &&
             command_id < kCmdIconBase + static_cast<int>(SpaceIcons().size())) {
    controller_->SetSpaceIcon(index, SpaceIcons()[command_id - kCmdIconBase]);
  }
}

BEGIN_METADATA(SidebarFooterView)
END_METADATA

}  // namespace lb
