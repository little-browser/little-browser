// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/bookmarks/bookmark_merge.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "url/gurl.h"

namespace lb {

namespace {

using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;

// The three places of the bookmark model, in lb::core::FlatBookmark's order.
std::vector<const BookmarkNode*> Places(BookmarkModel* model) {
  return {model->bookmark_bar_node(), model->other_node(), model->mobile_node()};
}

// The existing bookmarks as lb-core's flat list.
rust::Vec<core::FlatBookmark> FlattenModel(BookmarkModel* model) {
  rust::Vec<core::FlatBookmark> out;
  auto walk = [&out](auto& self, const BookmarkNode* node, uint8_t place, size_t depth) -> void {
    for (const auto& child : node->children()) {
      const bool folder = child->is_folder();
      out.push_back(core::FlatBookmark{place, depth, folder, base::UTF16ToUTF8(child->GetTitle()),
                                       folder ? std::string() : child->url().spec()});
      if (folder) {
        self(self, child.get(), place, depth + 1);
      }
    }
  };
  std::vector<const BookmarkNode*> places = Places(model);
  for (size_t place = 0; place < places.size(); ++place) {
    walk(walk, places[place], static_cast<uint8_t>(place), 0);
  }
  return out;
}

// The first folder called `title` directly in `parent`, or a new one at its end.
const BookmarkNode* FolderIn(BookmarkModel* model,
                             const BookmarkNode* parent,
                             const std::u16string& title) {
  for (const auto& child : parent->children()) {
    if (child->is_folder() && child->GetTitle() == title) {
      return child.get();
    }
  }
  return model->AddFolder(parent, parent->children().size(), title);
}

// Adds lb-core's merge result (folders to find or make, links to add at the end of their
// folder) to the model. Returns the number of links added.
size_t AddBookmarks(BookmarkModel* model, const rust::Vec<core::FlatBookmark>& added) {
  std::vector<const BookmarkNode*> places = Places(model);
  size_t links = 0;
  std::vector<const BookmarkNode*> open;  // open[d]: the folder items at depth d go into
  uint8_t place = 255;
  for (const core::FlatBookmark& b : added) {
    if (b.place != place || open.empty()) {
      place = b.place;
      open.assign(1, places[std::min<size_t>(place, places.size() - 1)]);
    }
    open.resize(std::min(open.size(), b.depth + 1));
    const BookmarkNode* parent = open.back();
    const std::u16string title = base::UTF8ToUTF16(std::string(b.title));
    if (b.is_folder) {
      open.push_back(FolderIn(model, parent, title));
      continue;
    }
    const GURL url(std::string(b.url));
    if (url.is_valid()) {
      model->AddURL(parent, parent->children().size(), title, url);
      ++links;
    }
  }
  return links;
}

// The imported links as the model keeps them (GURL's canonical form, as FlattenModel lists the
// model's), so a link already there is found however the file wrote it ("HTTPS://Example.com"
// is "https://example.com/"); links that aren't valid URLs are left out.
rust::Vec<core::FlatBookmark> Canonical(const rust::Vec<core::FlatBookmark>& bookmarks) {
  rust::Vec<core::FlatBookmark> out;
  for (const core::FlatBookmark& b : bookmarks) {
    if (b.is_folder) {
      out.push_back(b);
      continue;
    }
    const GURL url(std::string(b.url));
    if (url.is_valid()) {
      out.push_back(core::FlatBookmark{b.place, b.depth, false, b.title, url.spec()});
    }
  }
  return out;
}

}  // namespace

size_t MergeIntoBookmarkModel(BookmarkModel* model,
                              const rust::Vec<core::FlatBookmark>& bookmarks) {
  if (!model || !model->loaded() || bookmarks.empty()) {
    return 0;
  }
  return AddBookmarks(model, core::merge_bookmarks(FlattenModel(model), Canonical(bookmarks)));
}

}  // namespace lb
