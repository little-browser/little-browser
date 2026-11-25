// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/sidebar/sidebar_edits_demo.h"

#include <memory>
#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/startup/startup_tab.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/tabs/tab_menu_model_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/tab/tab_accessibility.h"
#include "chrome/browser/ui/views/tabs/tab/tab_title.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "lb/browser/demo/demo_report.h"
#include "lb/browser/tabs/sidebar_width.h"
#include "lb/browser/tabs/tab_names.h"
#include "lb/browser/ui/lb_ui_snapshot.h"
#include "lb/browser/ui/tabs/pinned_tab_sleep.h"
#include "lb/browser/ui/tabs/tab_custom_data.h"
#include "lb/browser/ui/views/command_bar/command_bar.h"
#include "lb/browser/ui/views/little_window/little_window.h"
#include "lb/browser/ui/views/sidebar/new_tab_row_view.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/widget/widget.h"

namespace lb {

namespace {

constexpr char kHiddenMode[] = "hidden-sidebar";
constexpr char kNamesMode[] = "tab-names";
constexpr char kOtherAppsMode[] = "other-apps";
constexpr char kSnapshotSwitch[] = "lb-ui-snapshot-sidebar";

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
  // With expand on hover off nothing would slide it in: it stays Chromium's column of icons.
  if (auto* state = tabs::VerticalTabStripStateController::From(run.browser.get())) {
    state->SetExpandOnHoverEnabled(false);
  }
  PostDemoTask(base::BindOnce(&HiddenWithoutHover), base::Seconds(1));
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
  // Leave the sidebar as it was.
  state->SetExpandOnHoverEnabled(true);
  state->SetUncollapsedWidth(run.width);
  state->RequestCollapse(false);
  ReportDemo(kHiddenMode, run.ok, run.detail);
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
  const int wider = SlidInSidebarWidth(run.width + 60, tabs::kVerticalTabStripUncollapsedMinWidth,
                                       tabs::kVerticalTabStripUncollapsedMaxWidth);
  run.detail += " | dragged: " + run.State(region) +
                " saved_width=" + base::NumberToString(state->GetUncollapsedWidth());
  run.Expect(region->width() == wider, "widened in place");
  run.Expect(state->GetUncollapsedWidth() == wider, "the new width is the sidebar's");
  run.Expect(state->IsCollapsed(), "still hidden");
  run.Expect(PageLeft(run.browser.get()) <= 1, "the page stays put under it");
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
  run.Expect(PageLeft(run.browser.get()) <= 1, "over the page, which doesn't move");
  // Drag its edge 60 px to the right, as the resize area does.
  region->OnResize(30, /*done_resizing=*/false);
  region->OnResize(60, /*done_resizing=*/true);
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
  run.Expect(state->IsCollapsed(), "hidden");
  run.Expect(region->width() == kHiddenSidebarEdgeWidth, "only the edge band");
  run.Expect(region->layer()->opacity() == 0.0f, "not drawn");
  run.Expect(PageLeft(run.browser.get()) <= 1, "the page takes the window's width");
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
  std::vector<int> path = DragPathForDemo(from, below);
  for (int y : DragPathForDemo(below, from)) {
    path.push_back(y);
  }
  SimulateTabDragForDemo(run.browser.get(), run.tab.Get(), path, /*drop=*/true);
  PostDemoTask(base::BindOnce(&NamesDragged), base::Seconds(1));
}

void NamesDragged() {
  NamesRun& run = TheNamesRun();
  if (!run.tab.Get()) {
    ReportDemo(kNamesMode, false, "the tab went away");
    return;
  }
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
  // Unnamed, it would show its link's title, not the page's (docs/ux.md §6).
  run.Expect(GetTabCustom(run.tab.Get()).pinned_title == u"Alpha page", "link_title_kept");
  run.Expect(
      TabTitleWithoutName(run.tab.Get(), run.tab.Get()->GetContents()->GetTitle()) == u"Alpha page",
      "unnamed_shows_link_title");
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
  // A second tab: dragging a window's only tab would drag the window.
  if (browser->GetTabStripModel()->count() < 2) {
    chrome::AddAndReturnTabAt(browser, GURL("about:blank"), -1, /*foreground=*/false);
  }
  NamesRun& run = TheNamesRun();
  run.browser = browser->GetWeakPtr();
  run.tab = tab->GetHandle();
  Load(tab, PageA());
  PostDemoTask(base::BindOnce(&NamesLoaded), base::Seconds(2));
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
  if (mode == kOtherAppsMode) {
    RunOtherAppsDemo(browser);
    return true;
  }
  return false;
}

}  // namespace lb
