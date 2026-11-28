// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/frame/contents_card.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

TEST(ContentsCardTest, UnderAToolbarTheCardIsInsetRightAndBelow) {
  EXPECT_EQ(ContentsCardBounds(gfx::Rect(240, 40, 800, 600)), gfx::Rect(240, 40, 792, 592));
  EXPECT_EQ(ContentsCardRadii(), gfx::RoundedCornersF(0, 0, 10, 10));
}

TEST(ContentsCardTest, WithTheToolbarInTheSidebarTheCardIsInsetAboveTooAndFullyRounded) {
  EXPECT_EQ(ContentsCardBounds(gfx::Rect(240, 0, 800, 600), /*toolbar_above=*/false),
            gfx::Rect(240, 8, 792, 584));
  EXPECT_EQ(ContentsCardRadii(/*toolbar_above=*/false), gfx::RoundedCornersF(10, 10, 10, 10));
}

}  // namespace
}  // namespace lb
