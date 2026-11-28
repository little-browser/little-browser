// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_KEYCHAIN_KEYCHAIN_KEY_H_
#define LB_BROWSER_KEYCHAIN_KEYCHAIN_KEY_H_

namespace lb {

// Little Browser's Safe Storage item in the macOS keychain: the password Chromium derives its
// cookie and password encryption key from (components/os_crypt). Never localised: another name
// would lose the key.
inline constexpr char kKeychainServiceName[] = "Little Browser Safe Storage";
inline constexpr char kKeychainAccountName[] = "Little Browser";

// Chromium's default item, which Little Browser used before it had its own. Real Chromium and
// other Chromium browsers use it too.
inline constexpr char kLegacyKeychainServiceName[] = "Chromium Safe Storage";
inline constexpr char kLegacyKeychainAccountName[] = "Chromium";

// Where this run's Safe Storage password comes from.
enum class KeychainKeySource {
  kOwnItem,      // Little Browser's own item.
  kLegacyItem,   // The legacy item's password, copied into Little Browser's own item.
  kNewPassword,  // A new random password, stored as Little Browser's own item.
};

// Whether to read the legacy item, which can make macOS ask the user: only when Little Browser's
// own item is missing and a profile already existed, whose data may be encrypted with the legacy
// item's password. A new install never reads it, so it neither asks for another browser's
// password nor shares it.
bool ShouldReadLegacyKeychainItem(bool own_item_found, bool profile_existed);

// `legacy_item_read` says the legacy item was read (see above) and held a password. When it
// couldn't be read (missing, or the user denied access), the data encrypted with it stays
// unreadable and a new password starts afresh, as on a new install.
KeychainKeySource ChooseKeychainKeySource(bool own_item_found,
                                          bool profile_existed,
                                          bool legacy_item_read);

// Whether the data folder already held a profile ("Local State") when the browser started.
// Recorded once in the browser process before anything is written there
// (chrome/app/chrome_main_delegate.cc, patches/lb/keychain-name.patch); false until then.
void SetProfileExistedAtStartup(bool existed);
bool ProfileExistedAtStartup();

}  // namespace lb

#endif  // LB_BROWSER_KEYCHAIN_KEYCHAIN_KEY_H_
