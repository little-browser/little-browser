// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_BOOKMARKS_BOOKMARK_MERGE_H_
#define LB_BROWSER_BOOKMARKS_BOOKMARK_MERGE_H_

#include <cstddef>

#include "lb/core_bridge/lib.rs.h"

namespace bookmarks {
class BookmarkModel;
}

namespace lb {

// Merges imported bookmarks, lb-core's flat list, into a loaded bookmark model (docs/ux.md §13):
// a folder with the same name in the same place is merged into, and a link already in its
// folder isn't added again (lb-core's merge), so importing twice adds only what is new. The
// desktop's importers and Android's bookmarks file import both use it. Returns the number of
// links added.
size_t MergeIntoBookmarkModel(bookmarks::BookmarkModel* model,
                              const rust::Vec<core::FlatBookmark>& bookmarks);

}  // namespace lb

#endif  // LB_BROWSER_BOOKMARKS_BOOKMARK_MERGE_H_
