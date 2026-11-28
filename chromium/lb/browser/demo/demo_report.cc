// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/demo/demo_report.h"

#include "base/logging.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/common/tab_drag_handler.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/shared/tab_strip_types.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "lb/browser/ui/views/importer/import_dialog.h"
#include "lb/browser/ui/views/password/password_manager_choice_dialog.h"
#include "lb/browser/ui/views/search/search_choice_dialog.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"
#include "ui/views/view_utils.h"

namespace lb {

void SkipFirstRunQuestionsForDemo(Profile* profile) {
  g_browser_process->local_state()->SetBoolean(kImportOfferedPref, true);
  profile->GetPrefs()->SetBoolean(kSearchChoiceShownPref, true);
  profile->GetPrefs()->SetBoolean(kPasswordManagerChoiceShownPref, true);
}

void ClickViewForDemo(views::View* view, std::optional<gfx::Point> point) {
  const gfx::Point at = point.value_or(view->GetLocalBounds().CenterPoint());
  view->OnMousePressed(ui::MouseEvent(ui::EventType::kMousePressed, at, at, ui::EventTimeForNow(),
                                      ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON));
  view->OnMouseReleased(ui::MouseEvent(ui::EventType::kMouseReleased, at, at, ui::EventTimeForNow(),
                                       ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON));
}

std::vector<views::View*> ViewTreeForDemo(views::View* root) {
  std::vector<views::View*> views;
  std::vector<views::View*> stack;
  if (root) {
    stack.push_back(root);
  }
  while (!stack.empty()) {
    views::View* view = stack.back();
    stack.pop_back();
    views.push_back(view);
    for (views::View* child : view->children()) {
      stack.push_back(child);
    }
  }
  return views;
}

content::WebContents* ActiveContentsForDemo(BrowserWindowInterface* browser) {
  tabs::TabInterface* tab = browser ? browser->GetActiveTabInterface() : nullptr;
  return tab ? tab->GetContents() : nullptr;
}

views::View* WindowViewForDemo(BrowserWindowInterface* browser) {
  return BrowserView::GetBrowserViewForBrowser(browser);
}

TabView* FindTabViewForDemo(BrowserWindowInterface* browser, const tabs::TabInterface* tab) {
  if (!tab) {
    return nullptr;
  }
  for (TabView* row : FindViewsForDemo<TabView>(WindowViewForDemo(browser))) {
    if (row->collection_node() && row->GetTabInterface() == tab) {
      return row;
    }
  }
  return nullptr;
}

void SimulateTabDragForDemo(BrowserWindowInterface* browser,
                            tabs::TabInterface* tab,
                            const std::vector<int>& screen_ys,
                            bool drop) {
  TabView* row = FindTabViewForDemo(browser, tab);
  if (!row) {
    VLOG(1) << "demo drag: no row";
    return;
  }
  SimulateViewDragForDemo(browser, row, screen_ys, drop);
}

namespace {

// The drag handler of `browser`'s tab strip, through any of its tab rows.
TabDragHandler* DragHandlerForDemo(BrowserWindowInterface* browser) {
  for (TabView* row : FindViewsForDemo<TabView>(WindowViewForDemo(browser))) {
    if (row->collection_node()) {
      return &const_cast<TabStripCollectionController*>(row->collection_node()->GetController())
                  ->GetDragHandler();
    }
  }
  return nullptr;
}

}  // namespace

bool StartViewDragForDemo(views::View* view, const std::vector<int>& screen_ys) {
  const gfx::Point start = view->GetLocalBounds().CenterPoint();
  ui::MouseEvent press(ui::EventType::kMousePressed, start, start, ui::EventTimeForNow(),
                       ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  view->OnMousePressed(press);
  return ContinueViewDragForDemo(view, screen_ys);
}

bool ContinueViewDragForDemo(views::View* view, const std::vector<int>& screen_ys) {
  views::ViewTracker tracker(view);
  const int screen_x = view->GetBoundsInScreen().CenterPoint().x();
  for (int y : screen_ys) {
    if (!tracker.view()) {
      VLOG(1) << "demo drag: view destroyed mid-drag";
      return false;
    }
    gfx::Point point(screen_x, y);
    views::View::ConvertPointFromScreen(view, &point);
    ui::MouseEvent drag(ui::EventType::kMouseDragged, point, point, ui::EventTimeForNow(),
                        ui::EF_LEFT_MOUSE_BUTTON, 0);
    view->OnMouseDragged(drag);
  }
  return tracker.view() != nullptr;
}

void EndTabDragForDemo(BrowserWindowInterface* browser, bool drop) {
  if (TabDragHandler* handler = DragHandlerForDemo(browser)) {
    handler->EndDrag(drop ? EndDragReason::kComplete : EndDragReason::kCancel);
  }
}

void SimulateViewDragForDemo(BrowserWindowInterface* browser,
                             views::View* view,
                             const std::vector<int>& screen_ys,
                             bool drop) {
  if (StartViewDragForDemo(view, screen_ys)) {
    EndTabDragForDemo(browser, drop);
  }
}

std::vector<int> DragPathForDemo(int from, int to) {
  std::vector<int> ys;
  for (int step = 1; step <= 6; ++step) {
    ys.push_back(from + (to - from) * step / 6);
  }
  return ys;
}

}  // namespace lb
