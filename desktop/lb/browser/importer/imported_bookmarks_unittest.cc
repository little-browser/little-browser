// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/imported_bookmarks.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace lb {
namespace {

user_data_importer::ImportedBookmarkEntry Entry(bool toolbar,
                                                std::vector<std::u16string> path,
                                                const std::u16string& title,
                                                const char* url) {
  user_data_importer::ImportedBookmarkEntry e;
  e.in_toolbar = toolbar;
  e.is_folder = url == nullptr;
  e.path = std::move(path);
  e.title = title;
  if (url) {
    e.url = GURL(url);
  }
  return e;
}

TEST(ImportedBookmarksTest, KeepsTheTreeAndMergesLikeOurs) {
  std::vector<user_data_importer::ImportedBookmarkEntry> entries = {
      // As Chromium's importers give them: toolbar paths start with the toolbar's folder.
      Entry(true, {}, u"Bookmarks bar", nullptr),
      Entry(true, {u"Bookmarks bar"}, u"Docs", "https://docs.example/"),
      Entry(true, {u"Bookmarks bar", u"Work"}, u"CI", "https://ci.example/"),
      Entry(true, {u"Bookmarks bar", u"Work"}, u"Tracker", "https://tracker.example/"),
      Entry(false, {u"Menu", u"Later"}, u"Read", "https://read.example/"),
      Entry(false, {}, u"Empty folder", nullptr),
      Entry(false, {}, u"No address", "not a url"),
  };
  rust::Vec<core::FlatBookmark> flat = ImportedBookmarksToFlat(entries);
  ASSERT_EQ(flat.size(), 9u);
  EXPECT_EQ(std::string(flat[0].url), "https://docs.example/");
  EXPECT_EQ(flat[0].place, 0);
  EXPECT_TRUE(flat[1].is_folder);
  EXPECT_EQ(std::string(flat[1].title), "Work");
  EXPECT_EQ(flat[2].depth, 1u);
  EXPECT_EQ(flat[8].place, 1);
  EXPECT_TRUE(flat[8].is_folder);
  EXPECT_EQ(std::string(flat[8].title), "Empty folder");

  // Into empty bookmarks, the repeated folder lines become one folder of two links.
  rust::Vec<core::FlatBookmark> added = core::merge_bookmarks({}, flat);
  ASSERT_EQ(added.size(), 8u);
  EXPECT_EQ(std::string(added[1].title), "Work");
  EXPECT_EQ(std::string(added[2].title), "CI");
  EXPECT_EQ(std::string(added[3].title), "Tracker");
  EXPECT_EQ(std::string(added[4].title), "Menu");
  EXPECT_EQ(std::string(added[5].title), "Later");
  EXPECT_EQ(added[6].depth, 2u);
}

TEST(ImportedBookmarksTest, SourceRootsArePlacesNotFolders) {
  std::vector<user_data_importer::ImportedBookmarkEntry> entries = {
      Entry(false, {u"unfiled"}, u"Loose", "https://loose.example/"),
      Entry(false, {u"Other Bookmarks", u"Kept"}, u"K", "https://k.example/"),
      Entry(false, {u"mobile"}, u"Phone", "https://phone.example/"),
      Entry(false, {u"com.apple.ReadingList"}, u"Later", "https://later.example/"),
      Entry(false, {}, u"unfiled", nullptr),
      Entry(false, {u"Menu"}, u"M", "https://m.example/"),
  };
  rust::Vec<core::FlatBookmark> flat = ImportedBookmarksToFlat(entries);
  ASSERT_EQ(flat.size(), 8u);
  EXPECT_EQ(flat[0].place, 1);  // Loose, straight into other bookmarks
  EXPECT_EQ(flat[0].depth, 0u);
  EXPECT_EQ(std::string(flat[0].title), "Loose");
  EXPECT_EQ(std::string(flat[1].title), "Kept");  // the root dropped, its folder kept
  EXPECT_EQ(flat[2].depth, 1u);
  EXPECT_EQ(flat[3].place, 2);  // mobile root → mobile bookmarks
  EXPECT_EQ(std::string(flat[3].title), "Phone");
  EXPECT_EQ(std::string(flat[4].title), "Reading List");
  EXPECT_TRUE(flat[4].is_folder);
  EXPECT_EQ(std::string(flat[5].title), "Later");
  EXPECT_EQ(std::string(flat[6].title), "Menu");  // an ordinary folder stays a folder
}

}  // namespace
}  // namespace lb
