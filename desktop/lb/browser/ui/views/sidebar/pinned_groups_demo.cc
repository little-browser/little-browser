// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/sidebar/pinned_groups_demo.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_header_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/groups/tab_group_editor_bubble_view.h"
#include "components/prefs/pref_service.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "lb/browser/demo/demo_report.h"
#include "lb/browser/spaces/space_model.h"
#include "lb/browser/ui/lb_ui_snapshot.h"
#include "lb/browser/ui/tabs/pinned_group_tracker.h"
#include "lb/browser/ui/tabs/pinned_tab_sleep.h"
#include "lb/browser/ui/views/sidebar/new_tab_row_view.h"
#include "lb/browser/ui/views/sidebar/pinned_group_layout.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "ui/base/models/list_selection_model.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace lb {

namespace {

constexpr char kDemo[] = "pinned-groups";
constexpr char kRestartDemo[] = "pinned-groups-restart";
constexpr char kDropsDemo[] = "pinned-groups-drops";
constexpr char kSnapshotSwitch[] = "lb-ui-snapshot-pinned-drag";
constexpr char16_t kGroupTitle[] = u"Work stuff";
constexpr int kGroupColor = static_cast<int>(tab_groups::TabGroupColorId::kCyan);

// A page whose title is `name`.
GURL Page(const std::string& name) {
  return GURL("data:text/html,<title>" + name + "</title>" + name);
}

std::string TitleOf(const tabs::TabInterface* tab) {
  return tab ? base::UTF16ToUTF8(tab->GetContents()->GetTitle()) : std::string("?");
}

template <typename T>
std::vector<T*> ViewsOf(BrowserWindowInterface* browser) {
  return FindViewsForDemo<T>(WindowViewForDemo(browser));
}

PinnedGroupHeaderView* HeaderOf(BrowserWindowInterface* browser, const std::string& id) {
  for (PinnedGroupHeaderView* header : ViewsOf<PinnedGroupHeaderView>(browser)) {
    if (header->group_id() == id) {
      return header;
    }
  }
  return nullptr;
}

bool Shows(const views::View* view) {
  return view && view->GetVisible() && !view->bounds().IsEmpty();
}

// One demo run: steps a second apart, each recording what failed.
struct Run {
  std::string mode;
  base::WeakPtr<BrowserWindowInterface> browser;
  std::vector<base::OnceCallback<void(Run&)>> steps;
  size_t next = 0;
  std::vector<std::string> failures;
  // The four pinned pages A, B, C, D, as handles.
  std::vector<tabs::TabHandle> tabs;
  std::string group;
  std::unique_ptr<views::Widget> menu;  // a today group's menu
  gfx::Point drag_end;                  // where a header drag ends
  bool hold = false;                    // stop here, without a result (for a snapshot)

  void Expect(bool ok, const std::string& what) {
    if (!ok) {
      failures.push_back(what);
    }
  }
  tabs::TabInterface* Tab(size_t i) const { return i < tabs.size() ? tabs[i].Get() : nullptr; }
  SidebarController* controller() const {
    return browser ? SidebarController::FromBrowser(browser.get()) : nullptr;
  }
  TabStripModel* model() const { return browser->GetTabStripModel(); }
  PinnedGroupTracker& groups() const { return controller()->pinned_groups(); }
  // The today tabs as "a b c" (the window's own first tab as "-").
  std::string Today() const {
    std::vector<std::string> parts;
    for (int i = model()->IndexOfFirstNonPinnedTab(); i < model()->count(); ++i) {
      const std::string title = TitleOf(model()->GetTabAtIndex(i));
      parts.push_back((title.size() > 3 ? std::string("-") : title) +
                      (model()->GetTabAtIndex(i)->GetGroup() ? "*" : ""));
    }
    return base::JoinString(parts, " ");
  }
  // The pinned tabs as "A B [C D]" (a pinned group in brackets with its title before it).
  std::string Pinned() const {
    std::vector<std::string> parts;
    std::string open;
    for (int i = 0; i < model()->IndexOfFirstNonPinnedTab(); ++i) {
      tabs::TabInterface* tab = model()->GetTabAtIndex(i);
      const PinnedGroup* group = groups().GroupOf(tab);
      const std::string id = group ? group->id : std::string();
      if (id != open && !open.empty()) {
        parts.back() += "]";
      }
      std::string part = TitleOf(tab);
      if (id != open && !id.empty()) {
        part = base::UTF16ToUTF8(group->title) + ":[" + part;
      }
      open = id;
      parts.push_back(part);
    }
    if (!open.empty()) {
      parts.back() += "]";
    }
    return base::JoinString(parts, " ");
  }
};

void RunNext(std::unique_ptr<Run> run) {
  if (!run->browser || !run->controller() || run->hold) {
    return;
  }
  // The first failing step ends the run: later steps build on it.
  if (run->next == run->steps.size() || !run->failures.empty()) {
    ReportDemo(
        run->mode, run->failures.empty(),
        run->failures.empty() ? "pinned: " + run->Pinned() : base::JoinString(run->failures, "; "));
    // A clean quit, so the next launch restores this session (pinned-groups-restart).
    if (run->mode == kDemo) {
      PostDemoTask(base::BindOnce(&chrome::AttemptExit), base::Seconds(1));
    }
    return;
  }
  std::move(run->steps[run->next++]).Run(*run);
  PostDemoTask(base::BindOnce(&RunNext, std::move(run)), base::Seconds(1));
}

void Start(std::unique_ptr<Run> run) {
  FailDemoAfter(run->mode, base::Seconds(60));
  PostDemoTask(base::BindOnce(&RunNext, std::move(run)), base::Seconds(2));
}

// --- pinned-groups ----------------------------------------------------------------------------

void AddSteps(Run& run) {
  // Four pinned pages, A to D, after the window's own tab, which stays shown.
  run.steps.push_back(base::BindOnce([](Run& run) {
    for (const char* name : {"A", "B", "C", "D"}) {
      content::WebContents* contents =
          chrome::AddAndReturnTabAt(run.browser.get(), Page(name), -1, /*foreground=*/false);
      tabs::TabInterface* tab = tabs::TabInterface::GetFromContents(contents);
      run.model()->SetTabPinned(run.model()->GetIndexOfTab(tab), true);
      run.tabs.push_back(tab->GetHandle());
    }
  }));
  // The tab menu's Add to New Group with B and C: a pinned group, its name being edited.
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(run.Pinned() == "A B C D", "pinned at start: " + run.Pinned());
    ui::ListSelectionModel selection;
    const int b = run.model()->GetIndexOfTab(run.Tab(1));
    selection.AddIndexToSelection(b);
    selection.AddIndexToSelection(run.model()->GetIndexOfTab(run.Tab(2)));
    selection.set_active(b);
    selection.set_anchor(b);
    tabs::TabInterface* const shown = run.model()->GetActiveTab();
    run.model()->SetSelectionFromModel(selection);
    run.model()->ExecuteContextMenuCommand(b, TabStripModel::CommandAddToNewGroup);
    run.model()->ActivateTab(shown);
    const PinnedGroup* group = run.groups().GroupOf(run.Tab(1));
    run.Expect(group, "B has no pinned group");
    if (!group) {
      return;
    }
    run.group = group->id;
    const std::optional<int> hue = run.controller()->ActiveSpaceHue();
    run.Expect(hue && group->color == static_cast<int>(TabGroupColorForHue(*hue)),
               "the group isn't in the space's colour");
    run.Expect(!run.Tab(1)->GetGroup() && run.Tab(1)->IsPinned(),
               "B was put in a Chromium group or unpinned");
  }));
  run.steps.push_back(base::BindOnce([](Run& run) {
    PinnedGroupHeaderView* header = HeaderOf(run.browser.get(), run.group);
    run.Expect(Shows(header), "no header shows");
    const std::vector<views::Textfield*> fields =
        header ? FindViewsForDemo<views::Textfield>(header) : std::vector<views::Textfield*>();
    run.Expect(!fields.empty() && fields.front()->GetVisible(), "the name isn't being edited");
    TabView* a = FindTabViewForDemo(run.browser.get(), run.Tab(0));
    TabView* b = FindTabViewForDemo(run.browser.get(), run.Tab(1));
    run.Expect(a && b && b->x() > a->x(), "B isn't indented");
    run.Expect(header && b && header->y() < b->y() && header->y() > a->y(),
               "the header isn't between A and B");
    run.groups().SetTitle(run.group, u"Reading");
    run.Expect(run.Pinned() == "A Reading:[B C] D", "after grouping: " + run.Pinned());
    run.groups().SetCollapsed(run.group, true);
  }));
  // Collapsed: B and C hide, A and D stay; the header shows the count.
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(!Shows(FindTabViewForDemo(run.browser.get(), run.Tab(1))) &&
                   !Shows(FindTabViewForDemo(run.browser.get(), run.Tab(2))),
               "a collapsed group's tabs show");
    run.Expect(Shows(FindTabViewForDemo(run.browser.get(), run.Tab(0))) &&
                   Shows(FindTabViewForDemo(run.browser.get(), run.Tab(3))),
               "tabs outside the collapsed group hide");
    run.Expect(Shows(HeaderOf(run.browser.get(), run.group)), "the collapsed header hides");
    run.groups().SetCollapsed(run.group, false);
    // D moved between B and C joins the group.
    run.model()->MoveWebContentsAt(run.model()->GetIndexOfTab(run.Tab(3)),
                                   run.model()->GetIndexOfTab(run.Tab(2)),
                                   /*select_after_move=*/false);
  }));
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(Shows(FindTabViewForDemo(run.browser.get(), run.Tab(1))),
               "an expanded group's tab hides");
    run.Expect(run.Pinned() == "A Reading:[B D C]", "D moved inside: " + run.Pinned());
    // D moved to the top leaves it.
    run.model()->MoveWebContentsAt(run.model()->GetIndexOfTab(run.Tab(3)), 0,
                                   /*select_after_move=*/false);
  }));
  // Close: its tabs sleep and it stays.
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(run.Pinned() == "D A Reading:[B C]", "D moved away: " + run.Pinned());
    run.Expect(!run.groups().AllTabsAsleep(run.group), "the group's tabs sleep before Close");
    run.groups().Close(run.group);
  }));
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(run.Tab(1) && IsAsleep(*run.Tab(1)) && run.Tab(2) && IsAsleep(*run.Tab(2)),
               "Close didn't put the group's tabs to sleep");
    run.Expect(run.Pinned() == "D A Reading:[B C]", "after Close: " + run.Pinned());
    run.Expect(run.groups().AllTabsAsleep(run.group), "the menu wouldn't offer Remove");
    run.groups().Close(run.group);  // now Remove
  }));
  // Remove: the group and its tabs are gone. Then A and D make a group, unpinned and pinned again.
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(!run.Tab(1) && !run.Tab(2), "Remove left the group's tabs");
    run.Expect(run.Tab(0) && run.Tab(3), "Remove closed A or D too");
    run.Expect(!run.groups().Find(run.group), "Remove left the group");
    run.Expect(run.Pinned() == "D A", "after Remove: " + run.Pinned());
    run.group = run.groups().Create({run.Tab(3), run.Tab(0)},
                                    PinnedGroup{.title = kGroupTitle, .color = kGroupColor});
    run.Expect(run.Pinned() == "Work stuff:[D A]", "a group of D and A: " + run.Pinned());
    run.groups().Unpin(run.group);
  }));
  run.steps.push_back(base::BindOnce([](Run& run) {
    if (!run.Tab(3) || !run.Tab(0)) {
      run.Expect(false, "A or D is gone");
      return;
    }
    run.Expect(!run.Tab(3)->IsPinned() && !run.Tab(0)->IsPinned(), "Unpin left tabs pinned");
    const std::optional<tab_groups::TabGroupId> today = run.Tab(3)->GetGroup();
    const TabGroup* group = today ? run.model()->group_model()->GetTabGroup(*today) : nullptr;
    run.Expect(group && run.Tab(0)->GetGroup() == today, "Unpin made no today group of both");
    if (!group) {
      return;
    }
    run.Expect(group->visual_data()->title() == kGroupTitle &&
                   static_cast<int>(group->visual_data()->color()) == kGroupColor,
               "the today group lost the name or colour");
    run.Expect(run.model()->GetIndexOfTab(run.Tab(3)) + 1 == run.model()->GetIndexOfTab(run.Tab(0)),
               "Unpin changed the order");
    // The today group's menu, as its header opens it.
    run.menu = TabGroupEditorBubbleView::Show(
        run.browser.get(), *today, FindTabViewForDemo(run.browser.get(), run.Tab(3)), std::nullopt,
        /*stop_context_menu_propagation=*/false);
  }));
  // Its Pin Group makes a pinned group again.
  run.steps.push_back(base::BindOnce([](Run& run) {
    views::LabelButton* pin = nullptr;
    const std::vector<views::LabelButton*> buttons =
        run.menu ? FindViewsForDemo<views::LabelButton>(run.menu->GetRootView())
                 : std::vector<views::LabelButton*>();
    for (views::LabelButton* button : buttons) {
      if (button->GetText() == u"Pin Group") {
        pin = button;
      }
    }
    run.Expect(pin && pin->GetVisible(), "the today group's menu has no Pin Group");
    if (pin) {
      ClickViewForDemo(pin);
    }
  }));
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(!run.menu || run.menu->IsClosed(), "Pin Group left the menu open");
    run.menu.reset();
    run.Expect(run.Tab(3)->IsPinned() && !run.Tab(3)->GetGroup(), "Pin Group left D unpinned");
    const PinnedGroup* group = run.groups().GroupOf(run.Tab(3));
    run.Expect(group && group->title == kGroupTitle && group->color == kGroupColor,
               "Pin Group lost the name or colour");
    run.Expect(run.Pinned() == "Work stuff:[D A]", "after Pin Group: " + run.Pinned());
    run.group = group ? group->id : std::string();
  }));
  // Saved with the space (the save comes a second after a change).
  run.steps.push_back(base::BindOnce([](Run&) {}));
  run.steps.push_back(base::BindOnce([](Run& run) {
    const base::ListValue* saved = g_browser_process->local_state()
                                       ->GetDict("lb.space_tabs")
                                       .FindList(run.controller()->GetActiveSpaceId());
    std::vector<std::string> grouped;
    for (const SavedTab& tab : saved ? SavedTabsFromList(*saved) : std::vector<SavedTab>()) {
      if (tab.pinned && tab.group_title == kGroupTitle && tab.group_color == kGroupColor) {
        grouped.push_back(base::UTF16ToUTF8(tab.title));
      }
    }
    run.Expect(grouped == std::vector<std::string>{"D", "A"},
               "saved as grouped: " + base::JoinString(grouped, " "));
    // To the other space and back: the parked group comes back.
    run.controller()->SwitchTo(run.controller()->GetActiveIndex() == 0 ? 1 : 0);
  }));
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(run.model()->GetIndexOfTab(run.Tab(0)) < 0, "the other space shows the group");
    run.controller()->SwitchTo(run.controller()->GetActiveIndex() == 0 ? 1 : 0);
  }));
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(run.Pinned() == "Work stuff:[D A]", "after a space switch: " + run.Pinned());
    run.Expect(Shows(HeaderOf(run.browser.get(), run.group)), "no header after a space switch");
    // E, pinned above the group, for the drags.
    content::WebContents* contents =
        chrome::AddAndReturnTabAt(run.browser.get(), Page("E"), -1, /*foreground=*/false);
    tabs::TabInterface* e = tabs::TabInterface::GetFromContents(contents);
    run.model()->SetTabPinned(run.model()->GetIndexOfTab(e), true);
    run.model()->MoveWebContentsAt(run.model()->GetIndexOfTab(e), 0, false);
    run.tabs.push_back(e->GetHandle());  // Tab(4)
  }));
  // Drags with the mouse, each over two steps so the sidebar lays out mid-drag as in a real one:
  // E onto the group's header joins it; E dragged above the header leaves it, but stays in with
  // Esc.
  struct EDrag {
    const char* before;  // the pinned tabs before it
    bool onto_header;    // else above the header
    bool drop;           // else Esc
  };
  for (const EDrag& drag :
       {EDrag{"E Work stuff:[D A]", true, true}, EDrag{"Work stuff:[E D A]", false, false},
        EDrag{"Work stuff:[E D A]", false, true}}) {
    run.steps.push_back(base::BindOnce(
        [](EDrag drag, Run& run) {
          run.Expect(run.Pinned() == drag.before,
                     std::string("before a drag of E: ") + run.Pinned());
          PinnedGroupHeaderView* header = HeaderOf(run.browser.get(), run.group);
          TabView* e = FindTabViewForDemo(run.browser.get(), run.Tab(4));
          if (!header || !e) {
            run.Expect(false, "no header or no row for E");
            return;
          }
          const int to = drag.onto_header ? header->GetBoundsInScreen().CenterPoint().y()
                                          : header->GetBoundsInScreen().y() - 30;
          run.Expect(StartViewDragForDemo(
                         e, DragPathForDemo(e->GetBoundsInScreen().CenterPoint().y(), to)),
                     "E's row went mid-drag");
        },
        drag));
    run.steps.push_back(base::BindOnce(
        [](EDrag drag, Run& run) {
          // Mid-drag, E is already in or out of the group, as the drop would leave it.
          run.Expect(!!run.groups().GroupOf(run.Tab(4)) == drag.onto_header,
                     "mid-drag, E isn't where it is dragged");
          EndTabDragForDemo(run.browser.get(), drag.drop);
        },
        drag));
  }
  // The group's header dragged below "+ New Tab" makes it a today group.
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(run.Pinned() == "E Work stuff:[D A]", "E dragged above the group: " + run.Pinned());
    PinnedGroupHeaderView* header = HeaderOf(run.browser.get(), run.group);
    std::vector<NewTabRowView*> rows = ViewsOf<NewTabRowView>(run.browser.get());
    if (!header || rows.empty()) {
      run.Expect(false, "no header or no New Tab row");
      return;
    }
    const gfx::Point center = header->GetBoundsInScreen().CenterPoint();
    const int to = rows.front()->GetBoundsInScreen().bottom() + 20;
    run.Expect(StartViewDragForDemo(header, DragPathForDemo(center.y(), to)),
               "the header went mid-drag");
    // Where the drag ends, in the header's coordinates, for the drop.
    run.drag_end = gfx::Point(center.x(), to);
    views::View::ConvertPointFromScreen(header, &run.drag_end);
  }));
  // Mid-drag, the group follows the pointer; then it is dropped.
  run.steps.push_back(base::BindOnce([](Run& run) {
    PinnedGroupHeaderView* header = HeaderOf(run.browser.get(), run.group);
    if (!header) {
      run.Expect(false, "the header went mid-drag");
      return;
    }
    // An image of the header and both its tabs follows the pointer over the window, down to
    // where the drag is; the group's tabs fold away under its header.
    TabView* d = FindTabViewForDemo(run.browser.get(), run.Tab(3));
    TabView* a = FindTabViewForDemo(run.browser.get(), run.Tab(0));
    views::Widget* image = header->GetDragImageForTesting();
    run.Expect(image && image->IsVisible(), "no drag image follows the header");
    if (image && d && a) {
      const gfx::Rect bounds = image->GetWindowBoundsInScreen();
      gfx::Point pointer = run.drag_end;
      views::View::ConvertPointToScreen(header, &pointer);
      run.Expect(image->GetContentsView()->children().size() == 3,
                 "the drag image isn't the header and both tabs");
      run.Expect(bounds.y() == pointer.y() - header->height() / 2,
                 "the drag image isn't where the pointer is");
      run.Expect(!Shows(d) && !Shows(a) && Shows(header),
                 "the dragged group's tabs didn't fold away under its header");
      // A line shows where it would land among the today tabs: below "+ New Tab".
      views::Widget* line = GetGroupDropLineForTesting();
      std::vector<NewTabRowView*> new_tab = ViewsOf<NewTabRowView>(run.browser.get());
      run.Expect(line && line->IsVisible() && !new_tab.empty() &&
                     line->GetWindowBoundsInScreen().y() >=
                         new_tab.front()->GetBoundsInScreen().CenterPoint().y(),
                 "no line shows where the group would land among the today tabs");
    }
    // With --lb-ui-snapshot-pinned-drag=<png> the run stops here, mid-drag, and captures it.
    if (base::CommandLine::ForCurrentProcess()->HasSwitch(kSnapshotSwitch)) {
      MaybeScheduleUiSnapshot(header->GetDragImageForTesting(), kSnapshotSwitch);
      run.hold = true;
      return;
    }
    header->OnMouseReleased(ui::MouseEvent(ui::EventType::kMouseReleased, run.drag_end,
                                           run.drag_end, ui::EventTimeForNow(),
                                           ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON));
    run.Expect(!header->GetDragImageForTesting(), "the drag image stayed after the drop");
    run.Expect(!GetGroupDropLineForTesting(), "the line stayed after the drop");
  }));
  // Its header (Chromium's) dragged back above "+ New Tab", onto E, pins it again there.
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(run.Pinned() == "E", "after dragging the header down: " + run.Pinned());
    const std::optional<tab_groups::TabGroupId> today = run.Tab(3)->GetGroup();
    const TabGroup* group = today ? run.model()->group_model()->GetTabGroup(*today) : nullptr;
    run.Expect(group && group->visual_data()->title() == kGroupTitle &&
                   run.Tab(0)->GetGroup() == today && !run.Tab(3)->IsPinned(),
               "dragging the header down made no today group of D and A");
    run.Expect(Shows(FindTabViewForDemo(run.browser.get(), run.Tab(3))),
               "D's row doesn't show after the drop");
    TabGroupHeaderView* header = nullptr;
    for (TabGroupHeaderView* candidate : ViewsOf<TabGroupHeaderView>(run.browser.get())) {
      if (today && candidate->group() == *today && candidate->IsDrawn()) {
        header = candidate;
      }
    }
    TabView* e = FindTabViewForDemo(run.browser.get(), run.Tab(4));
    if (!header || !e) {
      run.Expect(false, "no today group header or no row for E");
      return;
    }
    run.Expect(
        StartViewDragForDemo(header, DragPathForDemo(header->GetBoundsInScreen().CenterPoint().y(),
                                                     e->GetBoundsInScreen().CenterPoint().y())),
        "the today group's header went mid-drag");
  }));
  // Held over the pinned area, a line shows where it would be pinned: above E. Then it is dropped.
  run.steps.push_back(base::BindOnce([](Run& run) {
    views::Widget* line = GetGroupDropLineForTesting();
    TabView* e = FindTabViewForDemo(run.browser.get(), run.Tab(4));
    run.Expect(line && line->IsVisible(), "no line shows where the today group would be pinned");
    if (line && e) {
      const gfx::Rect bounds = line->GetWindowBoundsInScreen();
      run.Expect(std::abs(bounds.CenterPoint().y() - e->GetBoundsInScreen().y()) <= 2,
                 "the line isn't above E");
    }
    EndTabDragForDemo(run.browser.get(), /*drop=*/true);
    run.Expect(!GetGroupDropLineForTesting(), "the line stayed after the drop");
  }));
  // A tab in a collapsed pinned group counts as hidden: D, used more lately than the today tab,
  // is still not shown when E, the shown pinned tab, goes to sleep.
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(run.Pinned() == "Work stuff:[D A] E",
               "after dragging the today group up: " + run.Pinned());
    run.Expect(!run.Tab(3)->GetGroup(), "D is still in a today group");
    // Pinned again, it is a new pinned group.
    const PinnedGroup* group = run.groups().GroupOf(run.Tab(3));
    run.group = group ? group->id : std::string();
    run.model()->ActivateTab(run.Tab(3));  // D
  }));
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.model()->ActivateTab(run.Tab(4));  // E
    run.groups().SetCollapsed(run.group, true);
  }));
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(run.model()->IsTabCollapsed(run.model()->GetIndexOfTab(run.Tab(0))) &&
                   !run.model()->IsTabCollapsed(run.model()->GetIndexOfTab(run.Tab(4))),
               "Chromium doesn't count a collapsed pinned group's tabs as collapsed");
    run.model()->CloseWebContentsAt(run.model()->GetIndexOfTab(run.Tab(4)),
                                    TabCloseTypes::CLOSE_USER_GESTURE);
  }));
  run.steps.push_back(base::BindOnce([](Run& run) {
    const tabs::TabInterface* shown = run.model()->GetActiveTab();
    run.Expect(shown && !shown->IsPinned(),
               "E's sleep showed a tab of the collapsed group instead of the today tab");
    run.groups().SetCollapsed(run.group, false);
    // Back to how the restart demo expects it: E goes.
    if (const int e = run.model()->GetIndexOfTab(run.Tab(4)); e >= 0) {
      run.model()->CloseWebContentsAt(e, TabCloseTypes::CLOSE_NONE);
    }
  }));
}

// --- pinned-groups-drops ----------------------------------------------------------------------

// Tabs of the drops demo, in run.tabs.
enum DropTab : size_t { kL, kP, kS1, kS2, kC1, kC2, kC3, kX1, kX2 };

bool SplitTogether(const Run& run) {
  return run.Tab(kS1) && run.Tab(kS2) && run.Tab(kS1)->GetSplit() &&
         run.Tab(kS1)->GetSplit() == run.Tab(kS2)->GetSplit();
}

void AddDropSteps(Run& run) {
  // Pinned: L, then group G of P and the split view S1|S2. Today: the window's tab, a collapsed
  // group C of c1-c3, then x1 and x2.
  run.steps.push_back(base::BindOnce([](Run& run) {
    for (const char* name : {"L", "P", "S1", "S2", "c1", "c2", "c3", "x1", "x2"}) {
      content::WebContents* contents =
          chrome::AddAndReturnTabAt(run.browser.get(), Page(name), -1, /*foreground=*/false);
      tabs::TabInterface* tab = tabs::TabInterface::GetFromContents(contents);
      if (run.tabs.size() <= kS2) {
        run.model()->SetTabPinned(run.model()->GetIndexOfTab(tab), true);
      }
      run.tabs.push_back(tab->GetHandle());
    }
    std::vector<int> split = {run.model()->GetIndexOfTab(run.Tab(kS1)),
                              run.model()->GetIndexOfTab(run.Tab(kS2))};
    run.model()->RestoreSplit(
        split_tabs::SplitTabId::GenerateNew(), split,
        split_tabs::SplitTabVisualData(split_tabs::SplitTabLayout::kSideBySide, 0.5));
    std::vector<int> c;
    for (size_t i : {kC1, kC2, kC3}) {
      c.push_back(run.model()->GetIndexOfTab(run.Tab(i)));
    }
    const tab_groups::TabGroupId group = run.model()->AddToNewGroup(c);
    run.model()->ChangeTabGroupVisuals(
        group, tab_groups::TabGroupVisualData(u"C", tab_groups::TabGroupColorId::kBlue,
                                              /*is_collapsed=*/true));
    // Made from P and S1 only: the split comes along whole.
    run.group = run.groups().Create({run.Tab(kP), run.Tab(kS1)}, PinnedGroup{.title = u"G"});
  }));
  // G's header dragged below "+ New Tab", between x1 and x2, with C collapsed above them. The
  // drag starts first: G's tabs fold away and the rows below move up.
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(run.Pinned() == "L G:[P S1 S2]", "at start: " + run.Pinned());
    run.Expect(SplitTogether(run), "the split view came apart making the group");
    PinnedGroupHeaderView* header = HeaderOf(run.browser.get(), run.group);
    if (!header) {
      run.Expect(false, "no header for G");
      return;
    }
    const int y = header->GetBoundsInScreen().CenterPoint().y();
    run.Expect(StartViewDragForDemo(header, {y + 10, y + 20}), "the header went mid-drag");
  }));
  run.steps.push_back(base::BindOnce([](Run& run) {
    PinnedGroupHeaderView* header = HeaderOf(run.browser.get(), run.group);
    TabView* x1 = FindTabViewForDemo(run.browser.get(), run.Tab(kX1));
    if (!header || !x1) {
      run.Expect(false, "no header or no row for x1");
      return;
    }
    run.drag_end = gfx::Point(header->GetBoundsInScreen().CenterPoint().x(),
                              x1->GetBoundsInScreen().bottom() + 2);
    views::View::ConvertPointFromScreen(header, &run.drag_end);
    header->OnMouseDragged(ui::MouseEvent(ui::EventType::kMouseDragged, run.drag_end, run.drag_end,
                                          ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON, 0));
  }));
  run.steps.push_back(base::BindOnce([](Run& run) {
    if (PinnedGroupHeaderView* header = HeaderOf(run.browser.get(), run.group)) {
      header->OnMouseReleased(ui::MouseEvent(ui::EventType::kMouseReleased, run.drag_end,
                                             run.drag_end, ui::EventTimeForNow(),
                                             ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON));
    }
  }));
  // It is a today group between x1 and x2, the split whole, C untouched. Its header dragged back
  // above "+ New Tab", above L, pins it there.
  run.steps.push_back(base::BindOnce([](Run& run) {
    TabStripModel* m = run.model();
    const int x1 = m->GetIndexOfTab(run.Tab(kX1));
    const std::optional<tab_groups::TabGroupId> g = run.Tab(kP)->GetGroup();
    run.Expect(g && m->GetIndexOfTab(run.Tab(kP)) == x1 + 1 &&
                   m->GetIndexOfTab(run.Tab(kS2)) + 1 == m->GetIndexOfTab(run.Tab(kX2)) &&
                   run.Tab(kS2)->GetGroup() == g,
               "G isn't a today group between x1 and x2: " + run.Today());
    run.Expect(SplitTogether(run), "the split view came apart unpinning the group");
    run.Expect(run.Tab(kC1)->GetGroup() && run.Tab(kC1)->GetGroup() == run.Tab(kC3)->GetGroup(),
               "C was split");
    TabGroupHeaderView* header = nullptr;
    for (TabGroupHeaderView* candidate : ViewsOf<TabGroupHeaderView>(run.browser.get())) {
      if (g && candidate->group() == *g && candidate->IsDrawn()) {
        header = candidate;
      }
    }
    TabView* l = FindTabViewForDemo(run.browser.get(), run.Tab(kL));
    if (!header || !l) {
      run.Expect(false, "no header for G or no row for L");
      return;
    }
    run.Expect(
        StartViewDragForDemo(header, DragPathForDemo(header->GetBoundsInScreen().CenterPoint().y(),
                                                     l->GetBoundsInScreen().y() + 2)),
        "G's today header went mid-drag");
  }));
  run.steps.push_back(
      base::BindOnce([](Run& run) { EndTabDragForDemo(run.browser.get(), /*drop=*/true); }));
  // Pinned again above L, the split whole. Close Group puts every tab to sleep, the split's too.
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(run.Pinned() == "G:[P S1 S2] L", "after pinning G above L: " + run.Pinned());
    run.Expect(SplitTogether(run), "the split view came apart pinning the group");
    const PinnedGroup* group = run.groups().GroupOf(run.Tab(kP));
    run.group = group ? group->id : std::string();
    run.groups().Close(run.group);
  }));
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(run.Tab(kP) && run.Tab(kS1) && run.Tab(kS2), "Close Group closed a tab");
    run.Expect(run.Tab(kP) && IsAsleep(*run.Tab(kP)) && run.Tab(kS1) && IsAsleep(*run.Tab(kS1)) &&
                   run.Tab(kS2) && IsAsleep(*run.Tab(kS2)),
               "Close Group left a tab awake");
    run.Expect(run.Pinned() == "G:[P S1 S2] L", "after Close Group: " + run.Pinned());
    run.Expect(run.groups().AllTabsAsleep(run.group), "the menu wouldn't offer Remove");
    // Tab > Group Tab on L makes it a pinned group of its own.
    run.Expect(TogglePinnedGroup({run.Tab(kL)}) && run.groups().GroupOf(run.Tab(kL)),
               "Group Tab made no pinned group of L");
  }));
  // L, the only tab of its group, dragged a little and dropped in place stays in it.
  run.steps.push_back(base::BindOnce([](Run& run) {
    TabView* l = FindTabViewForDemo(run.browser.get(), run.Tab(kL));
    if (!l) {
      run.Expect(false, "no row for L");
      return;
    }
    const int y = l->GetBoundsInScreen().CenterPoint().y();
    run.Expect(StartViewDragForDemo(l, {y + 12, y + 2}), "L's row went mid-drag");
  }));
  run.steps.push_back(
      base::BindOnce([](Run& run) { EndTabDragForDemo(run.browser.get(), /*drop=*/true); }));
  // Group Tab again takes it out; then unpinning and pinning it in place keeps it out.
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(run.groups().GroupOf(run.Tab(kL)), "L left its group, dragged in place");
    run.Expect(TogglePinnedGroup({run.Tab(kL)}) && !run.groups().GroupOf(run.Tab(kL)),
               "Group Tab didn't take L out of its group");
    run.Expect(TogglePinnedGroup({run.Tab(kL)}), "Group Tab made no group of L again");
    const int index = run.model()->GetIndexOfTab(run.Tab(kL));
    run.model()->SetTabPinned(index, false);
    run.Expect(run.model()->GetIndexOfTab(run.Tab(kL)) == index, "unpinning moved L");
    run.model()->SetTabPinned(index, true);
    run.Expect(!run.groups().GroupOf(run.Tab(kL)), "L, unpinned and pinned again, is in a group");
  }));
  // G collapsed while it holds the shown tab, unpinned: a today group, expanded, P shown.
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.model()->ActivateTab(run.Tab(kP));
    run.groups().SetCollapsed(run.group, true);
    run.groups().Unpin(run.group);
  }));
  run.steps.push_back(base::BindOnce([](Run& run) {
    const std::optional<tab_groups::TabGroupId> g = run.Tab(kP)->GetGroup();
    const TabGroup* group = g ? run.model()->group_model()->GetTabGroup(*g) : nullptr;
    run.Expect(group && !group->visual_data()->is_collapsed(),
               "G, holding the shown tab, was unpinned collapsed");
    run.Expect(Shows(FindTabViewForDemo(run.browser.get(), run.Tab(kP))),
               "the shown tab's row is hidden");
  }));
}

// --- pinned-groups-restart --------------------------------------------------------------------

// The group's header is filled with the group's strip colour, as Chromium's group headers are.
void CheckHeaderFill(Run& run, const PinnedGroup& group) {
  PinnedGroupHeaderView* header = HeaderOf(run.browser.get(), group.id);
  const ui::ColorProvider* colors = header ? header->GetColorProvider() : nullptr;
  const views::Background* fill = header ? header->GetBackground() : nullptr;
  bool in_group_colour = false;
  if (colors && fill) {
    const SkColor painted = fill->color().ResolveToSkColor(colors);
    const auto color = static_cast<tab_groups::TabGroupColorId>(group.color);
    for (bool active : {true, false}) {
      in_group_colour =
          in_group_colour || painted == colors->GetColor(GetTabGroupTabStripColorId(color, active));
    }
  }
  run.Expect(in_group_colour, "the header isn't filled with its group's colour");
}

void AddRestartSteps(Run& run) {
  run.steps.push_back(base::BindOnce([](Run& run) {
    run.Expect(run.Pinned() == "Work stuff:[D A]", "after a restart: " + run.Pinned());
    const PinnedGroup* group =
        run.model()->count() ? run.groups().GroupOf(run.model()->GetTabAtIndex(0)) : nullptr;
    run.Expect(group && group->color == kGroupColor, "the colour didn't come back");
    run.Expect(group && Shows(HeaderOf(run.browser.get(), group->id)), "no header after restart");
    if (group) {
      CheckHeaderFill(run, *group);
    }
  }));
}

}  // namespace

bool MaybeRunPinnedGroupsDemo(BrowserWindowInterface* browser, const std::string& mode) {
  if (mode != kDemo && mode != kRestartDemo && mode != kDropsDemo) {
    return false;
  }
  auto run = std::make_unique<Run>();
  run->mode = mode;
  run->browser = browser->GetWeakPtr();
  if (mode == kDemo) {
    AddSteps(*run);
  } else if (mode == kDropsDemo) {
    AddDropSteps(*run);
  } else {
    AddRestartSteps(*run);
  }
  Start(std::move(run));
  return true;
}

}  // namespace lb
