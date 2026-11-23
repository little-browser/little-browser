// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/keychain/keychain_key.h"

#include <atomic>

namespace lb {

namespace {

// Written on the main thread at startup, read later by the keychain lookup on a worker thread.
std::atomic<bool> g_profile_existed_at_startup{false};

}  // namespace

bool ShouldReadLegacyKeychainItem(bool own_item_found, bool profile_existed) {
  return !own_item_found && profile_existed;
}

KeychainKeySource ChooseKeychainKeySource(bool own_item_found,
                                          bool profile_existed,
                                          bool legacy_item_read) {
  if (own_item_found) {
    return KeychainKeySource::kOwnItem;
  }
  if (ShouldReadLegacyKeychainItem(own_item_found, profile_existed) && legacy_item_read) {
    return KeychainKeySource::kLegacyItem;
  }
  return KeychainKeySource::kNewPassword;
}

void SetProfileExistedAtStartup(bool existed) {
  g_profile_existed_at_startup.store(existed);
}

bool ProfileExistedAtStartup() {
  return g_profile_existed_at_startup.load();
}

}  // namespace lb
