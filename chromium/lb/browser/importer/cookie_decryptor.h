// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_IMPORTER_COOKIE_DECRYPTOR_H_
#define LB_BROWSER_IMPORTER_COOKIE_DECRYPTOR_H_

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "base/containers/span.h"

namespace lb {

// Decrypts a Chrome-family browser's cookie values on macOS, as Chromium's os_crypt does: the
// key is PBKDF2-HMAC-SHA1 of the browser's "<name> Safe Storage" keychain password (salt
// "saltysalt", 1003 rounds, 16 bytes), and a value is "v10" followed by AES-128-CBC with an IV of
// 16 spaces. lb-core then strips the domain hash that newer files put first.
class CookieDecryptor {
 public:
  explicit CookieDecryptor(std::string_view keychain_password);
  ~CookieDecryptor();

  // The plaintext of an `encrypted_value`; null when it isn't one this key opens (another
  // prefix, a wrong key, damaged data).
  std::optional<std::vector<uint8_t>> Decrypt(base::span<const uint8_t> encrypted) const;

  // The reverse, for tests.
  std::vector<uint8_t> EncryptForTesting(base::span<const uint8_t> plaintext) const;

 private:
  std::vector<uint8_t> key_;
};

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_COOKIE_DECRYPTOR_H_
