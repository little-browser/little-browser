// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/rust_strings.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

TEST(RustStringsTest, KeepsTheTextAndItsOrder) {
  rust::Vec<rust::String> list = ToRustStrings({"one", "", "três"});
  ASSERT_EQ(list.size(), 3u);
  EXPECT_EQ(std::string(list[0]), "one");
  EXPECT_EQ(std::string(list[1]), "");
  EXPECT_EQ(std::string(list[2]), "três");
}

TEST(RustStringsTest, EmptyListStaysEmpty) {
  EXPECT_EQ(ToRustStrings({}).size(), 0u);
}

// Invalid UTF-8 becomes U+FFFD instead of aborting the browser.
TEST(RustStringsTest, InvalidUtf8BecomesReplacementCharacters) {
  rust::Vec<rust::String> list =
      ToRustStrings({std::string("a\xff"
                                 "b")});
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(std::string(list[0]),
            "a\xEF\xBF\xBD"
            "b");
}

}  // namespace
}  // namespace lb
