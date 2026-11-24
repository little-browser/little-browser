// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/spaces/space_model.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <utility>
#include <vector>

#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "lb/browser/tabs/tab_names.h"
#include "lb/core_bridge/rules.rs.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace lb {

namespace {

// The hue of each Chromium tab group colour (grey has none).
struct GroupColourHue {
  tab_groups::TabGroupColorId colour;
  int hue;
};
constexpr GroupColourHue kGroupColourHues[] = {
    {tab_groups::TabGroupColorId::kRed, 0},      {tab_groups::TabGroupColorId::kOrange, 24},
    {tab_groups::TabGroupColorId::kYellow, 45},  {tab_groups::TabGroupColorId::kGreen, 140},
    {tab_groups::TabGroupColorId::kCyan, 185},   {tab_groups::TabGroupColorId::kBlue, 220},
    {tab_groups::TabGroupColorId::kPurple, 270}, {tab_groups::TabGroupColorId::kPink, 330},
};

// The rules' view of a list of saved tabs.
template <typename Field>
rust::Vec<Field> Column(const std::vector<SavedTab>& tabs, Field SavedTab::* field) {
  rust::Vec<Field> column;
  column.reserve(tabs.size());
  for (const SavedTab& tab : tabs) {
    column.push_back(tab.*field);
  }
  return column;
}

rust::Vec<rust::String> Ids(const std::vector<SavedTab>& tabs, std::string SavedTab::* id) {
  rust::Vec<rust::String> ids;
  ids.reserve(tabs.size());
  for (const SavedTab& tab : tabs) {
    ids.push_back(rust::String(tab.*id));
  }
  return ids;
}

std::vector<std::vector<size_t>> ToRuns(const rust::Vec<core::TabRun>& runs) {
  std::vector<std::vector<size_t>> out;
  for (const core::TabRun& run : runs) {
    out.emplace_back(run.indexes.begin(), run.indexes.end());
  }
  return out;
}

}  // namespace

const std::vector<std::string>& SpaceIcons() {
  static const base::NoDestructor<std::vector<std::string>> icons([] {
    std::vector<std::string> list;
    for (const rust::String& icon : core::space_icons()) {
      list.push_back(std::string(icon));
    }
    return list;
  }());
  return *icons;
}

const std::vector<SpaceColour>& SpaceColours() {
  static const base::NoDestructor<std::vector<SpaceColour>> colours([] {
    std::vector<SpaceColour> list;
    for (const core::SpaceColour& colour : core::space_colours()) {
      list.push_back({base::UTF8ToUTF16(std::string(colour.name)), colour.hue});
    }
    return list;
  }());
  return *colours;
}

int FirstSpaceHue() {
  return core::first_space_hue();
}

base::DictValue SpaceToDict(const Space& space) {
  base::DictValue dict;
  dict.Set("id", space.id);
  dict.Set("name", space.name);
  dict.Set("icon", space.icon);
  dict.Set("hue", space.hue);
  dict.Set("profile", space.profile);
  return dict;
}

std::optional<Space> SpaceFromDict(const base::DictValue& dict, std::string_view fallback_profile) {
  const std::string* id = dict.FindString("id");
  if (!id || id->empty()) {
    return std::nullopt;
  }
  const std::string* name = dict.FindString("name");
  const std::string* icon = dict.FindString("icon");
  const std::string* profile = dict.FindString("profile");
  Space space;
  space.id = *id;
  space.name = name ? *name : "Space";
  space.icon = icon ? *icon : SpaceIcons()[0];
  space.hue = dict.FindInt("hue").value_or(FirstSpaceHue());
  space.profile = profile && !profile->empty() ? *profile : std::string(fallback_profile);
  return space;
}

int NextSpaceHue(const std::vector<Space>& spaces) {
  return core::next_space_hue(!spaces.empty(), spaces.empty() ? 0 : spaces.back().hue);
}

std::string NextSpaceIcon(const std::vector<Space>& spaces) {
  return std::string(core::next_space_icon(spaces.size()));
}

const Space* FindActiveSpace(const std::vector<Space>& spaces, std::string_view active_id) {
  if (spaces.empty()) {
    return nullptr;
  }
  const auto it = std::ranges::find(spaces, active_id, &Space::id);
  return it != spaces.end() ? &*it : &spaces.front();
}

std::string NextSpaceName(const std::vector<Space>& spaces) {
  return std::string(core::next_space_name(spaces.size()));
}

int NormalizeHue(int hue) {
  return core::normalize_hue(hue);
}

int HueDistance(int a, int b) {
  return core::hue_distance(a, b);
}

size_t NearestSpaceColour(int hue) {
  return core::nearest_space_colour(hue);
}

tab_groups::TabGroupColorId TabGroupColorForHue(int hue) {
  std::vector<int32_t> hues;
  for (const GroupColourHue& g : kGroupColourHues) {
    hues.push_back(g.hue);
  }
  const int64_t nearest =
      core::nearest_hue(rust::Slice<const int32_t>(hues.data(), hues.size()), hue);
  return kGroupColourHues[nearest < 0 ? 0 : static_cast<size_t>(nearest)].colour;
}

std::optional<std::string> ChooseSpaceForWindow(
    const std::vector<Space>& spaces,
    std::string_view profile,
    std::string_view last_active_id,
    base::FunctionRef<bool(const std::string& id)> is_shown,
    bool restoring_session) {
  rust::Vec<core::WindowSpace> list;
  for (const Space& s : spaces) {
    list.push_back({rust::String(s.id), rust::String(s.profile), is_shown(s.id)});
  }
  const rust::String id = core::choose_space_for_window(
      list, rust::Str(profile.data(), profile.size()),
      rust::Str(last_active_id.data(), last_active_id.size()), restoring_session);
  if (id.empty()) {
    return std::nullopt;
  }
  return std::string(id);
}

bool IsBlankTabUrl(const GURL& url) {
  const std::string_view spec = url.possibly_invalid_spec();
  return url.is_empty() || url == GURL(url::kAboutBlankURL) ||
         spec.starts_with("chrome://newtab") || spec.starts_with("chrome://new-tab-page");
}

bool IsUntouchedTabUrl(const GURL& url) {
  return IsBlankTabUrl(url) || url.possibly_invalid_spec().starts_with("chrome://welcome");
}

base::DictValue SavedTabToDict(const SavedTab& tab) {
  base::DictValue dict;
  dict.Set("url", tab.url.spec());
  dict.Set("title", base::UTF16ToUTF8(tab.title));
  dict.Set("pinned", tab.pinned);
  if (tab.active) {
    dict.Set("active", true);
  }
  if (!tab.group_id.empty()) {
    base::DictValue group = base::DictValue()
                                .Set("id", tab.group_id)
                                .Set("title", base::UTF16ToUTF8(tab.group_title))
                                .Set("color", tab.group_color)
                                .Set("collapsed", tab.group_collapsed);
    if (!tab.group_saved_guid.empty()) {
      group.Set("saved", tab.group_saved_guid);
    }
    dict.Set("group", std::move(group));
  }
  if (!tab.split_id.empty()) {
    dict.Set("split", base::DictValue()
                          .Set("id", tab.split_id)
                          .Set("layout", tab.split_layout)
                          .Set("ratio", tab.split_ratio));
  }
  WriteTabCustom({tab.name, tab.pinned_url}, dict);
  return dict;
}

std::vector<SavedTab> SavedTabsFromList(const base::ListValue& list) {
  std::vector<SavedTab> tabs;
  for (const base::Value& value : list) {
    if (!value.is_dict()) {
      continue;
    }
    const base::DictValue& dict = value.GetDict();
    const std::string* url = dict.FindString("url");
    if (!url || !GURL(*url).is_valid()) {
      continue;
    }
    const std::string* title = dict.FindString("title");
    SavedTab tab{GURL(*url), base::UTF8ToUTF16(title ? *title : *url),
                 dict.FindBool("pinned").value_or(false), dict.FindBool("active").value_or(false)};
    if (const base::DictValue* group = dict.FindDict("group")) {
      if (const std::string* id = group->FindString("id"); id && !id->empty()) {
        const std::string* group_title = group->FindString("title");
        tab.group_id = *id;
        tab.group_title = group_title ? base::UTF8ToUTF16(*group_title) : std::u16string();
        tab.group_color = group->FindInt("color").value_or(0);
        tab.group_collapsed = group->FindBool("collapsed").value_or(false);
        if (const std::string* saved = group->FindString("saved")) {
          tab.group_saved_guid = *saved;
        }
      }
    }
    if (const base::DictValue* split = dict.FindDict("split")) {
      if (const std::string* id = split->FindString("id"); id && !id->empty()) {
        tab.split_id = *id;
        tab.split_layout = split->FindInt("layout").value_or(0);
        const double ratio = split->FindDouble("ratio").value_or(0.5);
        tab.split_ratio = ratio > 0 && ratio < 1 ? ratio : 0.5;
      }
    }
    TabCustom custom = ReadTabCustom(dict);
    tab.name = std::move(custom.name);
    tab.pinned_url = std::move(custom.pinned_url);
    tabs.push_back(std::move(tab));
  }
  return tabs;
}

std::vector<std::vector<size_t>> SavedTabGroups(const std::vector<SavedTab>& tabs) {
  return ToRuns(core::tab_runs(Ids(tabs, &SavedTab::group_id)));
}

std::vector<std::vector<size_t>> SavedTabSplits(const std::vector<SavedTab>& tabs) {
  return ToRuns(core::split_runs(Ids(tabs, &SavedTab::split_id)));
}

std::vector<SavedTab> MergeSavedTabs(std::vector<SavedTab> existing, std::vector<SavedTab> added) {
  const rust::Vec<bool> existing_pinned = Column(existing, &SavedTab::pinned);
  const rust::Vec<bool> added_pinned = Column(added, &SavedTab::pinned);
  std::vector<SavedTab> merged;
  merged.reserve(existing.size() + added.size());
  for (const core::MergeSlot& slot :
       core::merge_order(rust::Slice<const bool>(existing_pinned.data(), existing_pinned.size()),
                         rust::Slice<const bool>(added_pinned.data(), added_pinned.size()))) {
    SavedTab& tab = slot.added ? added[slot.index] : existing[slot.index];
    if (slot.added) {
      tab.active = false;  // the space's active tab stays the active one
    }
    merged.push_back(std::move(tab));
  }
  return merged;
}

size_t ActiveSavedTab(const std::vector<SavedTab>& tabs) {
  const rust::Vec<bool> active = Column(tabs, &SavedTab::active);
  return core::active_tab(rust::Slice<const bool>(active.data(), active.size()));
}

}  // namespace lb
