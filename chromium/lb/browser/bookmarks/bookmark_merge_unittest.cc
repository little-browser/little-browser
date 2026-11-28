// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/bookmarks/bookmark_merge.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>

#include "base/test/task_environment.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/test/test_bookmark_client.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/gurl.h"

namespace lb {
namespace {

using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;

// Places, in core::FlatBookmark's order.
constexpr uint8_t kBar = 0;
constexpr uint8_t kOther = 1;

core::FlatBookmark Folder(uint8_t place, size_t depth, const std::string& title) {
  return core::FlatBookmark{place, depth, true, title, std::string()};
}

core::FlatBookmark Link(uint8_t place,
                        size_t depth,
                        const std::string& title,
                        const std::string& url) {
  return core::FlatBookmark{place, depth, false, title, url};
}

rust::Vec<core::FlatBookmark> List(std::initializer_list<core::FlatBookmark> items) {
  rust::Vec<core::FlatBookmark> out;
  for (const core::FlatBookmark& item : items) {
    out.push_back(item);
  }
  return out;
}

class BookmarkMergeTest : public testing::Test {
 protected:
  BookmarkModel* model() { return model_.get(); }
  const BookmarkNode* bar() { return model_->bookmark_bar_node(); }

 private:
  // The model names its folders from the locale's strings: none here, so they are empty.
  ui::ResourceBundle resources_{nullptr};
  ui::ResourceBundle::SharedInstanceSwapperForTesting use_resources_{&resources_};
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<BookmarkModel> model_ = bookmarks::TestBookmarkClient::CreateModel();
};

TEST_F(BookmarkMergeTest, AddsFoldersAndLinksInPlace) {
  const size_t added = MergeIntoBookmarkModel(
      model(), List({Folder(kBar, 0, "Work"), Link(kBar, 1, "CI", "https://ci.example/"),
                     Folder(kBar, 1, "Deep"), Link(kBar, 2, "Doc", "https://doc.example/"),
                     Link(kBar, 1, "Tracker", "https://tracker.example/"),
                     Link(kBar, 0, "Top", "https://top.example/"),
                     Link(kOther, 0, "Later", "https://later.example/")}));
  EXPECT_EQ(added, 5u);
  ASSERT_EQ(bar()->children().size(), 2u);
  const BookmarkNode* work = bar()->children()[0].get();
  EXPECT_TRUE(work->is_folder());
  EXPECT_EQ(work->GetTitle(), u"Work");
  // Back from "Deep" to "Work" for the link after it: the depth says where each item goes.
  ASSERT_EQ(work->children().size(), 3u);
  EXPECT_EQ(work->children()[0]->url(), GURL("https://ci.example/"));
  EXPECT_EQ(work->children()[1]->GetTitle(), u"Deep");
  EXPECT_EQ(work->children()[1]->children()[0]->url(), GURL("https://doc.example/"));
  EXPECT_EQ(work->children()[2]->url(), GURL("https://tracker.example/"));
  EXPECT_EQ(bar()->children()[1]->url(), GURL("https://top.example/"));
  ASSERT_EQ(model()->other_node()->children().size(), 1u);
  EXPECT_EQ(model()->other_node()->children()[0]->url(), GURL("https://later.example/"));
}

TEST_F(BookmarkMergeTest, ImportingTwiceAddsOnlyWhatIsNew) {
  const rust::Vec<core::FlatBookmark> first =
      List({Folder(kBar, 0, "Work"), Link(kBar, 1, "CI", "https://ci.example/")});
  EXPECT_EQ(MergeIntoBookmarkModel(model(), first), 1u);
  EXPECT_EQ(MergeIntoBookmarkModel(model(), first), 0u);
  // The same folder is merged into, not added again.
  EXPECT_EQ(MergeIntoBookmarkModel(model(), List({Folder(kBar, 0, "Work"),
                                                  Link(kBar, 1, "New", "https://new.example/")})),
            1u);
  ASSERT_EQ(bar()->children().size(), 1u);
  EXPECT_EQ(bar()->children()[0]->children().size(), 2u);
}

TEST_F(BookmarkMergeTest, ALinkIsFoundHoweverTheFileWroteIt) {
  // A bookmarks file's HREF as written: the model keeps it as GURL's canonical spec.
  EXPECT_EQ(
      MergeIntoBookmarkModel(model(), List({Link(kBar, 0, "Example", "https://example.com")})), 1u);
  EXPECT_EQ(
      MergeIntoBookmarkModel(model(), List({Link(kBar, 0, "Example", "https://example.com")})), 0u);
  EXPECT_EQ(
      MergeIntoBookmarkModel(model(), List({Link(kBar, 0, "Example", "HTTPS://Example.COM/")})),
      0u);
  // Two ways of writing one link in one file: it is added once.
  EXPECT_EQ(MergeIntoBookmarkModel(model(), List({Link(kBar, 0, "A", "https://a.example"),
                                                  Link(kBar, 0, "A", "https://a.example/")})),
            1u);
  EXPECT_EQ(bar()->children().size(), 2u);
}

TEST_F(BookmarkMergeTest, LinksThatArentURLsAreLeftOut) {
  EXPECT_EQ(MergeIntoBookmarkModel(model(), List({Link(kBar, 0, "Nothing", "not a url"),
                                                  Link(kBar, 0, "Fine", "https://fine.example/")})),
            1u);
  ASSERT_EQ(bar()->children().size(), 1u);
  EXPECT_EQ(bar()->children()[0]->GetTitle(), u"Fine");
}

TEST_F(BookmarkMergeTest, NothingToMerge) {
  EXPECT_EQ(MergeIntoBookmarkModel(model(), rust::Vec<core::FlatBookmark>()), 0u);
  EXPECT_EQ(MergeIntoBookmarkModel(nullptr, List({Link(kBar, 0, "A", "https://a.example/")})), 0u);
}

}  // namespace
}  // namespace lb
