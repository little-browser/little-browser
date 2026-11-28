// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_FOOTER_VIEW_H_
#define LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_FOOTER_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

namespace views {
class BoxLayout;
class Button;
class MenuRunner;
}  // namespace views

namespace lb {

// Bottom of the sidebar (docs/ux.md §2, §5): one button per space, centred (click switches;
// right-click: Rename…, Icon, Colour, Delete), and "+" in the corner, whose menu has New space,
// then, set apart, New tab, New split view (while a page is open) and New tab group. Collapsed
// strip: the active space only.
class SidebarFooterView : public views::View,
                          public SidebarController::Observer,
                          public views::ContextMenuController,
                          public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(SidebarFooterView, views::View)

 public:
  explicit SidebarFooterView(base::WeakPtr<SidebarController> controller);
  SidebarFooterView(const SidebarFooterView&) = delete;
  SidebarFooterView& operator=(const SidebarFooterView&) = delete;
  ~SidebarFooterView() override;

  // views::View:
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;

  // SidebarController::Observer:
  void OnSidebarModelChanged() override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(views::View* source,
                                  const gfx::Point& point,
                                  ui::mojom::MenuSourceType source_type) override;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool GetAcceleratorForCommandId(int command_id, ui::Accelerator* accelerator) const override;

 private:
  void Rebuild();
  // Fits the space buttons and "+" in the width: see the constructor.
  void UpdateOverflow();
  void ShowNewMenu();

  base::WeakPtr<SidebarController> controller_;
  int context_index_ = -1;
  bool compact_ = false;
  raw_ptr<views::View> spaces_ = nullptr;
  raw_ptr<views::BoxLayout> spaces_layout_ = nullptr;
  raw_ptr<views::View> balance_ = nullptr;  // as wide as "+", so the spaces stay centred
  raw_ptr<views::Button> new_button_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> icon_menu_;
  std::unique_ptr<ui::SimpleMenuModel> colour_menu_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_FOOTER_VIEW_H_
