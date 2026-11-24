// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_TABS_TAB_NAMES_H_
#define LB_BROWSER_TABS_TAB_NAMES_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "url/gurl.h"

namespace lb {

// What the user set on a tab (docs/ux.md §6): a name of their own, shown instead of the page's
// title, and, for a pinned tab, its link, the page it goes back to.
struct TabCustom {
  std::u16string name;
  GURL pinned_url;

  bool empty() const { return name.empty() && !pinned_url.is_valid(); }
  bool operator==(const TabCustom&) const = default;
};

// A TabCustom as fields of a record ("name", "pinned_url"), as both the session's data and the
// space's saved tabs keep it: nothing is written for what isn't set. Reading cleans the name
// (CleanTabName) and drops an invalid link.
void WriteTabCustom(const TabCustom& custom, base::DictValue& record);
TabCustom ReadTabCustom(const base::DictValue& record);

// A tab's TabCustom in Chromium's session (tab extra data under this key), so it comes back
// when the window is restored. A tab with nothing set has empty data.
inline constexpr char kTabCustomSessionKey[] = "lb_tab";
std::string TabCustomToSessionData(const TabCustom& custom);
// The TabCustom in data written by TabCustomToSessionData; nullopt for empty or bad data. An
// invalid link is dropped.
std::optional<TabCustom> TabCustomFromSessionData(std::string_view data);

// A typed tab name as the tab keeps it: runs of spaces and line breaks become one space, and
// the ends are trimmed. Empty means the page's own title again.
std::u16string CleanTabName(std::u16string_view typed);

// The name a tab keeps when `typed` is entered while its page's title is `page_title`: the
// cleaned text, or none when that is the page's own title (unchanged, it would stop following
// the page's title).
std::u16string NameFromInput(std::u16string_view typed, std::u16string_view page_title);

// The title a tab row shows: the user's name for it, or the page's title.
std::u16string TabTitleToShow(const TabCustom& custom, const std::u16string& page_title);

// Whether a pinned tab is showing a page other than its link (docs/ux.md §2: "back to pinned
// page"). The part after "#" doesn't count. False without a link or a page.
bool HasLeftPinnedUrl(const GURL& pinned_url, const GURL& current_url);

// The page a pinned tab goes back to: its link, or, while it has none yet, the page it shows.
GURL PinnedUrlOrPage(const TabCustom& custom, const GURL& current_url);

// A tab's TabCustom once its pin state or its page has settled: an unpinned tab has no link; a
// pinned tab without one takes the page it has committed, unless that is blank (it takes the
// first real page it commits). A link it has is kept.
TabCustom SettleTabLink(TabCustom custom, bool pinned, const GURL& committed_url);

// A typed pinned link as the tab keeps it, read as the command bar reads an address; nullopt
// when the text isn't an address (plain words would be a search, not a link).
std::optional<GURL> PinnedUrlFromInput(std::u16string_view typed);

// The tab menu's items for names and links, after Pin/Unpin.
enum class TabNameCommand {
  kRename,
  kEditPinnedUrl,
  kUsePageAsPinnedUrl,
  kBackToPinnedUrl,
};
struct TabNameMenuItem {
  TabNameCommand command;
  std::u16string label;
  bool enabled = true;
  bool operator==(const TabNameMenuItem&) const = default;
};
// Which items the menu of one tab shows: Rename Tab… for every tab; for a pinned tab also Edit
// Link…, Use Current Page as Link and Back to Pinned Page, the last two only once it has left its
// link. A menu for several tabs shows none: each acts on one tab.
std::vector<TabNameMenuItem> TabNameMenuItems(size_t tabs, bool pinned, bool left_pinned_url);

// How many tabs a tab menu acts on, for TabNameMenuItems: one when the tab right-clicked isn't
// selected; otherwise the selection, counting the tab's split view, which Chromium always selects
// whole, as one. `selected` is the selection's size, `selected_in_split` how many of it are in
// the right-clicked tab's split view (0 if it isn't in one).
size_t TabMenuTargets(bool right_clicked_selected, size_t selected, size_t selected_in_split);

// The command ids of those items in Chromium's tab menu (TabMenuModel), below its submenus'
// ranges (ExistingBaseSubMenuModel's start at 1000).
inline constexpr int kFirstTabNameCommandId = 900;
int TabNameCommandId(TabNameCommand command);
std::optional<TabNameCommand> TabNameCommandFromId(int id);

}  // namespace lb

#endif  // LB_BROWSER_TABS_TAB_NAMES_H_
