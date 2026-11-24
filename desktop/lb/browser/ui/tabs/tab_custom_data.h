// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_TABS_TAB_CUSTOM_DATA_H_
#define LB_BROWSER_UI_TABS_TAB_CUSTOM_DATA_H_

#include <map>
#include <optional>
#include <string>

#include "lb/browser/tabs/tab_names.h"
#include "url/gurl.h"

class TabStripModel;

namespace content {
class WebContents;
}  // namespace content

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace ui {
class SimpleMenuModel;
}  // namespace ui

namespace lb {

// A tab's name and pinned link (docs/ux.md §6, rules in lb/browser/tabs/tab_names.h), kept by the
// tab itself (its TabHandle), so they stay when the tab sleeps (a discard gives it a new page) and
// move with it between spaces and windows. They are saved in Chromium's session (tab extra data,
// patches/lb/tab-names.patch) and in the space's saved tabs.

// What is set on `tab`; empty when nothing is.
TabCustom GetTabCustom(const tabs::TabInterface* tab);
// Sets it: the tab's row redraws and the session and the space's saved tabs are updated.
void SetTabCustom(tabs::TabInterface* tab, TabCustom custom);
// Sets `name` (cleaned; empty gives the row the page's title again).
void RenameTab(tabs::TabInterface* tab, std::u16string_view name);
// Sets a pinned tab's link to `url` and loads it there.
void SetPinnedUrl(tabs::TabInterface* tab, const GURL& url);
// Loads a pinned tab's link in it again.
void GoBackToPinnedUrl(tabs::TabInterface* tab);

// The page a pinned tab goes back to: its link, or, until it has committed a real page to take
// as one, the page it shows (lb::PinnedUrlOrPage). Empty for a tab that isn't pinned.
GURL PinnedUrlOf(const tabs::TabInterface* tab);
// Whether pinned `tab` shows a page other than its link.
bool HasLeftPinnedUrl(const tabs::TabInterface* tab);
// The page `tab` shows, or is about to load.
GURL CurrentUrlOf(const tabs::TabInterface* tab);

// Hooks (patches/lb/tab-names.patch, patches/lb/tab-names-ui.patch):
// The title of `tab`'s row and its accessible name: its name, or `page_title`. Its name also means
// no "Loading…" title.
std::u16string TabRowTitle(const tabs::TabInterface* tab, std::u16string page_title);
bool HasTabName(const tabs::TabInterface* tab);
// The tab menu (TabMenuModel) of the tab at `index`: adds Rename Tab… and the pinned link items.
void AddTabNameMenuItems(ui::SimpleMenuModel* menu, TabStripModel* model, int index);
// Whether `command_id` is one of those items: whether it is enabled, or nullopt if it isn't one.
std::optional<bool> IsTabNameCommandEnabled(TabStripModel* model, int index, int command_id);
// Runs it, if it is one of those items; false otherwise.
bool ExecuteTabNameCommand(TabStripModel* model, int index, int command_id);
// The session's tab extra data for `contents`'s tab (kTabCustomSessionKey); empty when nothing
// is set.
std::string GetTabCustomSessionData(content::WebContents* contents);

// Called by lb::OnTabRestored: a restored tab gets its name and link back.
void RestoreTabCustom(tabs::TabInterface* tab,
                      const std::map<std::string, std::string>& extra_data);
// Called by SidebarController when a tab is pinned or unpinned, and when it changes (its page
// commits, or it sleeps and gets a new one): its link settles (lb::SettleTabLink), and a tab with
// a new page writes its name and link to the session again. During a tab drag, which pins and
// unpins tabs as they cross "+ New Tab", links wait for the drag to end.
void OnTabPinnedChangedForNames(tabs::TabInterface* tab);
void OnTabChangedForNames(tabs::TabInterface* tab);
// Called by lb::OnTabDragEnded once Chromium has put a cancelled drag's tabs back.
void OnTabDragEndedForNames();

}  // namespace lb

#endif  // LB_BROWSER_UI_TABS_TAB_CUSTOM_DATA_H_
