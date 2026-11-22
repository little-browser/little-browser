// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_HEADER_VIEW_H_
#define LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_HEADER_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class BrowserView;
class BrowserWindowInterface;

namespace ui {
class EventHandler;
}  // namespace ui

namespace views {
class Label;
}  // namespace views

namespace lb {

class CommandBar;

// Little Browser sidebar header (docs/ux.md §2), at the top of Chromium's vertical tab strip: the
// active space's name. The address is in the toolbar above the page; ⌘L edits it in the command
// bar. Owns the window's SidebarController and the command bar.
class SidebarHeaderView : public views::View, public SidebarController::Observer {
  METADATA_HEADER(SidebarHeaderView, views::View)

 public:
  explicit SidebarHeaderView(std::unique_ptr<SidebarController> controller);
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

 private:
  void UpdateSpace();
  void ShowCommandBar(CommandBarMode mode);

  std::unique_ptr<SidebarController> controller_;
  std::unique_ptr<CommandBar> command_bar_;
  std::unique_ptr<ui::EventHandler> swipe_handler_;
  raw_ptr<views::View> swipe_target_ = nullptr;
  // Collapsed strip: icons only.
  bool compact_ = false;
  raw_ptr<views::Label> space_label_ = nullptr;

  base::WeakPtrFactory<SidebarHeaderView> weak_factory_{this};
};

// Hook called by Chromium's VerticalTabStripRegionView (desktop/patches/lb/sidebar-hooks.patch):
// adds the header at the top of the strip's content area and the footer (space switcher, "+"
// menu) at the bottom, and hides the new-tab button in `bottom_container`.
void AddSidebar(views::View* content_area,
                BrowserView* browser_view,
                int horizontal_padding,
                views::View* bottom_container);

// --lb-sidebar-demo=sidebar-header: the header holds the space's name alone (no address field),
// and ⌘L still opens the command bar with the page's full address. Logs "lb demo: sidebar-header
// PASS ..." or "... FAIL ...".
void RunSidebarHeaderDemo(BrowserWindowInterface* browser);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_HEADER_VIEW_H_
