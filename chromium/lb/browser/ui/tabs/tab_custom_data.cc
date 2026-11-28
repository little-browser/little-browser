// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/tabs/tab_custom_data.h"

#include <map>
#include <set>
#include <utility>
#include <vector>

#include "base/no_destructor.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_change_type.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "lb/browser/ui/tabs/pinned_tab_sleep.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "ui/base/page_transition_types.h"
#include "ui/menus/simple_menu_model.h"

namespace lb {

namespace {

// What is set on a tab, and the page its session data was last written for: a sleeping tab gets
// a new page, which the session knows as a new tab.
struct Entry {
  TabCustom custom;
  const void* written_for = nullptr;
};

std::map<tabs::TabHandle, Entry>& Entries() {
  static base::NoDestructor<std::map<tabs::TabHandle, Entry>> entries;
  return *entries;
}

// Tabs whose pin state changed during the tab drag under way.
std::set<tabs::TabHandle>& DraggedTabs() {
  static base::NoDestructor<std::set<tabs::TabHandle>> tabs;
  return *tabs;
}

// Forgets tabs that are gone.
void Prune() {
  std::erase_if(Entries(), [](const auto& entry) { return !entry.first.Get(); });
}

// Writes `tab`'s TabCustom into the session, as tab extra data.
void WriteSession(tabs::TabInterface* tab) {
  content::WebContents* contents = tab->GetContents();
  BrowserWindowInterface* browser = tab->GetBrowserWindowInterface();
  SessionService* session =
      contents && browser ? SessionServiceFactory::GetForProfileIfExisting(browser->GetProfile())
                          : nullptr;
  if (!session) {
    return;
  }
  const SessionID window = sessions::SessionTabHelper::IdForWindowContainingTab(contents);
  const SessionID id = sessions::SessionTabHelper::IdForTab(contents);
  if (window.is_valid() && id.is_valid()) {
    session->AddTabExtraData(window, id, kTabCustomSessionKey,
                             TabCustomToSessionData(GetTabCustom(tab)));
    if (const auto it = Entries().find(tab->GetHandle()); it != Entries().end()) {
      it->second.written_for = contents;
    }
  }
}

// Loads a pinned tab's link (or a new one) in it; it no longer has to wake at its link.
void LoadUrl(tabs::TabInterface* tab, const GURL& url) {
  if (TabCustom custom = GetTabCustom(tab); custom.wakes_at_link) {
    custom.wakes_at_link = false;
    SetTabCustom(tab, std::move(custom));
  }
  content::NavigationController::LoadURLParams params(url);
  params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
  tab->GetContents()->GetController().LoadURLWithParams(params);
}

tabs::TabInterface* TabAt(TabStripModel* model, int index) {
  return model && model->ContainsIndex(index) ? model->GetTabAtIndex(index) : nullptr;
}

// How many tabs a menu opened on the tab at `index` acts on (lb::TabMenuTargets).
size_t MenuTabCount(TabStripModel* model, int index) {
  size_t selected_in_split = 0;
  if (const std::optional<split_tabs::SplitTabId> split = model->GetSplitForTab(index)) {
    if (const split_tabs::SplitTabData* data = model->GetSplitData(*split)) {
      for (tabs::TabInterface* tab : data->ListTabs()) {
        const int tab_index = model->GetIndexOfTab(tab);
        if (tab_index >= 0 && model->IsTabSelected(tab_index)) {
          ++selected_in_split;
        }
      }
    }
  }
  return TabMenuTargets(model->IsTabSelected(index), model->selection_model().size(),
                        selected_in_split);
}

std::vector<TabNameMenuItem> MenuItemsFor(TabStripModel* model, int index) {
  tabs::TabInterface* tab = TabAt(model, index);
  if (!tab) {
    return {};
  }
  return TabNameMenuItems(MenuTabCount(model, index), tab->IsPinned(), HasLeftPinnedUrl(tab));
}

GURL CommittedUrlOf(const tabs::TabInterface* tab) {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  return contents ? contents->GetLastCommittedURL() : GURL();
}

// The title the page set itself (not one made from its address); empty while it has none.
std::u16string CommittedTitleOf(const tabs::TabInterface* tab) {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  content::NavigationEntry* entry =
      contents ? contents->GetController().GetLastCommittedEntry() : nullptr;
  return entry ? entry->GetTitle() : std::u16string();
}

// The tab's link settles, and a pinned tab on its link takes its page's title as the link's.
void SettleLink(tabs::TabInterface* tab) {
  const GURL committed = CommittedUrlOf(tab);
  SetTabCustom(tab, TakePinnedTitle(SettleTabLink(GetTabCustom(tab), tab->IsPinned(), committed),
                                    committed, CommittedTitleOf(tab)));
}

}  // namespace

TabCustom GetTabCustom(const tabs::TabInterface* tab) {
  if (!tab) {
    return TabCustom();
  }
  const auto it = Entries().find(const_cast<tabs::TabInterface*>(tab)->GetHandle());
  return it == Entries().end() ? TabCustom() : it->second.custom;
}

void SetTabCustom(tabs::TabInterface* tab, TabCustom custom) {
  if (!tab || GetTabCustom(tab) == custom) {
    return;
  }
  Prune();
  if (custom.empty()) {
    Entries().erase(tab->GetHandle());
  } else {
    Entries()[tab->GetHandle()].custom = std::move(custom);
  }
  WriteSession(tab);
  // The row reads its title again, as after the page set a new one (the row's TabData listens to
  // TabUIHelper), and the window's controller saves the space's tabs (OnTabChangedAt).
  if (TabUIHelper* helper = TabUIHelper::From(tab)) {
    helper->TitleWasSet(nullptr);
  }
  if (BrowserWindowInterface* browser = tab->GetBrowserWindowInterface()) {
    browser->GetTabStripModel()->NotifyTabChanged(tab, TabChangeType::kAll);
  }
}

void RenameTab(tabs::TabInterface* tab, std::u16string_view name) {
  TabCustom custom = GetTabCustom(tab);
  custom.name = CleanTabName(name);
  SetTabCustom(tab, std::move(custom));
}

void SetPinnedUrl(tabs::TabInterface* tab, const GURL& url) {
  if (!tab || !tab->IsPinned() || !url.is_valid()) {
    return;
  }
  SetTabCustom(tab, WithPinnedUrl(GetTabCustom(tab), url));
  SettleLink(tab);  // already on the new link: its title is the link's
  if (lb::HasLeftPinnedUrl(url, CurrentUrlOf(tab))) {
    LoadUrl(tab, url);
  }
}

GURL CurrentUrlOf(const tabs::TabInterface* tab) {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!contents) {
    return GURL();
  }
  return contents->GetLastCommittedURL().is_empty() ? contents->GetVisibleURL()
                                                    : contents->GetLastCommittedURL();
}

GURL PinnedUrlOf(const tabs::TabInterface* tab) {
  if (!tab || !tab->IsPinned()) {
    return GURL();
  }
  return PinnedUrlOrPage(GetTabCustom(tab), CurrentUrlOf(tab));
}

bool HasLeftPinnedUrl(const tabs::TabInterface* tab) {
  return tab && tab->IsPinned() && lb::HasLeftPinnedUrl(PinnedUrlOf(tab), CurrentUrlOf(tab));
}

std::u16string TabRowTitle(const tabs::TabInterface* tab, std::u16string page_title) {
  return TabTitleToShow(GetTabCustom(tab), page_title, tab && IsAsleep(*tab));
}

bool HasTabName(const tabs::TabInterface* tab) {
  return HasOwnTitle(GetTabCustom(tab), tab && IsAsleep(*tab));
}

std::u16string TabTitleWithoutName(const tabs::TabInterface* tab, std::u16string page_title) {
  TabCustom custom = GetTabCustom(tab);
  custom.name.clear();
  return TabTitleToShow(custom, page_title, tab && IsAsleep(*tab));
}

void AddTabNameMenuItems(ui::SimpleMenuModel* menu, TabStripModel* model, int index) {
  if (!menu || !SidebarController::FromTabStripModel(model)) {
    return;  // pop-ups and apps keep Chromium's menu
  }
  for (const TabNameMenuItem& item : MenuItemsFor(model, index)) {
    menu->AddItem(TabNameCommandId(item.command), item.label);
  }
}

std::optional<bool> IsTabNameCommandEnabled(TabStripModel* model, int index, int command_id) {
  const std::optional<TabNameCommand> command = TabNameCommandFromId(command_id);
  if (!command) {
    return std::nullopt;
  }
  for (const TabNameMenuItem& item : MenuItemsFor(model, index)) {
    if (item.command == *command) {
      return item.enabled;
    }
  }
  return false;
}

bool ExecuteTabNameCommand(TabStripModel* model, int index, int command_id) {
  const std::optional<TabNameCommand> command = TabNameCommandFromId(command_id);
  if (!command) {
    return false;
  }
  tabs::TabInterface* tab = TabAt(model, index);
  SidebarController* controller = SidebarController::FromTabStripModel(model);
  if (!tab || !controller || IsTabNameCommandEnabled(model, index, command_id) != true) {
    return true;
  }
  switch (*command) {
    case TabNameCommand::kRename:
      controller->ShowCommandBarForTab(CommandBarMode::kRenameTab, tab);
      break;
    case TabNameCommand::kEditPinnedUrl:
      controller->ShowCommandBarForTab(CommandBarMode::kEditPinnedUrl, tab);
      break;
    case TabNameCommand::kUsePageAsPinnedUrl:
      SetPinnedUrl(tab, CurrentUrlOf(tab));
      break;
  }
  return true;
}

std::string GetTabCustomSessionData(content::WebContents* contents) {
  tabs::TabInterface* tab = contents ? tabs::TabInterface::MaybeGetFromContents(contents) : nullptr;
  if (!tab) {
    return std::string();
  }
  if (const auto it = Entries().find(tab->GetHandle()); it != Entries().end()) {
    it->second.written_for = contents;  // a session rebuild writes it for this page
  }
  return TabCustomToSessionData(GetTabCustom(tab));
}

void RestoreTabCustom(tabs::TabInterface* tab,
                      const std::map<std::string, std::string>& extra_data) {
  const auto data = extra_data.find(kTabCustomSessionKey);
  if (!tab || data == extra_data.end()) {
    return;
  }
  if (std::optional<TabCustom> custom = TabCustomFromSessionData(data->second)) {
    // A restored window shows its selected tab before this runs (lb::RestoredTabCustom).
    SetTabCustom(tab, RestoredTabCustom(std::move(*custom), tab->IsPinned(), CommittedUrlOf(tab),
                                        tab->IsActivated()));
  }
}

void OnTabPinnedChangedForNames(tabs::TabInterface* tab) {
  if (!tab) {
    return;
  }
  if (TabDragController::IsActive()) {
    DraggedTabs().insert(tab->GetHandle());
    return;
  }
  SettleLink(tab);
}

void OnTabChangedForNames(tabs::TabInterface* tab) {
  if (!tab) {
    return;
  }
  // A tab that slept has a new page, which the session knows as a new tab: write its name and
  // link for it.
  if (const auto it = Entries().find(tab->GetHandle());
      it != Entries().end() && it->second.written_for != tab->GetContents()) {
    WriteSession(tab);
  }
  if (!TabDragController::IsActive()) {
    SettleLink(tab);
  }
}

void OnTabShownForNames(tabs::TabInterface* tab) {
  if (!tab || !GetTabCustom(tab).wakes_at_link) {
    return;
  }
  const TabShown shown = OnTabShown(GetTabCustom(tab));
  SetTabCustom(tab, shown.custom);
  if (shown.load_link) {
    LoadUrl(tab, shown.custom.pinned_url);
  }
}

void OnTabDragEndedForNames() {
  std::set<tabs::TabHandle> dragged;
  std::swap(dragged, DraggedTabs());
  for (const tabs::TabHandle& handle : dragged) {
    if (tabs::TabInterface* tab = handle.Get()) {
      SettleLink(tab);
    }
  }
}

}  // namespace lb
