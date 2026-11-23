// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/keychain/keychain_key.h"

#include <string_view>

#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

TEST(KeychainKeyTest, OwnItemNamesAreLittleBrowsers) {
  EXPECT_EQ(std::string_view(kKeychainServiceName), "Little Browser Safe Storage");
  EXPECT_EQ(std::string_view(kKeychainAccountName), "Little Browser");
  EXPECT_NE(std::string_view(kKeychainServiceName), std::string_view(kLegacyKeychainServiceName));
}

TEST(KeychainKeyTest, OwnItemIsUsedWheneverItExists) {
  for (bool profile_existed : {false, true}) {
    for (bool legacy_item_read : {false, true}) {
      EXPECT_FALSE(ShouldReadLegacyKeychainItem(true, profile_existed));
      EXPECT_EQ(ChooseKeychainKeySource(true, profile_existed, legacy_item_read),
                KeychainKeySource::kOwnItem);
    }
  }
}

TEST(KeychainKeyTest, NewInstallNeverReadsTheLegacyItem) {
  EXPECT_FALSE(ShouldReadLegacyKeychainItem(false, false));
  EXPECT_EQ(ChooseKeychainKeySource(false, false, false), KeychainKeySource::kNewPassword);
  // Even a legacy password handed in by mistake isn't used without an existing profile.
  EXPECT_EQ(ChooseKeychainKeySource(false, false, true), KeychainKeySource::kNewPassword);
}

TEST(KeychainKeyTest, ExistingInstallKeepsTheLegacyPassword) {
  EXPECT_TRUE(ShouldReadLegacyKeychainItem(false, true));
  EXPECT_EQ(ChooseKeychainKeySource(false, true, true), KeychainKeySource::kLegacyItem);
}

TEST(KeychainKeyTest, ExistingInstallWithoutALegacyPasswordStartsAfresh) {
  EXPECT_EQ(ChooseKeychainKeySource(false, true, false), KeychainKeySource::kNewPassword);
}

TEST(KeychainKeyTest, ProfileExistedAtStartupIsRecorded) {
  SetProfileExistedAtStartup(true);
  EXPECT_TRUE(ProfileExistedAtStartup());
  SetProfileExistedAtStartup(false);
  EXPECT_FALSE(ProfileExistedAtStartup());
}

}  // namespace
}  // namespace lb
