// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_FOOTER_VIEW_H_
#define LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_FOOTER_VIEW_H_

#include <memory>

#include "base/memory/weak_ptr.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

namespace views {
class MenuRunner;
}  // namespace views

namespace lb {

// Bottom of the sidebar (docs/ux.md §2, §5): one button per space (click switches; right-click:
// Rename…, Icon, Colour, Delete) and "+" for a new space. Collapsed strip: active space only.
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

 private:
  void Rebuild();

  base::WeakPtr<SidebarController> controller_;
  int context_index_ = -1;
  bool compact_ = false;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> icon_menu_;
  std::unique_ptr<ui::SimpleMenuModel> colour_menu_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_FOOTER_VIEW_H_
