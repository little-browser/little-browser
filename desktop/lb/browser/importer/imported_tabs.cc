// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/imported_tabs.h"

#include <map>
#include <string>
#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "url/gurl.h"

namespace lb {

std::vector<SavedTab> ArcSpaceTabs(const core::ArcSpace& space, int group_color) {
  std::vector<SavedTab> tabs;
  std::map<int, std::string> group_ids;  // Arc folder number → group id
  for (const core::ArcPinnedTab& t : space.pinned) {
    SavedTab tab;
    tab.url = GURL(std::string(t.url));
    tab.title = base::UTF8ToUTF16(std::string(t.title));
    tab.pinned = true;
    if (!tab.url.is_valid()) {
      continue;
    }
    if (t.folder >= 0) {
      auto [it, inserted] = group_ids.try_emplace(t.folder);
      if (inserted) {
        it->second = base::Uuid::GenerateRandomV4().AsLowercaseString();
      }
      tab.group_id = it->second;
      tab.group_title = base::UTF8ToUTF16(std::string(t.folder_name));
      tab.group_color = group_color;
      tab.group_collapsed = true;  // imported groups come collapsed
    }
    tabs.push_back(std::move(tab));
  }
  for (const core::Tab& t : space.unpinned) {
    SavedTab tab;
    tab.url = GURL(std::string(t.url));
    tab.title = base::UTF8ToUTF16(std::string(t.title));
    if (tab.url.is_valid()) {
      tabs.push_back(std::move(tab));
    }
  }
  return tabs;
}

std::vector<SavedTab> OpenTabsAsSavedTabs(const rust::Vec<core::OpenTab>& open_tabs) {
  std::vector<SavedTab> tabs;
  for (const core::OpenTab& t : open_tabs) {
    SavedTab tab;
    tab.url = GURL(std::string(t.url));
    if (!tab.url.is_valid()) {
      continue;
    }
    tab.title = base::UTF8ToUTF16(std::string(t.title));
    tab.pinned = t.pinned;
    tab.group_id = std::string(t.group);
    tab.group_title = base::UTF8ToUTF16(std::string(t.group_title));
    tab.group_color = t.group_color;
    tab.group_collapsed = !tab.group_id.empty();  // imported groups come collapsed
    tabs.push_back(std::move(tab));
  }
  return tabs;
}

}  // namespace lb
