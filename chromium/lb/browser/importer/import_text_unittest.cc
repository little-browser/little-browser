// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/import_text.h"

#include <string>

#include "lb/core_bridge/lib.rs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

TEST(ImportTextTest, ValidTextPassesUnchanged) {
  EXPECT_TRUE(IsCoreText("caf\xC3\xA9"));
  EXPECT_EQ(ToCoreText("caf\xC3\xA9"), "caf\xC3\xA9");
  EXPECT_TRUE(IsCoreText(""));
  EXPECT_TRUE(IsCoreText("\xEF\xB7\x90"));  // U+FDD0, a noncharacter Rust accepts
}

TEST(ImportTextTest, InvalidBytesBecomeReplacementCharacters) {
  const std::string latin1 =
      "pr\xE9"
      "f";  // a Latin-1 é: not UTF-8
  EXPECT_FALSE(IsCoreText(latin1));
  EXPECT_EQ(ToCoreText(latin1),
            "pr\xEF\xBF\xBD"
            "f");
  EXPECT_EQ(ToCoreText("\xFF\xFE"), "\xEF\xBF\xBD\xEF\xBF\xBD");
}

TEST(ImportTextTest, ConvertedTextCrossesTheBridge) {
  // Without the conversion, cxx would abort here.
  core::BookmarksResult read = core::chromium_bookmarks(ToCoreText("{\"roots\": \xFF}"));
  EXPECT_FALSE(std::string(read.error).empty());
}

}  // namespace
}  // namespace lb
