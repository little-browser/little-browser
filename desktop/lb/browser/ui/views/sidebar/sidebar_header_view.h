// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_HEADER_VIEW_H_
#define LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_HEADER_VIEW_H_

#include <map>
#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/image/image.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

class BrowserView;

namespace ui {
class EventHandler;
}  // namespace ui
class GURL;

namespace views {
class Label;
class LabelButton;
class MenuRunner;
}  // namespace views

namespace lb {

class CommandBar;

// Little Browser sidebar header (docs/ux.md §2), at the top of Chromium's vertical tab strip:
// address pill (domain of the active tab; click = edit address), favourites grid (per profile,
// with favicons; "+" adds the current tab, right-click removes) and the active space's name.
// Owns the window's SidebarController.
class SidebarHeaderView : public views::View,
                          public SidebarController::Observer,
                          public TabStripModelObserver,
                          public content::WebContentsObserver,
                          public views::ContextMenuController,
                          public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(SidebarHeaderView, views::View)

 public:
  SidebarHeaderView(BrowserView* browser_view, std::unique_ptr<SidebarController> controller);
  SidebarHeaderView(const SidebarHeaderView&) = delete;
  SidebarHeaderView& operator=(const SidebarHeaderView&) = delete;
  ~SidebarHeaderView() override;

  // views::View:
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;

  // SidebarController::Observer:
  void OnSidebarModelChanged() override;
  void OnSpaceSwitched(int direction) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(TabStripModel* tab_strip_model,
                              const TabStripModelChange& change,
                              const TabStripSelectionChange& selection) override;

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* navigation_handle) override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(views::View* source,
                                  const gfx::Point& point,
                                  ui::mojom::MenuSourceType source_type) override;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  void UpdateAddress();
  void UpdateSpace();
  void RebuildFavourites();
  void UpdateAddTile();
  void RequestFavicon(const GURL& url);
  void FocusAddress();
  void ShowCommandBar(CommandBarMode mode);

  std::unique_ptr<SidebarController> controller_;
  std::unique_ptr<CommandBar> command_bar_;
  std::unique_ptr<ui::EventHandler> swipe_handler_;
  raw_ptr<views::View> swipe_target_ = nullptr;
  // Collapsed strip: icons only.
  bool compact_ = false;
  raw_ptr<views::LabelButton> address_ = nullptr;
  raw_ptr<views::View> favourites_ = nullptr;
  raw_ptr<views::Label> space_label_ = nullptr;

  // Whether the current page can be added (the "+" tile), as last built.
  bool shows_add_tile_ = false;
  std::map<GURL, gfx::Image> favicons_;
  base::CancelableTaskTracker favicon_tracker_;

  int context_index_ = -1;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;

  base::WeakPtrFactory<SidebarHeaderView> weak_factory_{this};
};

// Hook called by Chromium's VerticalTabStripRegionView (desktop/patches/lb/sidebar-hooks.patch):
// adds the header at the top of the strip's content area and the space switcher at the bottom.
void AddSidebar(views::View* content_area, BrowserView* browser_view, int horizontal_padding);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_HEADER_VIEW_H_
