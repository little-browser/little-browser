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
  // A pinned tab closed (put to sleep) while it showed another page than its link: it loads its
  // link, not that page, when it is shown again.
  bool wakes_at_link = false;
  // The title of a pinned tab's page at its link, which its row keeps while it shows other pages.
  std::u16string pinned_title;

  bool empty() const { return name.empty() && !pinned_url.is_valid(); }
  bool operator==(const TabCustom&) const = default;
};

// A TabCustom as fields of a record ("name", "pinned_url", "wakes_at_link", "pinned_title"), as
// both the session's data and the space's saved tabs keep it: nothing is written for what isn't
// set. Reading cleans the name (CleanTabName) and drops an invalid link, and wakes_at_link and
// pinned_title without a link.
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

// The title a tab row shows: the user's name for it; for a pinned tab `asleep` away from its link
// (`wakes_at_link`), its link's page title; otherwise the page's title. Awake, even woken in the
// background with `wakes_at_link` still set, it follows its page.
std::u16string TabTitleToShow(const TabCustom& custom,
                              const std::u16string& page_title,
                              bool asleep);

// Whether the row's title is the tab's own (a name, or a sleeping pinned tab's link's title), not
// the page's: the row then shows no "Loading…" title.
bool HasOwnTitle(const TabCustom& custom, bool asleep);

// Whether a pinned tab is showing a page other than its link (docs/ux.md §6: closing it then takes
// it back to its link). The part after "#" doesn't count. False without a link or a page.
bool HasLeftPinnedUrl(const GURL& pinned_url, const GURL& current_url);

// The page a pinned tab goes back to: its link, or, while it has none yet, the page it shows.
GURL PinnedUrlOrPage(const TabCustom& custom, const GURL& current_url);

// A tab's TabCustom once its pin state or its page has settled: an unpinned tab has no link (and
// doesn't wake at one or keep its title); a pinned tab without one takes the page it has committed,
// unless that is blank (it takes the first real page it commits). A link it has is kept.
TabCustom SettleTabLink(TabCustom custom, bool pinned, const GURL& committed_url);

// A restored tab's TabCustom (from the session or the space's saved tabs) once it is in its
// window: its link settles (SettleTabLink), and a tab already shown, which the user sees as it was,
// no longer wakes at its link.
TabCustom RestoredTabCustom(TabCustom custom, bool pinned, const GURL& committed_url, bool shown);

// A pinned tab's TabCustom when its page has `page_title` (empty: none set yet) at
// `committed_url`: on its link, the title is the link's title; on another page, the link's title
// stays.
TabCustom TakePinnedTitle(TabCustom custom,
                          const GURL& committed_url,
                          std::u16string_view page_title);

// A pinned tab's TabCustom with `url` as its link: a link to another page drops the old link's
// title (the new page's comes with it), and the tab no longer wakes at its link (it is on the new
// one, or loads it).
TabCustom WithPinnedUrl(TabCustom custom, const GURL& url);

// A pinned tab's TabCustom once a close has put it to sleep (docs/ux.md §6): it wakes at its link
// if it was showing another page (`current_url`), so closing a pinned tab takes it back to its
// link. A tab without a link is left as it is.
TabCustom AfterPinnedSleep(TabCustom custom, const GURL& current_url);

// What showing a tab does with its link: nothing, or load the link (it was closed away from it);
// either way the TabCustom it keeps is `custom` without wakes_at_link.
struct TabShown {
  bool load_link = false;
  TabCustom custom;
};
TabShown OnTabShown(TabCustom custom);

// A typed pinned link as the tab keeps it, read as the command bar reads an address; nullopt
// when the text isn't an address (plain words would be a search, not a link).
std::optional<GURL> PinnedUrlFromInput(std::u16string_view typed);

// The tab menu's items for names and links, after Pin/Unpin.
enum class TabNameCommand {
  kRename,
  kEditPinnedUrl,
  kUsePageAsPinnedUrl,
};
struct TabNameMenuItem {
  TabNameCommand command;
  std::u16string label;
  bool enabled = true;
  bool operator==(const TabNameMenuItem&) const = default;
};
// Which items the menu of one tab shows: Rename Tab… for every tab; for a pinned tab also Edit
// Link… and Use Current Page as Link, the last enabled only once it has left its link (closing it
// goes back to its link). A menu for several tabs shows none: each acts on one tab.
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
