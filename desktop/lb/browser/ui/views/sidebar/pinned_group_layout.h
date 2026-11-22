// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_SIDEBAR_PINNED_GROUP_LAYOUT_H_
#define LB_BROWSER_UI_VIEWS_SIDEBAR_PINNED_GROUP_LAYOUT_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_interface.h"
#include "lb/browser/tabs/pinned_groups.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class TabCollectionNode;
class TabStripModel;

namespace views {
class ImageView;
class Label;
class MenuRunner;
class Textfield;
class Widget;
struct ProposedLayout;
}  // namespace views

namespace lb {

// A pinned group's header row (docs/ux.md §6.1): chevron, colour, name and, while collapsed, the
// number of tabs. A click collapses or expands the group, a double click renames it, and its
// context menu has the group's commands (lb::PinnedGroupMenu).
class PinnedGroupHeaderView : public views::View,
                              public views::ContextMenuController,
                              public ui::SimpleMenuModel::Delegate,
                              public views::TextfieldController {
  METADATA_HEADER(PinnedGroupHeaderView, views::View)

 public:
  PinnedGroupHeaderView(SidebarController* controller, std::string group_id);
  PinnedGroupHeaderView(const PinnedGroupHeaderView&) = delete;
  PinnedGroupHeaderView& operator=(const PinnedGroupHeaderView&) = delete;
  ~PinnedGroupHeaderView() override;

  const std::string& group_id() const { return group_id_; }
  // Shows `group` (its name, colour, collapsed state) with `tab_count` tabs.
  void Update(const PinnedGroup& group, size_t tab_count);
  // An inline field for the name, selected; Enter or leaving it keeps the name, Esc doesn't.
  void StartRename();

  // While the header is dragged: the image of the group that follows the pointer.
  views::Widget* GetDragImageForTesting() const { return drag_image_.get(); }

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;
  void OnThemeChanged() override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(views::View* source,
                                  const gfx::Point& point,
                                  ui::mojom::MenuSourceType source_type) override;
  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdChecked(int command_id) const override;
  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender, const ui::KeyEvent& key_event) override;
  void OnAfterUserAction(views::Textfield* sender) override {}

 private:
  void FinishRename(bool keep);
  void UpdateColors();

  base::WeakPtr<SidebarController> controller_;
  const std::string group_id_;
  PinnedGroup group_;
  raw_ptr<views::ImageView> chevron_ = nullptr;
  raw_ptr<views::View> dot_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Textfield> rename_ = nullptr;
  raw_ptr<views::Label> count_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> color_menu_;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  // The collapsed state before the first click of a double click, which the second click
  // restores (it renames instead).
  bool collapsed_before_click_ = false;
  // A drag of the header moves the whole group (docs/ux.md §6.1): where it was pressed, and
  // whether the drag has started.
  gfx::Point press_point_;
  bool dragging_ = false;
  std::unique_ptr<views::Widget> drag_image_;
  void StartDrag();
  void EndDrag();
};

// Pinned groups in the vertical strip's pinned container (patches/lb/pinned-groups.patch): their
// headers and colour lines, and which tabs a collapsed group hides. It lives on the container (a
// view property) and lays the container out again whenever the window's pinned groups change.
class PinnedGroupLayout : public SidebarController::Observer {
 public:
  // One child of the container: a header above it, whether it shows, and how far it is indented.
  struct Row {
    raw_ptr<views::View> header = nullptr;
    bool shown = true;
    int indent = 0;
  };

  // The container's helper, made on first use; null outside a window with our sidebar.
  static PinnedGroupLayout* For(views::View& container, const TabCollectionNode* node);
  // The container's helper if it has one.
  static PinnedGroupLayout* Of(views::View& container);

  PinnedGroupLayout(views::View& container, SidebarController* controller);
  PinnedGroupLayout(const PinnedGroupLayout&) = delete;
  PinnedGroupLayout& operator=(const PinnedGroupLayout&) = delete;
  ~PinnedGroupLayout() override;

  // Before a layout: the rows for `node`'s children (pinned tabs and split views). With
  // `expanded` false (the collapsed strip) there are no headers and every tab shows.
  void Prepare(const TabCollectionNode& node, bool expanded);
  // The row of the container's child `index`, as Prepare() found it.
  Row RowFor(size_t index) const;
  // During a drag: the container's child `index` is dragged, drawn at `drawn`; `slot` is its
  // place in the list.
  void SetDragged(size_t index, const gfx::Rect& drawn, const gfx::Rect& slot);
  // While group `id` is dragged by its header its tabs fold away under it (empty: none is).
  void SetDraggingGroup(const std::string& id);
  // After a layout: adds each group's colour line beside its shown tabs, and hides the headers
  // and lines no group uses now.
  void Finish(views::ProposedLayout& layout) const;
  // The height of a header row.
  static int HeaderHeight();
  // The container's views of group `id`: its tab rows (and split views) and its line.
  std::vector<views::View*> GroupViews(const std::string& id) const;

  // SidebarController::Observer:
  void OnSidebarModelChanged() override {}
  void OnPinnedGroupsChanged() override;

 private:
  // Gives every pinned group of the window a header and a line view, and removes the views of
  // groups that are gone. Views can't be added during a layout, so a layout schedules it.
  void Sync();
  void ScheduleSync();
  // During a drag: puts each dragged tab into the group it is dragged over, or out of its group
  // (lb::DraggedTabInPinnedGroup). `tab_spans` are the groups' shown tabs, the dragged ones left
  // out; a dragged tab's own group counts its place.
  void DecideDraggedGroups(const views::ProposedLayout& layout,
                           const std::map<std::string, gfx::Rect>& tab_spans) const;

  raw_ref<views::View> container_;
  base::WeakPtr<SidebarController> controller_;
  std::vector<Row> rows_;
  // Per group id: its header and line views (children of the container).
  std::map<std::string, raw_ptr<PinnedGroupHeaderView>> headers_;
  std::map<std::string, raw_ptr<views::View>> lines_;
  // For each child of the container: its group id (empty: none), and its first tab.
  std::vector<std::string> child_groups_;
  std::vector<tabs::TabHandle> child_tabs_;
  // The dragged children in this layout: where they are drawn, and their places.
  struct Dragged {
    gfx::Rect drawn;
    gfx::Rect slot;
  };
  std::map<size_t, Dragged> dragged_;
  // The group dragged by its header, whose tabs fold away until the drop.
  std::string dragging_group_;
  bool sync_pending_ = false;
  base::WeakPtrFactory<PinnedGroupLayout> weak_factory_{this};
};

// A pinned group `id` dropped in the sidebar at `point_in_screen` (docs/ux.md §6.1), as
// lb::DecideGroupDrop says: above the middle of the "+ New Tab" row it moves among the pinned
// tabs, below it it becomes a today group among the today tabs; either way it lands between
// groups (lb::GroupDropIndex). Returns false when nothing happens, as for a group whose tabs
// aren't in the tab strip.
bool DropPinnedGroup(SidebarController* controller,
                     const std::string& id,
                     const gfx::Point& point_in_screen);
// While a pinned group `id` is dragged by its header to `point_in_screen`: a line shows where it
// would land, among the pinned or the today tabs.
void ShowGroupDropLine(SidebarController* controller,
                       const std::string& id,
                       const gfx::Point& point_in_screen);
// The line, while one shows.
views::Widget* GetGroupDropLineForTesting();
// The hook for a drag of today groups (`dragged`: every dragged tab of `model`) that Chromium
// moves to `point_in_screen` (patches/lb/pinned-groups.patch): over the pinned area, a line shows
// where they would be pinned.
void OnTabGroupDragged(TabStripModel* model,
                       const std::vector<tabs::TabHandle>& dragged,
                       const gfx::Point& point_in_screen);
// The hook for the end of any tab drag (patches/lb/pinned-groups.patch): the line goes, and a
// cancelled drag gives its tabs back the pinned groups they had (PinnedGroupTracker::EndTabDrag).
void OnTabDragEnded(bool cancelled);

// The hook for a drag of today groups `groups` (with `dragged`, every dragged tab) that Chromium
// just dropped (patches/lb/pinned-groups.patch): dropped above the middle of the "+ New Tab" row,
// they become pinned there, in order, the groups as pinned groups (lb::DecideGroupDrop).
void OnTabGroupDropped(TabStripModel* model,
                       const std::set<tab_groups::TabGroupId>& groups,
                       const std::vector<tabs::TabHandle>& dragged,
                       const gfx::Point& point_in_screen);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_SIDEBAR_PINNED_GROUP_LAYOUT_H_
