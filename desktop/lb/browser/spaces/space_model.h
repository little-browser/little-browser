// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_SPACES_SPACE_MODEL_H_
#define LB_BROWSER_SPACES_SPACE_MODEL_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/function_ref.h"
#include "base/values.h"
#include "url/gurl.h"

namespace lb {

// Space rules that don't need a window or views (unit tested in space_model_unittest.cc).

// Space icons and colours offered in menus.
inline constexpr const char* kSpaceIcons[] = {"\U0001F3E0",   "\U0001F4BC", "\U0001F3A8",
                                              "\U0001F4DA",   "\U0001F9EA", "\U0001F3AE",
                                              "\u2708\uFE0F", "\U0001F331"};
struct SpaceColour {
  const char16_t* name;
  int hue;
};
inline constexpr SpaceColour kSpaceColours[] = {{u"Blue", 222}, {u"Teal", 180},  {u"Green", 140},
                                                {u"Amber", 40}, {u"Orange", 24}, {u"Red", 0},
                                                {u"Pink", 330}, {u"Purple", 270}};

// The first space's colour (blue).
inline constexpr int kFirstSpaceHue = 222;

// A space (docs/ux.md §5): a named set of tabs with an icon, a colour and a profile.
// Spaces are browser-wide; each runs in one profile (its logins, history, favourites), and
// several spaces can share a profile.
struct Space {
  std::string id;
  std::string name;
  std::string icon;
  int hue = kFirstSpaceHue;
  // Profile directory name ("Default", "Profile 1", …) and its display name.
  std::string profile;
  std::u16string profile_name;
};

// The saved form of a space (a Local State `lb.spaces` entry). Its tabs are saved separately.
base::DictValue SpaceToDict(const Space& space);

// Reads a saved space. Null without an id; missing fields get defaults, and a missing or empty
// profile becomes `fallback_profile`. `profile_name` is left empty (the caller looks it up).
std::optional<Space> SpaceFromDict(const base::DictValue& dict, std::string_view fallback_profile);

// Defaults for the next new space, given the existing ones: colours step around the wheel so
// neighbours differ, icons cycle through kSpaceIcons, names count up ("Space 3").
int NextSpaceHue(const std::vector<Space>& spaces);
const char* NextSpaceIcon(const std::vector<Space>& spaces);
std::string NextSpaceName(const std::vector<Space>& spaces);

// Which space a new window of `profile` opens on: the profile's last active space, else its
// first space no window shows; during session restore (the window is put on its saved space
// right after), any of its spaces. Null means "create a new space".
std::optional<std::string> ChooseSpaceForWindow(
    const std::vector<Space>& spaces,
    std::string_view profile,
    std::string_view last_active_id,
    base::FunctionRef<bool(const std::string& id)> is_shown,
    bool restoring_session);

// A page nobody has used yet: empty, about:blank or the new-tab page.
bool IsBlankTabUrl(const GURL& url);

// Same host (favourites match open tabs this way).
bool SameSite(const GURL& a, const GURL& b);

// A saved tab of a space, as in Local State lb.space_tabs[space id]. Every space is saved
// there, also those a window shows (continuously), so their tabs survive a restart.
struct SavedTab {
  GURL url;
  std::u16string title;
  bool pinned = false;
  // The space's active tab: switching back to the space shows it (docs/ux.md §5).
  bool active = false;
  // Tab group, if the tab was in one: neighbouring tabs with the same id form one group.
  std::string group_id;
  std::u16string group_title;
  int group_color = 0;  // tab_groups::TabGroupColorId
  bool group_collapsed = false;
  // The group's entry in Chromium's saved tab groups (TabGroupSyncService), if it had one: a
  // rebuilt group reconnects to it instead of being saved again as a new group.
  std::string group_saved_guid;
  // Split view, if the tab was in one: the two neighbouring tabs with the same id.
  std::string split_id;
  int split_layout = 0;  // split_tabs::SplitTabLayout
  double split_ratio = 0.5;
};

base::DictValue SavedTabToDict(const SavedTab& tab);

// The valid records of a saved tab list, in order; malformed records and invalid URLs are skipped.
std::vector<SavedTab> SavedTabsFromList(const base::ListValue& list);

// Which of `tabs` to show first: the one saved as active, else the first. 0 for an empty list.
size_t ActiveSavedTab(const std::vector<SavedTab>& tabs);

// The tab groups among `tabs`: for each, the indexes of its tabs (neighbours with the same
// group id), in order.
std::vector<std::vector<size_t>> SavedTabGroups(const std::vector<SavedTab>& tabs);

// The split views among `tabs`: for each, the indexes of its two tabs (neighbours with the same
// split id). Runs that aren't exactly two tabs are left out.
std::vector<std::vector<size_t>> SavedTabSplits(const std::vector<SavedTab>& tabs);

}  // namespace lb

#endif  // LB_BROWSER_SPACES_SPACE_MODEL_H_
