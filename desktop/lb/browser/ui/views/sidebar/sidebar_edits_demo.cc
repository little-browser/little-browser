// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/sidebar/sidebar_edits_demo.h"

#include <memory>
#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/tabs/tab_menu_model_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/tab/tab_accessibility.h"
#include "chrome/browser/ui/views/tabs/tab/tab_title.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "lb/browser/demo/demo_report.h"
#include "lb/browser/tabs/tab_names.h"
#include "lb/browser/ui/lb_ui_snapshot.h"
#include "lb/browser/ui/tabs/pinned_tab_sleep.h"
#include "lb/browser/ui/tabs/tab_custom_data.h"
#include "lb/browser/ui/views/command_bar/command_bar.h"
#include "lb/browser/ui/views/sidebar/new_tab_row_view.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/widget/widget.h"

namespace lb {

namespace {

constexpr char kNamesMode[] = "tab-names";
constexpr char kSnapshotSwitch[] = "lb-ui-snapshot-sidebar";

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
  // The space's saved tabs carry the name and the link.
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
    saved_ok = saved_ok || (name && *name == "Docs" && link && GURL(*link) == PageB());
  }
  run.Expect(saved_ok, "saved_with_space");
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(kSnapshotSwitch)) {
    ReportDemo(kNamesMode, run.ok, run.detail + " (captured)");
    MaybeScheduleUiSnapshot(
        views::Widget::GetWidgetForNativeWindow(run.browser->GetWindow()->GetNativeWindow()),
        kSnapshotSwitch);
    return;
  }
  ReportDemo(kNamesMode, run.ok, run.detail);
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
  PostDemoTask(base::BindOnce(&NamesFinish), base::Seconds(3));  // the space saves its tabs
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
  run.Expect(menu == "900:1 901:1 902:1 903:1", "menu_left_ok");
  run.Expect(run.RowTitle() == u"Docs", "name_kept_on_other_page");
  run.Run(TabNameCommand::kBackToPinnedUrl);
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
  run.Expect(menu == "900:1 901:1 902:0 903:0", "menu_pinned_ok");
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

}  // namespace

bool MaybeRunSidebarEditsDemo(BrowserWindowInterface* browser, const std::string& mode) {
  if (mode == kNamesMode) {
    RunTabNamesDemo(browser);
    return true;
  }
  return false;
}

}  // namespace lb
