// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/version/release_version.h"

#include "base/strings/string_number_conversions.h"
#include "base/version_info/version_info.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

TEST(ReleaseVersionTest, ARevisionFollowsTheChromiumVersion) {
  EXPECT_EQ(FormatReleaseVersion("157.0.8103.78", 1), "157.0.8103.78-1");
  EXPECT_EQ(FormatReleaseVersion("157.0.8103.78", 3), "157.0.8103.78-3");
  EXPECT_EQ(FormatReleaseVersion("157.0.8103.78", 12), "157.0.8103.78-12");
}

TEST(ReleaseVersionTest, NoRevisionIsTheChromiumVersionAlone) {
  EXPECT_EQ(FormatReleaseVersion("157.0.8103.78", 0), "157.0.8103.78");
  EXPECT_EQ(FormatReleaseVersion("157.0.8103.78", -1), "157.0.8103.78");
}

// The build's own version is its Chromium version, with its revision when it has one.
TEST(ReleaseVersionTest, ThisBuildsVersion) {
  const std::string chromium(version_info::GetVersionNumber());
  EXPECT_GE(ReleaseRevision(), 0);
  if (ReleaseRevision() == 0) {
    EXPECT_EQ(ReleaseVersion(), chromium);
  } else {
    EXPECT_EQ(ReleaseVersion(), chromium + "-" + base::NumberToString(ReleaseRevision()));
  }
}

}  // namespace
}  // namespace lb
