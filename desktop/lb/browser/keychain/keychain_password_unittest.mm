// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Chromium's KeychainPassword as patched by patches/lb/keychain-name.patch: Little Browser's own
// Safe Storage item, and the password an existing install keeps from Chromium's item.

#include "components/os_crypt/common/keychain_password_mac.h"

#include <Security/Security.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/containers/to_vector.h"
#include "base/types/expected.h"
#include "crypto/apple/keychain_v2.h"
#include "lb/browser/keychain/keychain_key.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

using Item = std::pair<std::string, std::string>;  // service, account

// Generic passwords by service and account. An item in `denied` behaves as when the user
// chooses Deny on macOS's prompt.
class ItemKeychain : public crypto::apple::KeychainV2 {
 public:
  ItemKeychain() = default;
  ~ItemKeychain() override = default;

  base::expected<std::vector<uint8_t>, OSStatus> FindGenericPassword(
      std::string_view service_name,
      std::string_view account_name) override {
    Item item{std::string(service_name), std::string(account_name)};
    reads.push_back(item);
    if (denied.contains(item)) {
      return base::unexpected(errSecAuthFailed);
    }
    auto it = items.find(item);
    if (it == items.end()) {
      return base::unexpected(errSecItemNotFound);
    }
    return base::ToVector(base::as_byte_span(it->second));
  }

  OSStatus AddGenericPassword(std::string_view service_name,
                              std::string_view account_name,
                              base::span<const uint8_t> password) override {
    Item item{std::string(service_name), std::string(account_name)};
    if (items.contains(item)) {
      return errSecDuplicateItem;
    }
    items[item] = std::string(password.begin(), password.end());
    return noErr;
  }

  std::map<Item, std::string> items;
  std::set<Item> denied;
  std::vector<Item> reads;
};

const Item kOwn{kKeychainServiceName, kKeychainAccountName};
const Item kLegacy{kLegacyKeychainServiceName, kLegacyKeychainAccountName};

class KeychainPasswordTest : public testing::Test {
 protected:
  ~KeychainPasswordTest() override { SetProfileExistedAtStartup(false); }

  std::string GetPassword() { return ::KeychainPassword(*keychain_).GetPassword(); }

  ItemKeychain& keychain() { return *keychain_; }

 private:
  // KeychainV2's destructor is protected (it is a singleton); a test's own copy is never deleted
  // through the base class.
  std::unique_ptr<ItemKeychain> keychain_ = std::make_unique<ItemKeychain>();
};

TEST_F(KeychainPasswordTest, UsesLittleBrowsersOwnItemNames) {
  EXPECT_EQ(::KeychainPassword::GetServiceName(), kKeychainServiceName);
  EXPECT_EQ(::KeychainPassword::GetAccountName(), kKeychainAccountName);
}

TEST_F(KeychainPasswordTest, ReturnsTheOwnItemAndNeverReadsTheLegacyOne) {
  SetProfileExistedAtStartup(true);
  keychain().items[kOwn] = "own-password";
  keychain().items[kLegacy] = "chromium-password";

  EXPECT_EQ(GetPassword(), "own-password");
  EXPECT_EQ(keychain().reads, std::vector<Item>{kOwn});
}

TEST_F(KeychainPasswordTest, NewInstallMakesItsOwnPasswordWithoutReadingChromiums) {
  SetProfileExistedAtStartup(false);
  keychain().items[kLegacy] = "chromium-password";

  std::string password = GetPassword();
  EXPECT_FALSE(password.empty());
  EXPECT_NE(password, "chromium-password");
  EXPECT_EQ(keychain().items[kOwn], password);
  EXPECT_EQ(keychain().reads, std::vector<Item>{kOwn});
}

TEST_F(KeychainPasswordTest, ExistingInstallCopiesTheLegacyPassword) {
  SetProfileExistedAtStartup(true);
  keychain().items[kLegacy] = "legacy-password";

  EXPECT_EQ(GetPassword(), "legacy-password");
  EXPECT_EQ(keychain().items[kOwn], "legacy-password");
  // The legacy item stays: another browser may own it.
  EXPECT_EQ(keychain().items[kLegacy], "legacy-password");

  // The next run finds its own item and leaves the legacy one alone.
  keychain().reads.clear();
  EXPECT_EQ(GetPassword(), "legacy-password");
  EXPECT_EQ(keychain().reads, std::vector<Item>{kOwn});
}

TEST_F(KeychainPasswordTest, ExistingInstallWithoutALegacyItemMakesANewPassword) {
  SetProfileExistedAtStartup(true);

  std::string password = GetPassword();
  EXPECT_FALSE(password.empty());
  EXPECT_EQ(keychain().items[kOwn], password);
  EXPECT_EQ(keychain().reads, (std::vector<Item>{kOwn, kLegacy}));
}

TEST_F(KeychainPasswordTest, DeniedLegacyItemMakesANewPassword) {
  SetProfileExistedAtStartup(true);
  keychain().items[kLegacy] = "legacy-password";
  keychain().denied.insert(kLegacy);

  std::string password = GetPassword();
  EXPECT_FALSE(password.empty());
  EXPECT_NE(password, "legacy-password");
  EXPECT_EQ(keychain().items[kOwn], password);
}

}  // namespace
}  // namespace lb
