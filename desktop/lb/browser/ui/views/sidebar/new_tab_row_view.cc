// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/sidebar/new_tab_row_view.h"

#include <memory>

#include "base/functional/bind.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"

namespace lb {

namespace {

constexpr int kRowHeight = 34;
constexpr int kRadius = 8;
constexpr int kHorizontalPadding = 8;
// Below this width (the strip is collapsed) the row shows "+" only.
constexpr int kMinLabelWidth = 150;

}  // namespace

NewTabRowView::NewTabRowView(BrowserWindowInterface* browser) : browser_(browser) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(0, 0, 2, 0), 6));
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);

  divider_ = AddChildView(std::make_unique<views::Separator>());
  // The same colour as Chromium's own divider between pinned and unpinned tabs.
  divider_->SetColorId(kColorTabDividerFrameActive);
  divider_->SetProperty(views::kMarginsKey, gfx::Insets::VH(0, kHorizontalPadding));

  button_ = AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(
          [](BrowserWindowInterface* browser) { chrome::ExecuteCommand(browser, IDC_NEW_TAB); },
          browser_),
      u"+  New Tab"));
  button_->SetMinSize(gfx::Size(0, kRowHeight));
  // The "+" lines up with the tabs' favicons below it.
  button_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, kHorizontalPadding + 8)));
  button_->SetEnabledTextColors(ui::kColorSysOnSurfaceSubtle);
  button_->SetProperty(views::kMarginsKey, gfx::Insets::VH(0, kHorizontalPadding));
  button_->GetViewAccessibility().SetName(u"New Tab");
  views::InkDrop::Get(button_)->SetMode(views::InkDropHost::InkDropMode::ON);
  button_->SetHasInkDropActionOnClick(true);
  views::InstallRoundRectHighlightPathGenerator(button_, gfx::Insets(), kRadius);

  browser_->GetTabStripModel()->AddObserver(this);
  UpdateDivider();
}

NewTabRowView::~NewTabRowView() {
  browser_->GetTabStripModel()->RemoveObserver(this);
}

void NewTabRowView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  if (width() == 0) {
    return;
  }
  const bool compact = width() < kMinLabelWidth;
  if (compact == compact_) {
    return;
  }
  compact_ = compact;
  button_->SetText(compact_ ? u"+" : u"+  New Tab");
  button_->SetHorizontalAlignment(compact_ ? gfx::ALIGN_CENTER : gfx::ALIGN_LEFT);
  button_->SetTooltipText(compact_ ? u"New Tab" : std::u16string());
}

void NewTabRowView::OnTabStripModelChanged(TabStripModel* tab_strip_model,
                                           const TabStripModelChange& change,
                                           const TabStripSelectionChange& selection) {
  UpdateDivider();
}

void NewTabRowView::OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) {
  UpdateDivider();
}

void NewTabRowView::UpdateDivider() {
  const bool has_pinned = browser_->GetTabStripModel()->IndexOfFirstNonPinnedTab() > 0;
  if (divider_->GetVisible() != has_pinned) {
    divider_->SetVisible(has_pinned);
    PreferredSizeChanged();
  }
}

BEGIN_METADATA(NewTabRowView)
END_METADATA

void AddNewTabRow(TabStripView& tab_strip, BrowserWindowInterface* browser) {
  tab_strip.SetLbNewTabRow(std::make_unique<NewTabRowView>(browser));
}

}  // namespace lb
