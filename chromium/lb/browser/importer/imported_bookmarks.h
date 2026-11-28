// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_IMPORTER_IMPORTED_BOOKMARKS_H_
#define LB_BROWSER_IMPORTER_IMPORTED_BOOKMARKS_H_

#include <vector>

#include "components/user_data_importer/common/imported_bookmark_entry.h"
#include "lb/core_bridge/lib.rs.h"

namespace lb {

// The bookmarks Chromium's own importers read (Firefox, Safari, a bookmarks file), each with the
// path of folders it sits in, as lb-core's flat list, so they merge into the existing bookmarks
// like ours do (docs/ux.md §13): toolbar bookmarks go to the bookmarks bar, the rest to other
// bookmarks. A toolbar entry's path starts with the toolbar's own folder ("Bookmarks bar",
// "Bookmarks Toolbar"), which is left out, as Chromium's ProfileWriter does; so is that folder's
// own entry. Likewise Firefox's other-bookmarks root ("unfiled") and mobile root lead to those
// places, and Safari's Reading List ("com.apple.ReadingList") becomes a "Reading List" folder.
// A folder is listed before each entry in it; the merge joins folders of the same name. Entries
// without an address that aren't folders are left out.
rust::Vec<core::FlatBookmark> ImportedBookmarksToFlat(
    const std::vector<user_data_importer::ImportedBookmarkEntry>& entries);

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_IMPORTED_BOOKMARKS_H_
