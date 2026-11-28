// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/sidebar/sidebar_edits_demo.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/find_bar/find_bar.h"
#include "chrome/browser/ui/find_bar/find_bar_controller.h"
#include "components/find_in_page/find_types.h"
#include "chrome/browser/ui/startup/startup_tab.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/tabs/tab_menu_model_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/profiles/avatar_toolbar_button.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/hovercard/tab_hover_card_bubble_view.h"
#include "chrome/browser/ui/views/tabs/shared/tab_strip_combo_button.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_top_container.h"
#include "chrome/browser/ui/views/tabs/tab/tab_accessibility.h"
#include "chrome/browser/ui/views/tabs/tab/tab_title.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "lb/browser/demo/demo_report.h"
#include "lb/browser/tabs/sidebar_width.h"
#include "lb/browser/tabs/tab_names.h"
#include "lb/browser/ui/lb_ui_snapshot.h"
#include "lb/browser/ui/tabs/pinned_tab_sleep.h"
#include "lb/browser/ui/tabs/tab_custom_data.h"
#include "lb/browser/ui/views/command_bar/command_bar.h"
#include "lb/browser/ui/views/frame/load_progress_line.h"
#include "lb/browser/ui/views/little_window/little_window.h"
#include "lb/browser/ui/views/sidebar/new_tab_row_view.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "lb/browser/ui/views/sidebar/sidebar_toolbar.h"
#include "lb/browser/ui/views/frame/contents_card.h"
#include "ui/base/base_window.h"
#include "ui/base/hit_test.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/toolbar/reload_button.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/views/window/non_client_view.h"

namespace lb {

namespace {

constexpr char kHiddenMode[] = "hidden-sidebar";
constexpr char kNamesMode[] = "tab-names";
constexpr char kProgressMode[] = "load-progress";
constexpr char kOtherAppsMode[] = "other-apps";
// --lb-sidebar-demo=pinned-fresh --lb-demo-url=<url>: <url> is an HTTP page the cache may keep
// for long (Cache-Control: max-age) whose title changes with each response.
constexpr char kFreshMode[] = "pinned-fresh";
constexpr char kToolbarMode[] = "sidebar-toolbar";
constexpr char kSnapshotSwitch[] = "lb-ui-snapshot-sidebar";
constexpr char kProgressSnapshotSwitch[] = "lb-ui-snapshot-load-progress";

// --------------------------------------------------------------- hidden-sidebar

VerticalTabStripRegionView* Region(BrowserWindowInterface* browser) {
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser);
  return view ? view->vertical_tab_strip_region_view_for_testing() : nullptr;
}

// The page's left edge in the window.
int PageLeft(BrowserWindowInterface* browser) {
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser);
  return view && view->multi_contents_view() ? view->multi_contents_view()->bounds().x() : -1;
}

struct HiddenRun {
  base::WeakPtr<BrowserWindowInterface> browser;
  std::string detail;
  bool ok = true;
  int width = 0;  // the sidebar's width before the drag
  std::unique_ptr<ExpandOnHoverLock> slide_in;

  void Expect(bool condition, const std::string& what) {
    if (!condition) {
      ok = false;
      detail += " FAIL(" + what + ")";
    }
  }
  std::string State(VerticalTabStripRegionView* region) {
    return "strip=" + base::NumberToString(region->width()) +
           " opacity=" + base::NumberToString(region->layer()->opacity()) +
           " page_x=" + base::NumberToString(PageLeft(browser.get()));
  }
};

HiddenRun& TheHiddenRun() {
  static base::NoDestructor<HiddenRun> run;
  return *run;
}

void HiddenWithoutHover();
void HiddenFromOutside();
void HiddenLeftOutside();

void HiddenSlidOut() {
  HiddenRun& run = TheHiddenRun();
  VerticalTabStripRegionView* region = run.browser ? Region(run.browser.get()) : nullptr;
  if (!region) {
    ReportDemo(kHiddenMode, false, "the window went away");
    return;
  }
  run.detail += " | let go: " + run.State(region);
  run.Expect(region->width() == kHiddenSidebarEdgeWidth, "slides out to the edge band");
  run.Expect(region->layer()->opacity() == 0.0f, "not drawn once slid out");
  // The pointer resting just left of the window (lb::InSidebarOutsideZone) slides it in too, in
  // the active window.
  run.browser->GetWindow()->Activate();
  region->LbSetPointerNearEdgeForTesting(true);
  PostDemoTask(base::BindOnce(&HiddenFromOutside), base::Milliseconds(1500));
}

void HiddenFromOutside() {
  HiddenRun& run = TheHiddenRun();
  VerticalTabStripRegionView* region = run.browser ? Region(run.browser.get()) : nullptr;
  if (!region) {
    ReportDemo(kHiddenMode, false, "the window went away");
    return;
  }
  // Chromium slides it in only while the window is active. If macOS kept another app in front
  // (someone is using the Mac), the check can't run: say so rather than fail.
  run.detail += " | from outside: " + run.State(region);
  if (region->GetWidget()->ShouldPaintAsActive()) {
    run.Expect(region->is_expanded_on_hover(), "slides in from left of the window");
  } else {
    run.detail += " (not checked: the window isn't active)";
  }
  // The pointer leaves that zone (and isn't over the sidebar): it slides out.
  region->LbSetPointerNearEdgeForTesting(false);
  PostDemoTask(base::BindOnce(&HiddenLeftOutside), base::Milliseconds(1500));
}

void HiddenLeftOutside() {
  HiddenRun& run = TheHiddenRun();
  VerticalTabStripRegionView* region = run.browser ? Region(run.browser.get()) : nullptr;
  if (!region) {
    ReportDemo(kHiddenMode, false, "the window went away");
    return;
  }
  run.detail += " | left outside: " + run.State(region);
  run.Expect(!region->is_expanded_on_hover() && region->width() == kHiddenSidebarEdgeWidth,
             "slides out once the pointer leaves the zone");
  region->LbSetPointerNearEdgeForTesting(std::nullopt);
  // With expand on hover off nothing would slide it in: it stays Chromium's column of icons.
  if (auto* state = tabs::VerticalTabStripStateController::From(run.browser.get())) {
    state->SetExpandOnHoverEnabled(false);
  }
  PostDemoTask(base::BindOnce(&HiddenWithoutHover), base::Seconds(1));
}

void HiddenShownAgain() {
  HiddenRun& run = TheHiddenRun();
  BrowserView* view =
      run.browser ? BrowserView::GetBrowserViewForBrowser(run.browser.get()) : nullptr;
  if (!view) {
    ReportDemo(kHiddenMode, false, "the window went away");
    return;
  }
  run.Expect(IsToolbarInSidebar(view->toolbar()), "toolbar back in the shown sidebar");
  ReportDemo(kHiddenMode, run.ok, run.detail);
}

void HiddenWithoutHover() {
  HiddenRun& run = TheHiddenRun();
  VerticalTabStripRegionView* region = run.browser ? Region(run.browser.get()) : nullptr;
  auto* state =
      run.browser ? tabs::VerticalTabStripStateController::From(run.browser.get()) : nullptr;
  if (!region || !state) {
    ReportDemo(kHiddenMode, false, "the window went away");
    return;
  }
  run.detail += " | no hover: " + run.State(region);
  run.Expect(region->width() == tabs::kVerticalTabStripCollapsedWidth,
             "Chromium's column without expand on hover");
  run.Expect(region->layer()->opacity() == 1.0f, "drawn without expand on hover");
  run.Expect(PageLeft(run.browser.get()) >= tabs::kVerticalTabStripCollapsedWidth - 1,
             "the column takes its room");
  // The toolbar doesn't fit the column: it is Chromium's row above the page, and the column has
  // its toggle back, drawn inside it; the window buttons are on that row, off the page.
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(run.browser.get());
  views::View* toggle = region->GetTopContainer()->GetCollapseButton();
  const gfx::Rect toggle_bounds = views::View::ConvertRectToTarget(
      toggle, region, toggle->GetLocalBounds());
  const gfx::Rect close = CloseButtonBoundsForTesting(region->GetWidget());
  const gfx::Rect card =
      view->multi_contents_view()->ConvertRectToWidget(view->multi_contents_view()->GetLocalBounds());
  run.detail += " toggle=" + toggle_bounds.ToString() + " close=" + close.ToString() +
                " card=" + card.ToString();
  run.Expect(!IsToolbarInSidebar(view->toolbar()) &&
                 view->toolbar()->parent() == view->top_container(),
             "toolbar above the page in the column");
  run.Expect(toggle->parent() == region->GetTopContainer() && toggle->IsDrawn() &&
                 region->GetLocalBounds().Contains(toggle_bounds),
             "the column's toggle in it");
  run.Expect(WindowButtonsShownForTesting(region->GetWidget()) && !close.Intersects(card),
             "window buttons off the page");
  // Leave the sidebar as it was.
  state->SetExpandOnHoverEnabled(true);
  state->SetUncollapsedWidth(run.width);
  state->RequestCollapse(false);
  PostDemoTask(base::BindOnce(&HiddenShownAgain), base::Seconds(1));
}

void HiddenResized() {
  HiddenRun& run = TheHiddenRun();
  VerticalTabStripRegionView* region = run.browser ? Region(run.browser.get()) : nullptr;
  auto* state =
      run.browser ? tabs::VerticalTabStripStateController::From(run.browser.get()) : nullptr;
  if (!region || !state) {
    ReportDemo(kHiddenMode, false, "the window went away");
    return;
  }
  const int wider = SlidInSidebarWidth(run.width + 300, tabs::kVerticalTabStripUncollapsedMinWidth,
                                       tabs::kVerticalTabStripUncollapsedMaxWidth);
  // Wider than Chromium's 400 px (patches/lb/sidebar-max-width.patch).
  run.Expect(wider > 400, "wider than 400");
  run.detail += " | dragged: " + run.State(region) +
                " saved_width=" + base::NumberToString(state->GetUncollapsedWidth());
  run.Expect(region->width() == wider, "widened in place");
  run.Expect(state->GetUncollapsedWidth() == wider, "the new width is the sidebar's");
  run.Expect(state->IsCollapsed(), "still hidden");
  run.Expect(PageLeft(run.browser.get()) == kHiddenSidebarEdgeWidth, "the page stays put under it");
  // Its edge can be dragged again: the resize area stays while it is slid in.
  run.Expect(region->resize_area_for_testing()->GetVisible(), "resize area kept after a drag");
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(kSnapshotSwitch)) {
    // Keep it slid in for the capture, which quits the app.
    ReportDemo(kHiddenMode, run.ok, run.detail + " (captured slid in)");
    MaybeScheduleUiSnapshot(region->GetWidget(), kSnapshotSwitch);
    return;
  }
  // Let go, and the pointer leaves it (the real pointer may rest over it, which keeps it out).
  run.slide_in.reset();
  region->OnMouseExited(ui::MouseEvent(ui::EventType::kMouseExited, gfx::PointF(), gfx::PointF(),
                                       base::TimeTicks::Now(), ui::EF_NONE, ui::EF_NONE));
  PostDemoTask(base::BindOnce(&HiddenSlidOut), base::Milliseconds(1500));
}

void HiddenSlidIn() {
  HiddenRun& run = TheHiddenRun();
  VerticalTabStripRegionView* region = run.browser ? Region(run.browser.get()) : nullptr;
  if (!region) {
    ReportDemo(kHiddenMode, false, "the window went away");
    return;
  }
  run.detail += " | slid in: " + run.State(region);
  run.Expect(region->is_expanded_on_hover(), "slid in");
  run.Expect(region->width() == run.width, "slides in to the sidebar's width");
  run.Expect(region->layer()->opacity() == 1.0f, "drawn while slid in");
  run.Expect(PageLeft(run.browser.get()) == kHiddenSidebarEdgeWidth,
             "over the page, which doesn't move");
  // Its top row is the shown sidebar's: one row, the toolbar's (the toggle, back, forward, reload
  // and Tab Search) beside the window controls, not Chromium's column for a collapsed strip.
  VerticalTabStripTopContainer* top = region->GetTopContainer();
  views::View* toggle = top ? top->GetCollapseButton() : nullptr;
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(run.browser.get());
  ToolbarView* toolbar = view ? view->toolbar() : nullptr;
  const gfx::Rect close = CloseButtonBoundsForTesting(region->GetWidget());
  const gfx::Rect toggle_bounds =
      toggle ? toggle->ConvertRectToWidget(toggle->GetLocalBounds()) : gfx::Rect();
  const gfx::Rect reload = toolbar ? toolbar->reload_button()->ConvertRectToWidget(
                                         toolbar->reload_button()->GetLocalBounds())
                                   : gfx::Rect();
  run.detail += " close=" + close.ToString() + " toggle=" + toggle_bounds.ToString() +
                " reload=" + reload.ToString();
  run.Expect(toggle && toggle->GetVisible() && toggle->parent() == toolbar &&
                 std::abs(toggle_bounds.CenterPoint().y() - reload.CenterPoint().y()) <= 1 &&
                 std::abs(close.CenterPoint().y() - reload.CenterPoint().y()) <= 3,
             "top row is one row");
  run.Expect(toggle && toggle_bounds.x() >= close.right(), "toggle right of the window controls");
  // Drag its edge 300 px to the right, as the resize area does.
  region->OnResize(150, /*done_resizing=*/false);
  region->OnResize(300, /*done_resizing=*/true);
  PostDemoTask(base::BindOnce(&HiddenResized), base::Seconds(1));
}

void HiddenCollapsed() {
  HiddenRun& run = TheHiddenRun();
  VerticalTabStripRegionView* region = run.browser ? Region(run.browser.get()) : nullptr;
  auto* state =
      run.browser ? tabs::VerticalTabStripStateController::From(run.browser.get()) : nullptr;
  if (!region || !state) {
    ReportDemo(kHiddenMode, false, "the window went away");
    return;
  }
  run.width = state->GetUncollapsedWidth();
  run.detail = "hidden: " + run.State(region);
  // A width saved before the sidebar's limits changed (Chromium's 240) comes back within them,
  // so it slides in at a width its row fits in.
  state->SetUncollapsedWidth(240);
  run.Expect(state->GetUncollapsedWidth() == tabs::kVerticalTabStripUncollapsedMinWidth,
             "an old narrow width comes back at the minimum");
  state->SetUncollapsedWidth(run.width);
  run.Expect(state->IsCollapsed(), "hidden");
  run.Expect(region->width() == kHiddenSidebarEdgeWidth, "only the edge band");
  run.Expect(region->layer()->opacity() == 0.0f, "not drawn");
  run.Expect(PageLeft(run.browser.get()) == kHiddenSidebarEdgeWidth,
             "the page leaves only the edge band");
  // What the pointer at the window's edge does.
  run.slide_in = region->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepExpanded);
  PostDemoTask(base::BindOnce(&HiddenSlidIn), base::Milliseconds(1500));
}

void RunHiddenSidebarDemo(BrowserWindowInterface* browser) {
  FailDemoAfter(kHiddenMode, base::Seconds(30));
  auto* state = tabs::VerticalTabStripStateController::From(browser);
  if (!state || !Region(browser)) {
    ReportDemo(kHiddenMode, false, "no vertical tab strip");
    return;
  }
  TheHiddenRun().browser = browser->GetWeakPtr();
  state->RequestCollapse(true);
  PostDemoTask(base::BindOnce(&HiddenCollapsed), base::Seconds(2));
}

// --------------------------------------------------------------- tab-names

const GURL& PageA() {
  static const base::NoDestructor<GURL> url("data:text/html,<title>Alpha page</title>A");
  return *url;
}
const GURL& PageB() {
  static const base::NoDestructor<GURL> url("data:text/html,<title>Beta page</title>B");
  return *url;
}

// A menu delegate that runs nothing: the demo only reads the menu's items.
class NoMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  void ExecuteCommand(int command_id, int event_flags) override {}
};

struct NamesRun {
  base::WeakPtr<BrowserWindowInterface> browser;
  tabs::TabHandle tab;
  raw_ptr<const content::WebContents> contents_before_sleep = nullptr;
  bool only_tab = false;  // the window's only tab, so its drag must not move the window
  gfx::Rect window_before_drag;
  std::string detail;
  bool ok = true;

  void Expect(bool condition, const std::string& what) {
    detail += " " + what + "=" + (condition ? "1" : "0");
    ok = ok && condition;
  }
  TabStripModel* Model() { return browser ? browser->GetTabStripModel() : nullptr; }
  int Index() {
    TabStripModel* model = Model();
    return model && tab.Get() ? model->GetIndexOfTab(tab.Get()) : -1;
  }
  // The tab menu's items for names and links, as "ids:enabled", e.g. "900:1 901:1 902:0".
  std::string MenuItems() {
    TabStripModel* model = Model();
    if (!model || Index() < 0) {
      return "none";
    }
    NoMenuDelegate delegate;
    TabMenuModel menu(&delegate, TabMenuModelDelegate::From(browser.get()), model, Index());
    std::string items;
    for (size_t i = 0; i < menu.GetItemCount(); ++i) {
      const int id = menu.GetCommandIdAt(i);
      if (TabNameCommandFromId(id)) {
        items += (items.empty() ? "" : " ") + base::NumberToString(id) + ":" +
                 (model->IsContextMenuCommandEnabled(
                      Index(), static_cast<TabStripModel::ContextMenuCommand>(id))
                      ? "1"
                      : "0");
      }
    }
    return items;
  }
  void Run(TabNameCommand command) {
    if (TabStripModel* model = Model(); model && Index() >= 0) {
      model->ExecuteContextMenuCommand(
          Index(), static_cast<TabStripModel::ContextMenuCommand>(TabNameCommandId(command)));
    }
  }
  std::u16string RowTitle() {
    TabView* row = browser && tab.Get() ? FindTabViewForDemo(browser.get(), tab.Get()) : nullptr;
    return row && row->title_for_testing() ? std::u16string(row->title_for_testing()->GetText())
                                           : u"(no row)";
  }
};

NamesRun& TheNamesRun() {
  static base::NoDestructor<NamesRun> run;
  return *run;
}

void Load(tabs::TabInterface* tab, const GURL& url) {
  tab->GetContents()->GetController().LoadURL(url, content::Referrer(), ui::PAGE_TRANSITION_TYPED,
                                              std::string());
}

void NamesDragAcross();
void NamesDragged();
void NamesSlept();

void NamesFinish() {
  NamesRun& run = TheNamesRun();
  if (!run.browser || !run.tab.Get()) {
    ReportDemo(kNamesMode, false, "the tab went away");
    return;
  }
  // Shown again, the tab closed away from its link loads its link, not the page it was on.
  run.Expect(CurrentUrlOf(run.tab.Get()) == PageB(), "woke_at_link");
  run.Expect(!IsAsleep(*run.tab.Get()), "awake");
  run.Expect(!GetTabCustom(run.tab.Get()).wakes_at_link, "wakes_at_link_cleared");
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(kSnapshotSwitch)) {
    ReportDemo(kNamesMode, run.ok, run.detail + " (captured)");
    MaybeScheduleUiSnapshot(
        views::Widget::GetWidgetForNativeWindow(run.browser->GetWindow()->GetNativeWindow()),
        kSnapshotSwitch);
    return;
  }
  ReportDemo(kNamesMode, run.ok, run.detail);
}

void NamesSaved() {
  NamesRun& run = TheNamesRun();
  if (!run.browser || !run.tab.Get()) {
    ReportDemo(kNamesMode, false, "the tab went away");
    return;
  }
  // The space's saved tabs carry the name, the link and that it wakes at its link.
  SidebarController* controller = SidebarController::FromBrowser(run.browser.get());
  const base::DictValue& all = g_browser_process->local_state()->GetDict("lb.space_tabs");
  const base::ListValue* saved =
      controller ? all.FindList(controller->GetActiveSpaceId()) : nullptr;
  bool saved_ok = false;
  const base::ListValue empty;
  for (const base::Value& record : saved ? *saved : empty) {
    const base::DictValue* dict = record.GetIfDict();
    const std::string* name = dict ? dict->FindString("name") : nullptr;
    const std::string* link = dict ? dict->FindString("pinned_url") : nullptr;
    saved_ok = saved_ok || (name && *name == "Docs" && link && GURL(*link) == PageB() &&
                            dict->FindBool("wakes_at_link").value_or(false));
  }
  run.Expect(saved_ok, "saved_with_space");
  // Show it again, as a click on its row does.
  run.Model()->ActivateTabAt(
      run.Index(), TabStripUserGestureDetails(TabStripUserGestureDetails::GestureType::kOther));
  PostDemoTask(base::BindOnce(&NamesFinish), base::Seconds(2));
}

void NamesUsedPage() {
  NamesRun& run = TheNamesRun();
  if (!run.tab.Get()) {
    ReportDemo(kNamesMode, false, "the tab went away");
    return;
  }
  run.Expect(GetTabCustom(run.tab.Get()).pinned_url == PageB(), "page_became_link");
  run.Expect(!HasLeftPinnedUrl(run.tab.Get()), "on_link_again");
  run.detail += " menu_on_link=[" + run.MenuItems() + "]";
  // Leave the link, then drag the tab below "+ New Tab" and back up: the drag unpins and pins it
  // again on the way, which must not touch its link.
  Load(run.tab.Get(), PageA());
  PostDemoTask(base::BindOnce(&NamesDragAcross), base::Seconds(2));
}

void NamesDragAcross() {
  NamesRun& run = TheNamesRun();
  TabView* row =
      run.browser && run.tab.Get() ? FindTabViewForDemo(run.browser.get(), run.tab.Get()) : nullptr;
  std::vector<NewTabRowView*> rows =
      run.browser ? FindViewsForDemo<NewTabRowView>(WindowViewForDemo(run.browser.get()))
                  : std::vector<NewTabRowView*>();
  if (!row || rows.empty()) {
    ReportDemo(kNamesMode, false, run.detail + " no row to drag, or no + New Tab row");
    return;
  }
  const int from = row->GetBoundsInScreen().CenterPoint().y();
  const int below = rows.front()->GetBoundsInScreen().bottom() + 30;
  run.window_before_drag = run.browser->GetWindow()->GetBounds();
  // Below "+ New Tab" the tab is unpinned; back up, pinned again. The tab moves, not the window
  // (dragging a window's only tab moved the window in Chromium: patches/lb/no-tab-detach.patch).
  views::ViewTracker tracker(row);
  const bool down = StartViewDragForDemo(row, DragPathForDemo(from, below));
  run.Expect(down && run.tab.Get() && !run.tab.Get()->IsPinned(), "unpinned_below_new_tab");
  const bool up = down && tracker.view() &&
                  ContinueViewDragForDemo(tracker.view(), DragPathForDemo(below, from));
  run.Expect(up, "dragged_back_up");
  EndTabDragForDemo(run.browser.get(), /*drop=*/true);
  PostDemoTask(base::BindOnce(&NamesDragged), base::Seconds(1));
}

void NamesDragged() {
  NamesRun& run = TheNamesRun();
  if (!run.tab.Get()) {
    ReportDemo(kNamesMode, false, "the tab went away");
    return;
  }
  run.Expect(run.only_tab, "only_tab");
  run.Expect(run.browser && run.browser->GetWindow()->GetBounds() == run.window_before_drag,
             "window_stays_put");
  run.Expect(run.tab.Get()->IsPinned(), "pinned_after_drag");
  run.Expect(GetTabCustom(run.tab.Get()).pinned_url == PageB(), "link_kept_through_drag");
  // Put it to sleep, as closing it does: it gets a new page, and keeps its name and link.
  run.contents_before_sleep = run.tab.Get()->GetContents();
  SleepPinnedTabs(*run.Model(), {run.tab.Get()});
  PostDemoTask(base::BindOnce(&NamesSlept), base::Seconds(2));
}

void NamesSlept() {
  NamesRun& run = TheNamesRun();
  if (!run.tab.Get()) {
    ReportDemo(kNamesMode, false, "the tab went away");
    return;
  }
  run.Expect(IsAsleep(*run.tab.Get()), "asleep");
  run.detail += std::string(" new_page_when_asleep=") +
                (run.tab.Get()->GetContents() != run.contents_before_sleep ? "1" : "0");
  run.Expect(run.RowTitle() == u"Docs", "name_kept_asleep");
  // Unnamed, asleep away from its link (page B), it would show its link's title again.
  run.Expect(TabTitleWithoutName(run.tab.Get(), u"Alpha page") == u"Beta page",
             "unnamed_asleep_shows_link_title");
  run.Expect(GetTabCustom(run.tab.Get()).pinned_url == PageB(), "link_kept_asleep");
  run.Expect(
      GetTabCustomSessionData(run.tab.Get()->GetContents()).find("Docs") != std::string::npos,
      "session_data_for_new_page");
  // Closed on page A, away from its link (page B): it wakes at its link.
  run.Expect(GetTabCustom(run.tab.Get()).wakes_at_link, "wakes_at_link");
  run.Expect(GetTabCustomSessionData(run.tab.Get()->GetContents()).find("wakes_at_link") !=
                 std::string::npos,
             "wakes_at_link_in_session");
  PostDemoTask(base::BindOnce(&NamesSaved), base::Seconds(3));  // the space saves its tabs
}

void NamesLeftAgain() {
  NamesRun& run = TheNamesRun();
  if (!run.tab.Get()) {
    ReportDemo(kNamesMode, false, "the tab went away");
    return;
  }
  run.Run(TabNameCommand::kUsePageAsPinnedUrl);
  PostDemoTask(base::BindOnce(&NamesUsedPage), base::Seconds(1));
}

void NamesWentBack() {
  NamesRun& run = TheNamesRun();
  if (!run.tab.Get()) {
    ReportDemo(kNamesMode, false, "the tab went away");
    return;
  }
  run.Expect(CurrentUrlOf(run.tab.Get()) == PageA(), "back_to_link");
  run.Expect(!HasLeftPinnedUrl(run.tab.Get()), "on_link");
  Load(run.tab.Get(), PageB());
  PostDemoTask(base::BindOnce(&NamesLeftAgain), base::Seconds(2));
}

void NamesLeftLink() {
  NamesRun& run = TheNamesRun();
  if (!run.tab.Get()) {
    ReportDemo(kNamesMode, false, "the tab went away");
    return;
  }
  run.Expect(HasLeftPinnedUrl(run.tab.Get()), "left_link");
  const std::string menu = run.MenuItems();
  run.detail += " menu_left=[" + menu + "]";
  run.Expect(menu == "900:1 901:1 902:1", "menu_left_ok");
  run.Expect(run.RowTitle() == u"Docs", "name_kept_on_other_page");
  // Its link keeps its title; unnamed and open, the row would follow the page it shows
  // (docs/ux.md §6).
  run.Expect(GetTabCustom(run.tab.Get()).pinned_title == u"Alpha page", "link_title_kept");
  run.Expect(TabTitleWithoutName(run.tab.Get(), u"Beta page") == u"Beta page",
             "unnamed_follows_page");
  Load(run.tab.Get(), PageA());
  PostDemoTask(base::BindOnce(&NamesWentBack), base::Seconds(2));
}

void NamesPinned() {
  NamesRun& run = TheNamesRun();
  if (!run.tab.Get()) {
    ReportDemo(kNamesMode, false, "the tab went away");
    return;
  }
  run.Expect(run.tab.Get()->IsPinned(), "pinned");
  run.Expect(GetTabCustom(run.tab.Get()).pinned_url == PageA(), "link_is_page_when_pinned");
  const std::string menu = run.MenuItems();
  run.detail += " menu_pinned=[" + menu + "]";
  run.Expect(menu == "900:1 901:1 902:0", "menu_pinned_ok");
  Load(run.tab.Get(), PageB());
  PostDemoTask(base::BindOnce(&NamesLeftLink), base::Seconds(2));
}

void NamesRenamedInBar() {
  NamesRun& run = TheNamesRun();
  if (!run.tab.Get()) {
    ReportDemo(kNamesMode, false, "the tab went away");
    return;
  }
  run.Expect(run.RowTitle() == u"Docs", "renamed_in_bar");
  // VoiceOver reads it by its name too.
  run.Expect(tabs::GetAccessibleTabLabel(run.tab.Get(), /*is_for_tab=*/true).find(u"Docs") !=
                 std::u16string::npos,
             "accessible_name");
  run.Expect(
      GetTabCustomSessionData(run.tab.Get()->GetContents()).find("Docs") != std::string::npos,
      "session_data");
  run.Model()->SetTabPinned(run.Index(), true);
  PostDemoTask(base::BindOnce(&NamesPinned), base::Seconds(1));
}

void NamesBarOpen() {
  NamesRun& run = TheNamesRun();
  const std::optional<std::u16string> text = CommandBarTextForTesting();
  run.detail += " bar_text=" + (text ? base::UTF16ToUTF8(*text) : std::string("(no bar)"));
  run.Expect(text == u"My Notes", "bar_starts_with_name");
  run.Expect(RunFirstCommandBarResultForTesting(u"  Docs "), "bar_ran");
  PostDemoTask(base::BindOnce(&NamesRenamedInBar), base::Seconds(1));
}

void NamesRenamed() {
  NamesRun& run = TheNamesRun();
  if (!run.tab.Get()) {
    ReportDemo(kNamesMode, false, "the tab went away");
    return;
  }
  run.Expect(run.RowTitle() == u"My Notes", "row_shows_name");
  const std::string menu = run.MenuItems();
  run.detail += " menu_today=[" + menu + "]";
  run.Expect(menu == "900:1", "menu_today_ok");
  run.Run(TabNameCommand::kRename);  // opens the command bar on the tab's name
  PostDemoTask(base::BindOnce(&NamesBarOpen), base::Seconds(1));
}

void NamesLoaded() {
  NamesRun& run = TheNamesRun();
  if (!run.tab.Get()) {
    ReportDemo(kNamesMode, false, "the tab went away");
    return;
  }
  run.Expect(run.RowTitle() == u"Alpha page", "row_shows_page_title");
  RenameTab(run.tab.Get(), u"  My \n Notes ");
  PostDemoTask(base::BindOnce(&NamesRenamed), base::Milliseconds(500));
}

void RunTabNamesDemo(BrowserWindowInterface* browser) {
  FailDemoAfter(kNamesMode, base::Seconds(40));
  tabs::TabInterface* tab = browser->GetTabStripModel()->GetActiveTab();
  if (!tab) {
    ReportDemo(kNamesMode, false, "no tab");
    return;
  }
  if (tab->IsPinned()) {
    browser->GetTabStripModel()->SetTabPinned(browser->GetTabStripModel()->GetIndexOfTab(tab),
                                              false);
  }
  SetTabCustom(tab, TabCustom());  // a name left by an earlier run
  NamesRun& run = TheNamesRun();
  // The window's only tab: its drag across "+ New Tab" below moves the tab, not the window
  // (patches/lb/no-tab-detach.patch). Tabs left by an earlier run go.
  TabStripModel* model = browser->GetTabStripModel();
  for (int tries = model->count(); tries > 0 && model->count() > 1; --tries) {
    tabs::TabInterface* other = model->GetTabAtIndex(model->GetTabAtIndex(0) == tab ? 1 : 0);
    if (other->IsPinned()) {
      model->SetTabPinned(model->GetIndexOfTab(other), false);  // closed for good, not asleep
    }
    model->CloseWebContentsAt(model->GetIndexOfTab(other), TabCloseTypes::CLOSE_NONE);
  }
  run.only_tab = model->count() == 1;
  run.browser = browser->GetWeakPtr();
  run.tab = tab->GetHandle();
  Load(tab, PageA());
  PostDemoTask(base::BindOnce(&NamesLoaded), base::Seconds(2));
}

// --------------------------------------------------------------- load-progress

struct ProgressRun {
  base::WeakPtr<BrowserWindowInterface> browser;
  std::string detail;
  bool ok = true;

  void Expect(bool condition, const std::string& what) {
    detail += " " + what + "=" + (condition ? "1" : "0");
    ok = ok && condition;
  }
};

ProgressRun& TheProgressRun() {
  static base::NoDestructor<ProgressRun> run;
  return *run;
}

// The line runs along the toolbar's bottom edge, as wide as the page card under it.
void ExpectLineOnThePage(ProgressRun& run, const std::string& when) {
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(run.browser.get());
  const std::optional<LoadProgressLineShown> shown =
      GetLoadProgressLineForTesting(run.browser.get());
  if (!view || !shown) {
    run.Expect(false, "line_" + when);
    return;
  }
  const gfx::Rect card = view->multi_contents_view()->GetBoundsInScreen();
  const gfx::Rect toolbar = view->toolbar()->GetBoundsInScreen();
  run.detail += " " + when + "=[" + shown->bounds.ToString() + " card " + card.ToString() + "]";
  // Along the toolbar row's bottom, or, with the toolbar in the sidebar, just above the card, or
  // along its top where it has no margin above it (fullscreen); in the window either way.
  const bool in_sidebar = IsToolbarInSidebar(view->toolbar());
  const gfx::Rect window = view->GetBoundsInScreen();
  const bool above_card = shown->bounds.bottom() < card.y() &&
                          shown->bounds.bottom() >= card.y() - kContentsCardInset;
  const bool card_top = card.y() - window.y() < kLoadProgressHeight + kContentsCardInset / 2 &&
                        shown->bounds.y() == card.y();
  run.Expect(shown->bounds.height() == kLoadProgressHeight && window.Contains(shown->bounds) &&
                 (in_sidebar ? above_card || card_top
                             : shown->bounds.bottom() == toolbar.bottom()) &&
                 shown->bounds.x() == card.x() && shown->bounds.width() == card.width(),
             "line_on_page_" + when + (in_sidebar ? "_sidebar_toolbar" : ""));
}

// The sidebar's toggle in the window, and the window's close button (empty without one).
std::pair<gfx::Rect, gfx::Rect> ToggleAndCloseButton(BrowserWindowInterface* browser) {
  VerticalTabStripRegionView* region = Region(browser);
  views::View* toggle = region ? region->GetTopContainer()->GetCollapseButton() : nullptr;
  if (!toggle) {
    return {};
  }
  return {toggle->ConvertRectToWidget(toggle->GetLocalBounds()),
          CloseButtonBoundsForTesting(region->GetWidget())};
}

void ProgressLeftFullscreen() {
  ProgressRun& run = TheProgressRun();
  if (!run.browser) {
    return;
  }
  ExpectLineOnThePage(run, "after_fullscreen");
  // Out of fullscreen the row's buttons start after the window buttons again.
  const auto [toggle, close] = ToggleAndCloseButton(run.browser.get());
  run.detail += " toggle_after_fullscreen=" + toggle.ToString() + " close=" + close.ToString();
  run.Expect(!close.IsEmpty() && toggle.x() >= close.right(), "row_inset_back_after_fullscreen");
  ReportDemo(kProgressMode, run.ok, run.detail);
}

void ProgressFullscreenHidden() {
  ProgressRun& run = TheProgressRun();
  VerticalTabStripRegionView* region = run.browser ? Region(run.browser.get()) : nullptr;
  if (!region) {
    return;
  }
  // Hidden in fullscreen: the window buttons are macOS's, in the title bar it reveals.
  run.Expect(WindowButtonsShownForTesting(region->GetWidget()), "buttons_in_fullscreen_hidden");
  tabs::VerticalTabStripStateController::From(run.browser.get())->RequestCollapse(false);
  chrome::ExecuteCommand(run.browser.get(), IDC_FULLSCREEN);
  PostDemoTask(base::BindOnce(&ProgressLeftFullscreen), base::Seconds(3));
}

void ProgressFullscreen() {
  ProgressRun& run = TheProgressRun();
  if (!run.browser) {
    return;
  }
  // macOS fullscreen moves the toolbar's parent into a widget of its own: the line must follow the
  // page there (it used to convert between the two widgets' views, a CHECK failure).
  run.Expect(run.browser->GetWindow()->IsFullscreen(), "fullscreen");
  ExpectLineOnThePage(run, "fullscreen");
  // No window buttons in the row in fullscreen: its buttons start at its edge.
  const auto [toggle, close] = ToggleAndCloseButton(run.browser.get());
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(run.browser.get());
  const int toolbar_x =
      view ? view->toolbar()->ConvertRectToWidget(view->toolbar()->GetLocalBounds()).x() : 0;
  run.detail += " toggle_in_fullscreen=" + toggle.ToString() +
                " toolbar_x=" + base::NumberToString(toolbar_x);
  run.Expect(!toggle.IsEmpty() && toggle.x() - toolbar_x < 40, "row_inset_follows_fullscreen");
  tabs::VerticalTabStripStateController::From(run.browser.get())->RequestCollapse(true);
  PostDemoTask(base::BindOnce(&ProgressFullscreenHidden), base::Seconds(2));
}

void ProgressHidden() {
  ProgressRun& run = TheProgressRun();
  if (!run.browser) {
    return;
  }
  ExpectLineOnThePage(run, "hidden_sidebar");
  tabs::VerticalTabStripStateController::From(run.browser.get())->RequestCollapse(false);
  chrome::ExecuteCommand(run.browser.get(), IDC_FULLSCREEN);
  PostDemoTask(base::BindOnce(&ProgressFullscreen), base::Seconds(3));
}

void ProgressForced() {
  ProgressRun& run = TheProgressRun();
  if (!run.browser) {
    return;
  }
  // Halfway-ish through a load: the drawn part is that much of the line.
  SetLoadProgressForTesting(run.browser.get(), {.loading = true, .progress = 0.6});
  const std::optional<LoadProgressLineShown> shown =
      GetLoadProgressLineForTesting(run.browser.get());
  run.Expect(shown && shown->opacity == 1.0f &&
                 std::abs(shown->filled.width() - 0.6 * shown->bounds.width()) <= 1 &&
                 shown->filled.x() == shown->bounds.x(),
             "fills_its_progress");
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(kProgressSnapshotSwitch)) {
    ReportDemo(kProgressMode, run.ok, run.detail + " (captured at 60%)");
    MaybeScheduleUiSnapshot(
        views::Widget::GetWidgetForNativeWindow(run.browser->GetWindow()->GetNativeWindow()),
        kProgressSnapshotSwitch);
    return;
  }
  SetLoadProgressForTesting(run.browser.get(), PageLoad());
  // Hidden sidebar: the line still runs along the page card, which now starts at the edge band.
  tabs::VerticalTabStripStateController::From(run.browser.get())->RequestCollapse(true);
  PostDemoTask(base::BindOnce(&ProgressHidden), base::Seconds(2));
}

void ProgressLoaded() {
  ProgressRun& run = TheProgressRun();
  if (!run.browser) {
    return;
  }
  const std::optional<LoadProgressLineShown> shown =
      GetLoadProgressLineForTesting(run.browser.get());
  run.Expect(shown && !shown->line.visible && shown->filled.IsEmpty(), "gone_after_load");
  ProgressForced();
}

void RunLoadProgressDemo(BrowserWindowInterface* browser) {
  FailDemoAfter(kProgressMode, base::Seconds(30));
  ProgressRun& run = TheProgressRun();
  run.browser = browser->GetWeakPtr();
  tabs::TabInterface* tab = browser->GetTabStripModel()->GetActiveTab();
  if (!tab || !GetLoadProgressLineForTesting(browser)) {
    ReportDemo(kProgressMode, false, "no tab, or no load progress line");
    return;
  }
  ExpectLineOnThePage(run, "shown_sidebar");
  // A page starting to load shows the line at once.
  Load(tab, PageA());
  const std::optional<LoadProgressLineShown> shown = GetLoadProgressLineForTesting(browser);
  run.Expect(shown && shown->line.visible && shown->line.fraction > 0 && !shown->line.finishing,
             "shows_while_loading");
  PostDemoTask(base::BindOnce(&ProgressLoaded), base::Seconds(2));
}

// --------------------------------------------------------------- other-apps

// Pages that never load (nothing listens on port 9): a little window shows its address anyway.
GURL OtherAppPage(const char* path) {
  return GURL(std::string("http://127.0.0.1:9/") + path);
}

struct OtherAppsRun {
  base::WeakPtr<BrowserWindowInterface> browser;
  std::string detail;
  bool ok = true;

  void Expect(bool condition, const std::string& what) {
    detail += " " + what + "=" + (condition ? "1" : "0");
    ok = ok && condition;
  }
};

OtherAppsRun& TheOtherAppsRun() {
  static base::NoDestructor<OtherAppsRun> run;
  return *run;
}

// The profile's little window showing `url`, if one does.
BrowserWindowInterface* LittleWindowShowing(Profile* profile, const GURL& url) {
  BrowserWindowInterface* found = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation([&](BrowserWindowInterface* window) {
    content::WebContents* shown = window->GetTabStripModel()->GetActiveWebContents();
    if (window->GetProfile() == profile && WindowKindOf(window) == WindowKind::kLittle && shown &&
        shown->GetVisibleURL() == url) {
      found = window;
      return false;
    }
    return true;
  });
  return found;
}

void OtherAppsCheck() {
  OtherAppsRun& run = TheOtherAppsRun();
  if (!run.browser) {
    return;
  }
  Profile* profile = run.browser->GetProfile();
  run.Expect(LittleWindowShowing(profile, OtherAppPage("mail")) != nullptr,
             "mail_link_in_little_window");
  run.Expect(LittleWindowShowing(profile, OtherAppPage("script")) != nullptr,
             "command_line_link_in_little_window");
  run.Expect(LittleWindowShowing(profile, OtherAppPage("mixed")) == nullptr,
             "mixed_command_line_left_to_chromium");
  // Close the little windows it opened.
  for (const char* path : {"mail", "script"}) {
    if (BrowserWindowInterface* little = LittleWindowShowing(profile, OtherAppPage(path))) {
      little->GetWindow()->Close();
    }
  }
  ReportDemo(kOtherAppsMode, run.ok, run.detail);
}

void RunOtherAppsDemo(BrowserWindowInterface* browser) {
  FailDemoAfter(kOtherAppsMode, base::Seconds(20));
  OtherAppsRun& run = TheOtherAppsRun();
  run.browser = browser->GetWeakPtr();
  Profile* profile = browser->GetProfile();
  // Links handed over by macOS (a mail link): the web page opens in a little window, a browser page
  // is left to open as a tab.
  StartupTabs links;
  links.emplace_back(OtherAppPage("mail"));
  links.emplace_back(GURL("chrome://version/"));
  const StartupTabs rest = OpenLinksFromOtherAppsInLittleWindows(profile, links);
  run.Expect(rest.size() == 1 && rest[0].url == GURL("chrome://version/"),
             "browser_page_left_as_tab");
  // A second launch's command line with a link: a little window, nothing left to Chromium.
  base::CommandLine script(base::CommandLine::NO_PROGRAM);
  script.AppendArg(OtherAppPage("script").spec());
  run.Expect(OpenCommandLineLinksInLittleWindows(profile, script, base::FilePath()),
             "command_line_taken");
  // One with a page that opens as a tab too (about:blank; Chromium drops chrome:// pages from a
  // command line): all of it goes to Chromium (here: not opened at all).
  base::CommandLine mixed(base::CommandLine::NO_PROGRAM);
  mixed.AppendArg(OtherAppPage("mixed").spec());
  mixed.AppendArg("about:blank");
  run.Expect(!OpenCommandLineLinksInLittleWindows(profile, mixed, base::FilePath()),
             "mixed_command_line_not_taken");
  PostDemoTask(base::BindOnce(&OtherAppsCheck), base::Seconds(2));
}

// --------------------------------------------------------------- sidebar-toolbar

struct ToolbarRun {
  base::WeakPtr<BrowserWindowInterface> browser;
  GURL page;
  std::string detail;
  bool ok = true;
  std::unique_ptr<ExpandOnHoverLock> slide_in;
  // A popup left open from the slid-in sidebar's toolbar.
  std::unique_ptr<views::BubbleDialogDelegate> bubble;
  std::unique_ptr<views::Widget> bubble_widget;
  // The toggle's lowest opacity while the sidebar hides.
  float toggle_opacity = 1.0f;

  void Expect(bool condition, const std::string& what) {
    detail += " " + what + "=" + (condition ? "1" : "0");
    ok = ok && condition;
  }
};

ToolbarRun& TheToolbarRun() {
  static base::NoDestructor<ToolbarRun> run;
  return *run;
}

void ToolbarBackInSidebar() {
  ToolbarRun& run = TheToolbarRun();
  BrowserView* view = run.browser ? BrowserView::GetBrowserViewForBrowser(run.browser.get()) : nullptr;
  if (!view) {
    ReportDemo(kToolbarMode, false, "the window went away");
    return;
  }
  run.Expect(IsToolbarInSidebar(view->toolbar()), "back_in_sidebar_with_sidebar");
  // Moved back in, the window buttons sit on its row again: the title bar is measured anew.
  {
    ToolbarView* toolbar = view->toolbar();
    const gfx::Rect close = CloseButtonBoundsForTesting(view->GetWidget());
    const gfx::Rect reload =
        toolbar->reload_button()->ConvertRectToWidget(toolbar->reload_button()->GetLocalBounds());
    run.detail += " back: close=" + close.ToString() + " reload=" + reload.ToString();
    run.Expect(!close.IsEmpty() && std::abs(close.CenterPoint().y() - reload.CenterPoint().y()) <= 3 &&
                   toolbar->height() == toolbar->GetPreferredSize().height(),
               "buttons_on_row_after_moving_back");
  }
  if (VerticalTabStripRegionView* region = Region(run.browser.get())) {
    run.Expect(region->GetTopContainer()->GetCollapseButton()->parent() == view->toolbar(),
               "toggle_back_in_toolbar_row");
  }
  ReportDemo(kToolbarMode, run.ok, run.detail);
}

void ToolbarOnTop() {
  ToolbarRun& run = TheToolbarRun();
  BrowserView* view = run.browser ? BrowserView::GetBrowserViewForBrowser(run.browser.get()) : nullptr;
  if (!view) {
    ReportDemo(kToolbarMode, false, "the window went away");
    return;
  }
  run.Expect(!IsToolbarInSidebar(view->toolbar()) &&
                 view->toolbar()->parent() == view->top_container() &&
                 view->top_container()->height() > 0,
             "tabs_on_top_toolbar_row");
  run.Expect(!view->toolbar()->avatar_toolbar_button() ||
                 !view->toolbar()->avatar_toolbar_button()->GetVisible(),
             "no_profile_button_on_top");
  // The strip's top row has its toggle and Tab Search back.
  if (VerticalTabStripRegionView* region = Region(run.browser.get())) {
    run.Expect(region->GetTopContainer()->GetCollapseButton()->parent() ==
                       region->GetTopContainer() &&
                   region->GetTopContainer()->GetComboButton()->parent() ==
                       region->GetTopContainer(),
               "top_row_buttons_back");
  }
  tabs::VerticalTabStripStateController::From(run.browser.get())->SetVerticalTabsEnabled(true);
  PostDemoTask(base::BindOnce(&ToolbarBackInSidebar), base::Seconds(2));
}

void ToolbarSlidOut() {
  ToolbarRun& run = TheToolbarRun();
  VerticalTabStripRegionView* region = run.browser ? Region(run.browser.get()) : nullptr;
  if (!region) {
    ReportDemo(kToolbarMode, false, "the window went away");
    return;
  }
  run.Expect(!WindowButtonsShownForTesting(region->GetWidget()), "buttons_hide_again");
  auto* state = tabs::VerticalTabStripStateController::From(run.browser.get());
  state->RequestCollapse(false);
  // Tabs on top: Chromium's toolbar row above the page again.
  state->SetVerticalTabsEnabled(false);
  PostDemoTask(base::BindOnce(&ToolbarOnTop), base::Seconds(2));
}

void CloseHeldPopup(ToolbarRun& run) {
  if (run.bubble_widget) {
    run.bubble_widget->CloseNow();
    run.bubble_widget.reset();
  }
  run.bubble.reset();
}

void ToolbarShownWithPopup() {
  ToolbarRun& run = TheToolbarRun();
  VerticalTabStripRegionView* region = run.browser ? Region(run.browser.get()) : nullptr;
  if (!region) {
    ReportDemo(kToolbarMode, false, "the window went away");
    return;
  }
  // Shown, with the popup still open: nothing is left to watch.
  run.Expect(!region->lb_popup_timer_running_for_testing(), "popup_timer_stops_when_shown");
  CloseHeldPopup(run);
  tabs::VerticalTabStripStateController::From(run.browser.get())->RequestCollapse(true);
  PostDemoTask(base::BindOnce(&ToolbarSlidOut), base::Seconds(2));
}

void ToolbarHeldByPopup() {
  ToolbarRun& run = TheToolbarRun();
  VerticalTabStripRegionView* region = run.browser ? Region(run.browser.get()) : nullptr;
  if (!region) {
    ReportDemo(kToolbarMode, false, "the window went away");
    return;
  }
  // Chromium slides it in and out only while the window is active.
  if (region->GetWidget()->ShouldPaintAsActive()) {
    run.Expect(region->is_expanded_on_hover() && region->lb_popup_timer_running_for_testing(),
               "popup_holds_it_out");
  } else {
    run.detail += " popup_holds_it_out=(not checked: the window isn't active)";
  }
  // Shown while the popup is open.
  tabs::VerticalTabStripStateController::From(run.browser.get())->RequestCollapse(false);
  PostDemoTask(base::BindOnce(&ToolbarShownWithPopup), base::Seconds(1));
}

void ToolbarSlidIn() {
  ToolbarRun& run = TheToolbarRun();
  VerticalTabStripRegionView* region = run.browser ? Region(run.browser.get()) : nullptr;
  BrowserView* view = run.browser ? BrowserView::GetBrowserViewForBrowser(run.browser.get()) : nullptr;
  if (!region || !view) {
    ReportDemo(kToolbarMode, false, "the window went away");
    return;
  }
  run.Expect(region->is_expanded_on_hover(), "slid_in");
  run.Expect(WindowButtonsShownForTesting(region->GetWidget()), "buttons_show_slid_in");
  // A popup hanging from the toolbar in the slid-in sidebar keeps it out; closed, it doesn't.
  views::BubbleDialogDelegate bubble(view->toolbar()->reload_button(),
                                     views::BubbleBorder::TOP_LEFT);
  bubble.SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  bubble.SetContentsView(std::make_unique<views::View>());
  std::unique_ptr<views::Widget> widget = views::BubbleDialogDelegate::CreateBubble(&bubble);
  widget->ShowInactive();
  run.Expect(KeepsSidebarOutForPopup(region, /*sidebar_hidden=*/true), "popup_keeps_it_out");
  widget->CloseNow();
  widget.reset();
  run.Expect(!KeepsSidebarOutForPopup(region, true), "closed_popup_doesnt");
  // A tab's hover card isn't a popup: it goes as the pointer leaves the tab.
  if (TabView* row = FindTabViewForDemo(run.browser.get(),
                                        run.browser->GetTabStripModel()->GetActiveTab())) {
    TabHoverCardBubbleView::InitParams params;
    params.use_animation = false;
    params.show_image_preview = false;
    auto* card = new TabHoverCardBubbleView(row, params);  // owned by its widget
    card->GetWidget()->ShowInactive();
    run.Expect(!KeepsSidebarOutForPopup(region, true), "hover_card_doesnt_keep_it_out");
    card->GetWidget()->CloseNow();
  } else {
    run.Expect(false, "hover_card_doesnt_keep_it_out");
  }
  // One left open: the pointer leaves, and it keeps the sidebar out.
  run.bubble = std::make_unique<views::BubbleDialogDelegate>(view->toolbar()->reload_button(),
                                                             views::BubbleBorder::TOP_LEFT);
  run.bubble->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  run.bubble->SetContentsView(std::make_unique<views::View>());
  run.bubble_widget = views::BubbleDialogDelegate::CreateBubble(run.bubble.get());
  run.bubble_widget->ShowInactive();
  run.slide_in.reset();
  region->OnMouseExited(ui::MouseEvent(ui::EventType::kMouseExited, gfx::PointF(), gfx::PointF(),
                                       base::TimeTicks::Now(), ui::EF_NONE, ui::EF_NONE));
  PostDemoTask(base::BindOnce(&ToolbarHeldByPopup), base::Seconds(1));
}

void ToolbarHidden() {
  ToolbarRun& run = TheToolbarRun();
  VerticalTabStripRegionView* region = run.browser ? Region(run.browser.get()) : nullptr;
  if (!region) {
    ReportDemo(kToolbarMode, false, "the window went away");
    return;
  }
  run.Expect(!WindowButtonsShownForTesting(region->GetWidget()), "buttons_hide_with_sidebar");
  // The toggle in the toolbar's row doesn't fade while the sidebar hides, as the strip's own
  // top row does.
  run.detail += " toggle_opacity_min=" + base::NumberToString(run.toggle_opacity);
  run.Expect(run.toggle_opacity == 1.0f, "toggle_stays_while_hiding");
  // Slid in, as the pointer at the edge does.
  run.slide_in = region->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepExpanded);
  PostDemoTask(base::BindOnce(&ToolbarSlidIn), base::Seconds(2));
}

// Notes the toggle's lowest opacity, every frame for `ticks` frames.
void WatchToggleOpacity(int ticks) {
  ToolbarRun& run = TheToolbarRun();
  VerticalTabStripRegionView* region = run.browser ? Region(run.browser.get()) : nullptr;
  views::View* toggle = region ? region->GetTopContainer()->GetCollapseButton() : nullptr;
  if (!toggle || ticks == 0) {
    return;
  }
  if (toggle->layer()) {
    run.toggle_opacity = std::min(run.toggle_opacity, toggle->layer()->opacity());
  }
  PostDemoTask(base::BindOnce(&WatchToggleOpacity, ticks - 1), base::Milliseconds(16));
}

void ToolbarPlaced() {
  ToolbarRun& run = TheToolbarRun();
  BrowserView* view = run.browser ? BrowserView::GetBrowserViewForBrowser(run.browser.get()) : nullptr;
  VerticalTabStripRegionView* region = run.browser ? Region(run.browser.get()) : nullptr;
  if (!view || !region) {
    ReportDemo(kToolbarMode, false, "no window or sidebar");
    return;
  }
  ToolbarView* toolbar = view->toolbar();
  run.Expect(IsToolbarInSidebar(toolbar) && region->Contains(toolbar), "toolbar_in_sidebar");
  run.Expect(view->top_container()->height() == 0, "nothing_above_page");
  const gfx::Rect card = view->multi_contents_view()->bounds();
  run.detail += " card=" + card.ToString();
  run.Expect(card.y() == kContentsCardInset, "card_inset_at_top");
  LocationBarView* bar = toolbar->location_bar_view();
  run.Expect(bar && bar->GetVisible() &&
                 bar->GetBoundsInScreen().y() > toolbar->reload_button()->GetBoundsInScreen().bottom(),
             "address_under_buttons");
  run.Expect(WindowButtonsShownForTesting(region->GetWidget()), "buttons_shown");
  // One row with the window buttons, as in Arc: they sit centred on the toolbar's buttons, which
  // start after them with the strip's toggle, and Tab Search is at the row's end; the strip's own
  // top row is empty. The gap between them drags the window; a button doesn't.
  {
    VerticalTabStripTopContainer* top = region->GetTopContainer();
    const gfx::Rect close = CloseButtonBoundsForTesting(region->GetWidget());
    const gfx::Rect reload =
        toolbar->reload_button()->ConvertRectToWidget(toolbar->reload_button()->GetLocalBounds());
    views::View* toggle = top->GetCollapseButton();
    const gfx::Rect toggle_bounds = toggle->ConvertRectToWidget(toggle->GetLocalBounds());
    run.detail += " close=" + close.ToString() + " reload=" + reload.ToString() +
                  " toggle=" + toggle_bounds.ToString();
    run.Expect(toggle->parent() == toolbar && top->GetComboButton()->parent() == toolbar &&
                   top->height() == 0,
               "toggle_and_tab_search_in_toolbar_row");
    run.Expect(!close.IsEmpty() &&
                   std::abs(close.CenterPoint().y() - reload.CenterPoint().y()) <= 3 &&
                   toggle_bounds.x() >= close.right() && reload.x() > toggle_bounds.x(),
               "one_row_with_window_buttons");
    // The row's inset for the window buttons is the laid-out one: a space switch's slide (a layer
    // transform on the sidebar's content) doesn't change it.
    {
      views::View* sliding = toolbar->parent();
      const bool had_layer = sliding->layer() != nullptr;
      if (!had_layer) {
        sliding->SetPaintToLayer();
      }
      const int inset = SidebarToolbarLeadingInset(toolbar);
      gfx::Transform slide;
      slide.Translate(24, 0);
      sliding->layer()->SetTransform(slide);
      const int inset_sliding = SidebarToolbarLeadingInset(toolbar);
      sliding->layer()->SetTransform(gfx::Transform());
      if (!had_layer) {
        sliding->DestroyLayer();
      }
      run.detail += " inset=" + base::NumberToString(inset) +
                    " inset_sliding=" + base::NumberToString(inset_sliding);
      run.Expect(inset > 0 && inset_sliding == inset, "inset_ignores_slide");
    }
    // The row drags the window where it has no button: beside the window buttons, and in the gap
    // between the leading and trailing buttons when the sidebar is wide enough to have one.
    const gfx::Rect gap = SidebarToolbarGapForTesting(toolbar);
    run.detail += " gap=" + gap.ToString();
    views::NonClientView* frame = region->GetWidget()->non_client_view();
    const gfx::Point beside_buttons(toggle_bounds.x() - 4, toggle_bounds.CenterPoint().y());
    run.Expect(frame->NonClientHitTest(beside_buttons) == HTCAPTION &&
                   (gap.IsEmpty() || frame->NonClientHitTest(gap.CenterPoint()) == HTCAPTION) &&
                   frame->NonClientHitTest(reload.CenterPoint()) == HTCLIENT,
               "row_drags_window");
    // The toolbar is as tall as it asks to be, its address row in it: laid out with its margins
    // when it moved in, not left at its height from before.
    LocationBarView* address = toolbar->location_bar_view();
    run.Expect(toolbar->height() == toolbar->GetPreferredSize().height() && address &&
                   address->bounds().bottom() <= toolbar->height(),
               "toolbar_full_height");
    // No profile button: the space's icon is in the footer and beside its name already. The
    // sidebar opens, and can be narrowed, only to a width that fits the whole row.
    run.Expect(!toolbar->avatar_toolbar_button() || !toolbar->avatar_toolbar_button()->GetVisible(),
               "no_profile_button_in_row");
    const int inset = views::View::ConvertRectToTarget(toolbar, region, toolbar->GetLocalBounds()).x();
    const int row_needs = toolbar->GetPreferredSize().width() + 2 * inset;
    // Also with macOS 13-15's window buttons, which take 94 px with their margins (86 on 26).
    constexpr int kWidestWindowButtons = 94;
    const int window_buttons = base::ClampCeil(view->browser_widget()
                                                   ->GetFrameView()
                                                   ->GetBrowserLayoutParams()
                                                   .leading_exclusion.ContentWithPadding()
                                                   .width());
    const int widest_row_needs = row_needs + std::max(0, kWidestWindowButtons - window_buttons);
    run.detail += " row_needs=" + base::NumberToString(row_needs) +
                  " widest_row_needs=" + base::NumberToString(widest_row_needs);
    run.Expect(widest_row_needs <= tabs::kVerticalTabStripUncollapsedMinWidth &&
                   tabs::kVerticalTabStripDefaultUncollapsedWidth ==
                       tabs::kVerticalTabStripUncollapsedMinWidth,
               "opens_at_the_rows_width");
  }
  // Cmd+F's find bar opens at the page's top right, not at the address in the sidebar.
  chrome::ExecuteCommand(run.browser.get(), IDC_FIND);
  {
    FindBarController* find = FindBarController::From(run.browser.get());
    FindBar* find_bar = find ? find->find_bar() : nullptr;
    gfx::Point position;
    bool fully_visible = false;
    const bool shown = find_bar && find_bar->GetFindBarTesting() &&
                       find_bar->GetFindBarTesting()->GetFindBarWindowInfo(&position, &fully_visible);
    const gfx::Rect page = view->multi_contents_view()->GetBoundsInScreen();
    run.detail += " find_bar_at=" + position.ToString() + " page=" + page.ToString();
    // Its window starts a little above the page, for its shadow.
    run.Expect(shown && position.x() > page.CenterPoint().x() && position.x() < page.right() &&
                   std::abs(position.y() - page.y()) <= 16,
               "find_bar_on_page");
    if (find) {
      find->EndFindSession(find_in_page::SelectionAction::kKeep,
                           find_in_page::ResultAction::kKeep);
    }
  }
  // A click on the address opens the command bar on the page's address.
  if (bar && bar->omnibox_view()) {
    gfx::Point point = bar->omnibox_view()->GetLocalBounds().CenterPoint();
    views::View::ConvertPointToWidget(bar->omnibox_view(), &point);
    for (ui::EventType type : {ui::EventType::kMousePressed, ui::EventType::kMouseReleased}) {
      ui::MouseEvent event(type, point, point, base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                           ui::EF_LEFT_MOUSE_BUTTON);
      bar->GetWidget()->OnMouseEvent(&event);
    }
  }
  const std::optional<std::u16string> text = CommandBarTextForTesting();
  run.Expect(text == base::UTF8ToUTF16(run.page.spec()), "address_click_edits");
  RunFirstCommandBarResultForTesting(base::UTF8ToUTF16(run.page.spec()));  // closes the bar
  // Hidden: the window's buttons go with the sidebar.
  auto* state = tabs::VerticalTabStripStateController::From(run.browser.get());
  state->SetExpandOnHoverEnabled(true);
  state->RequestCollapse(true);
  run.toggle_opacity = 1.0f;
  WatchToggleOpacity(60);  // about a second, the animation's length and more
  PostDemoTask(base::BindOnce(&ToolbarHidden), base::Seconds(2));
}

void RunSidebarToolbarDemo(BrowserWindowInterface* browser) {
  FailDemoAfter(kToolbarMode, base::Seconds(30));
  ToolbarRun& run = TheToolbarRun();
  run.browser = browser->GetWeakPtr();
  run.page = GURL("data:text/html,<title>Toolbar</title>T");
  chrome::AddAndReturnTabAt(browser, run.page, -1, /*foreground=*/true);
  PostDemoTask(base::BindOnce(&ToolbarPlaced), base::Seconds(2));
}

// --------------------------------------------------------------- pinned-fresh

struct FreshRun {
  base::WeakPtr<BrowserWindowInterface> browser;
  tabs::TabHandle tab;
  std::u16string title_before;
  // What the page's script set (window.lbScript, "-" for none): a script the server sends with
  // a new value each time and `Cache-Control: no-cache`, so a copy must be checked first.
  std::string script_before;
  std::string slept;  // the tab's state asleep, for the log
};

// Reads the page's window.lbScript into `done` ("-" if the page has none).
void ReadFreshScript(content::WebContents* contents, base::OnceCallback<void(std::string)> done) {
  contents->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
      u"String(window.lbScript || '-')",
      base::BindOnce(
          [](base::OnceCallback<void(std::string)> done, base::Value value) {
            std::move(done).Run(value.is_string() ? value.GetString() : "-");
          },
          std::move(done)),
      content::ISOLATED_WORLD_ID_GLOBAL);
}

FreshRun& TheFreshRun() {
  static base::NoDestructor<FreshRun> run;
  return *run;
}

void FreshReport(std::string script_after);
void FreshSleep(std::string script_before);

void FreshWoke() {
  FreshRun& run = TheFreshRun();
  tabs::TabInterface* tab = run.tab.Get();
  if (!run.browser || !tab) {
    ReportDemo(kFreshMode, false, "the tab went away");
    return;
  }
  ReadFreshScript(tab->GetContents(), base::BindOnce(&FreshReport));
}

void FreshReport(std::string script_after) {
  FreshRun& run = TheFreshRun();
  tabs::TabInterface* tab = run.tab.Get();
  if (!run.browser || !tab) {
    ReportDemo(kFreshMode, false, "the tab went away");
    return;
  }
  const std::u16string after = tab->GetContents()->GetTitle();
  // Shown again, the page asks the server, which answers with a new title: not the cached copy.
  // Its script too (when the page has one): not the copy from before.
  const bool script_fresh = run.script_before == "-" || script_after != run.script_before;
  ReportDemo(kFreshMode,
             !after.empty() && after != run.title_before && !IsAsleep(*tab) && script_fresh,
             "before=" + base::UTF16ToUTF8(run.title_before) + " after=" +
                 base::UTF16ToUTF8(after) + " script_before=" + run.script_before +
                 " script_after=" + script_after +
                 (run.script_before == "-" ? " (the page has no script: not checked)" : "") +
                 " asleep: " + run.slept + " awake: discarded=" +
                 (tab->GetContents()->WasDiscarded() ? "1" : "0") + " live=" +
                 (tab->GetContents()->GetPrimaryMainFrame()->IsRenderFrameLive() ? "1" : "0") +
                 " needs_reload=" +
                 (tab->GetContents()->GetController().NeedsReload() ? "1" : "0"));
}

void FreshSlept() {
  FreshRun& run = TheFreshRun();
  tabs::TabInterface* tab = run.tab.Get();
  if (!run.browser || !tab || !IsAsleep(*tab)) {
    ReportDemo(kFreshMode, false, "the pinned tab didn't sleep");
    return;
  }
  run.slept = "discarded=" + std::string(tab->GetContents()->WasDiscarded() ? "1" : "0") +
              " needs_reload=" +
              (tab->GetContents()->GetController().NeedsReload() ? "1" : "0");
  run.browser->GetTabStripModel()->ActivateTab(tab);
  // Showing it loads it once its window is visible; the demo's window may be behind others, so
  // load it as showing does.
  tab->GetContents()->GetController().LoadIfNecessary();
  PostDemoTask(base::BindOnce(&FreshWoke), base::Seconds(4));
}

void FreshLoaded() {
  FreshRun& run = TheFreshRun();
  tabs::TabInterface* tab = run.tab.Get();
  if (!run.browser || !tab) {
    ReportDemo(kFreshMode, false, "the tab went away");
    return;
  }
  run.title_before = tab->GetContents()->GetTitle();
  ReadFreshScript(tab->GetContents(), base::BindOnce(&FreshSleep));
}

void FreshSleep(std::string script_before) {
  FreshRun& run = TheFreshRun();
  tabs::TabInterface* tab = run.tab.Get();
  if (!run.browser || !tab) {
    ReportDemo(kFreshMode, false, "the tab went away");
    return;
  }
  run.script_before = std::move(script_before);
  TabStripModel* model = run.browser->GetTabStripModel();
  model->SetTabPinned(model->GetIndexOfTab(tab), true);
  model->ActivateTabAt(model->count() - 1);  // another tab, so this one can sleep
  SleepPinnedTabs(*model, {tab});
  PostDemoTask(base::BindOnce(&FreshSlept), base::Seconds(4));
}

void RunPinnedFreshDemo(BrowserWindowInterface* browser) {
  FailDemoAfter(kFreshMode, base::Seconds(30));
  const std::string url =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("lb-demo-url");
  if (url.empty()) {
    ReportDemo(kFreshMode, false, "needs --lb-demo-url");
    return;
  }
  content::WebContents* contents =
      chrome::AddAndReturnTabAt(browser, GURL(url), 0, /*foreground=*/true);
  FreshRun& run = TheFreshRun();
  run.browser = browser->GetWeakPtr();
  run.tab = tabs::TabInterface::GetFromContents(contents)->GetHandle();
  PostDemoTask(base::BindOnce(&FreshLoaded), base::Seconds(3));
}

}  // namespace

bool MaybeRunSidebarEditsDemo(BrowserWindowInterface* browser, const std::string& mode) {
  if (mode == kHiddenMode) {
    RunHiddenSidebarDemo(browser);
    return true;
  }
  if (mode == kNamesMode) {
    RunTabNamesDemo(browser);
    return true;
  }
  if (mode == kProgressMode) {
    RunLoadProgressDemo(browser);
    return true;
  }
  if (mode == kOtherAppsMode) {
    RunOtherAppsDemo(browser);
    return true;
  }
  if (mode == kToolbarMode) {
    RunSidebarToolbarDemo(browser);
    return true;
  }
  if (mode == kFreshMode) {
    RunPinnedFreshDemo(browser);
    return true;
  }
  return false;
}

}  // namespace lb
