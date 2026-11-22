// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/imported_bookmarks.h"

#include <string>

#include "base/strings/utf_string_conversions.h"

namespace lb {

namespace {

constexpr uint8_t kBar = 0;
constexpr uint8_t kOther = 1;
constexpr uint8_t kMobile = 2;

// The source's own root folders, which Chromium's importers put first in a bookmark's path:
// Firefox's "unfiled" root (other bookmarks; "Other Bookmarks" in older profiles and HTML
// exports) and its mobile root. They are places here, not folders.
bool IsOtherRoot(const std::u16string& name) {
  return name == u"unfiled" || name == u"Other Bookmarks" || name == u"Unsorted Bookmarks";
}
bool IsMobileRoot(const std::u16string& name) {
  return name == u"mobile" || name == u"Mobile Bookmarks";
}

// Safari's Reading List comes as a folder named by its internal id.
std::u16string FolderName(const std::u16string& name) {
  return name == u"com.apple.ReadingList" ? std::u16string(u"Reading List") : name;
}

}  // namespace

rust::Vec<core::FlatBookmark> ImportedBookmarksToFlat(
    const std::vector<user_data_importer::ImportedBookmarkEntry>& entries) {
  rust::Vec<core::FlatBookmark> flat;
  for (const auto& entry : entries) {
    if (!entry.is_folder && !entry.url.is_valid()) {
      continue;
    }
    uint8_t place = entry.in_toolbar ? kBar : kOther;
    // The toolbar's own folder is the bookmarks bar itself; other roots are places too.
    size_t skip = 0;
    if (entry.in_toolbar) {
      skip = entry.path.empty() ? 0 : 1;
      if (entry.is_folder && entry.path.empty()) {
        continue;
      }
    } else if (!entry.path.empty() && IsOtherRoot(entry.path[0])) {
      skip = 1;
    } else if (!entry.path.empty() && IsMobileRoot(entry.path[0])) {
      skip = 1;
      place = kMobile;
    } else if (entry.is_folder && entry.path.empty() &&
               (IsOtherRoot(entry.title) || IsMobileRoot(entry.title))) {
      continue;  // a root folder's own entry
    }
    for (size_t i = skip; i < entry.path.size(); ++i) {
      flat.push_back(core::FlatBookmark{
          place, i - skip, true, base::UTF16ToUTF8(FolderName(entry.path[i])), std::string()});
    }
    flat.push_back(core::FlatBookmark{
        place, entry.path.size() - skip, entry.is_folder,
        base::UTF16ToUTF8(entry.is_folder ? FolderName(entry.title) : entry.title),
        entry.is_folder ? std::string() : entry.url.spec()});
  }
  return flat;
}

}  // namespace lb
