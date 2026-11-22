// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/arc_tabs.h"

#include <map>
#include <string>

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

}  // namespace lb
