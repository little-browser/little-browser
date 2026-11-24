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
#include "components/tab_groups/tab_group_color.h"
#include "url/gurl.h"

namespace lb {

// Spaces on desktop: the saved form (Local State) and Chromium's types around the space rules,
// which are lb-core's (lb_core::spaces, through lb/core_bridge/rules.rs.h) so every platform
// shares them. Unit tested in space_model_unittest.cc.

// Space icons and colours offered in menus, in the order new spaces take them.
const std::vector<std::string>& SpaceIcons();
struct SpaceColour {
  std::u16string name;
  int hue = 0;
};
const std::vector<SpaceColour>& SpaceColours();

// The first space's colour (blue).
int FirstSpaceHue();

// A space (docs/ux.md §5): a named set of tabs with an icon, a colour and a profile.
// Spaces are browser-wide; each runs in one profile (its logins and history), and
// several spaces can share a profile.
struct Space {
  std::string id;
  std::string name;
  std::string icon;
  int hue = FirstSpaceHue();
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
// neighbours differ, icons cycle through SpaceIcons(), names count up ("Space 3").
// The space a window shows: the one with `active_id`, else the first; null when there are none.
const Space* FindActiveSpace(const std::vector<Space>& spaces, std::string_view active_id);

int NextSpaceHue(const std::vector<Space>& spaces);
std::string NextSpaceIcon(const std::vector<Space>& spaces);
std::string NextSpaceName(const std::vector<Space>& spaces);

// Hues are degrees on the colour wheel: NormalizeHue maps any int into [0, 360), and HueDistance
// is the shorter way round between two hues (0 to 180).
int NormalizeHue(int hue);
int HueDistance(int a, int b);

// The index into SpaceColours() of the colour closest to `hue` (the new-space dialog's preset).
size_t NearestSpaceColour(int hue);

// The Chromium tab group colour closest to a space colour: a new tab group starts in its space's
// colour (docs/ux.md §6.1). Grey is never chosen.
tab_groups::TabGroupColorId TabGroupColorForHue(int hue);

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

// A page that leaves its space empty for import (docs/ux.md §13: an empty space with the same
// name and profile is filled): a blank page, or the welcome page first run opens.
bool IsUntouchedTabUrl(const GURL& url);

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
  // The user's name for the tab, and a pinned tab's link (lb/browser/tabs/tab_names.h).
  std::u16string name;
  GURL pinned_url;
};

base::DictValue SavedTabToDict(const SavedTab& tab);

// The valid records of a saved tab list, in order; malformed records and invalid URLs are skipped.
std::vector<SavedTab> SavedTabsFromList(const base::ListValue& list);

// A space's saved tabs with imported ones added (docs/ux.md §13): imported pinned tabs after its
// pinned tabs, the rest after its today tabs. Which tab is active doesn't change, and none of the
// added tabs is active.
std::vector<SavedTab> MergeSavedTabs(std::vector<SavedTab> existing, std::vector<SavedTab> added);

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
