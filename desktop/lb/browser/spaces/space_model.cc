// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/spaces/space_model.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <utility>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace lb {

namespace {

// Golden-angle-ish step: consecutive spaces get clearly different colours.
constexpr int kHueStep = 137;

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

}  // namespace

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
  space.icon = icon ? *icon : kSpaceIcons[0];
  space.hue = dict.FindInt("hue").value_or(kFirstSpaceHue);
  space.profile = profile && !profile->empty() ? *profile : std::string(fallback_profile);
  return space;
}

int NextSpaceHue(const std::vector<Space>& spaces) {
  return spaces.empty() ? kFirstSpaceHue : (spaces.back().hue + kHueStep) % 360;
}

const char* NextSpaceIcon(const std::vector<Space>& spaces) {
  return kSpaceIcons[spaces.size() % std::size(kSpaceIcons)];
}

std::string NextSpaceName(const std::vector<Space>& spaces) {
  return "Space " + base::NumberToString(spaces.size() + 1);
}

int NormalizeHue(int hue) {
  return ((hue % 360) + 360) % 360;
}

int HueDistance(int a, int b) {
  const int d = std::abs(NormalizeHue(a) - NormalizeHue(b));
  return std::min(d, 360 - d);
}

size_t NearestSpaceColour(int hue) {
  const auto it = std::ranges::min_element(
      kSpaceColours, {}, [hue](const SpaceColour& c) { return HueDistance(c.hue, hue); });
  return static_cast<size_t>(it - std::begin(kSpaceColours));
}

tab_groups::TabGroupColorId TabGroupColorForHue(int hue) {
  return std::ranges::min(kGroupColourHues, {},
                          [hue](const GroupColourHue& g) { return HueDistance(g.hue, hue); })
      .colour;
}

std::optional<std::string> ChooseSpaceForWindow(
    const std::vector<Space>& spaces,
    std::string_view profile,
    std::string_view last_active_id,
    base::FunctionRef<bool(const std::string& id)> is_shown,
    bool restoring_session) {
  for (const Space& s : spaces) {
    if (s.id == last_active_id && s.profile == profile && !is_shown(s.id)) {
      return s.id;
    }
  }
  for (const Space& s : spaces) {
    if (s.profile == profile && !is_shown(s.id)) {
      return s.id;
    }
  }
  if (restoring_session) {
    for (const Space& s : spaces) {
      if (s.profile == profile) {
        return s.id;
      }
    }
  }
  return std::nullopt;
}

bool IsBlankTabUrl(const GURL& url) {
  const std::string_view spec = url.possibly_invalid_spec();
  return url.is_empty() || url == GURL(url::kAboutBlankURL) ||
         spec.starts_with("chrome://newtab") || spec.starts_with("chrome://new-tab-page");
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
    tabs.push_back(std::move(tab));
  }
  return tabs;
}

namespace {

// Runs of neighbouring tabs with the same non-empty id.
std::vector<std::vector<size_t>> Runs(const std::vector<SavedTab>& tabs,
                                      std::string SavedTab::* id) {
  std::vector<std::vector<size_t>> runs;
  for (size_t i = 0; i < tabs.size(); ++i) {
    if ((tabs[i].*id).empty()) {
      continue;
    }
    if (i > 0 && tabs[i - 1].*id == tabs[i].*id) {
      runs.back().push_back(i);
    } else {
      runs.push_back({i});
    }
  }
  return runs;
}

}  // namespace

std::vector<std::vector<size_t>> SavedTabGroups(const std::vector<SavedTab>& tabs) {
  return Runs(tabs, &SavedTab::group_id);
}

std::vector<std::vector<size_t>> SavedTabSplits(const std::vector<SavedTab>& tabs) {
  std::vector<std::vector<size_t>> splits = Runs(tabs, &SavedTab::split_id);
  std::erase_if(splits, [](const std::vector<size_t>& run) { return run.size() != 2; });
  return splits;
}

size_t ActiveSavedTab(const std::vector<SavedTab>& tabs) {
  const auto it = std::ranges::find_if(tabs, &SavedTab::active);
  return it == tabs.end() ? 0 : static_cast<size_t>(it - tabs.begin());
}

}  // namespace lb
