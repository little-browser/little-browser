// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/cookie_decryptor.h"

#include <string>
#include <vector>

#include "base/containers/span.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

std::vector<uint8_t> Bytes(std::string_view s) {
  return std::vector<uint8_t>(s.begin(), s.end());
}

// "hello" encrypted the way Chrome does on macOS with the keychain password "peanuts": "v10",
// then AES-128-CBC with the PBKDF2-HMAC-SHA1 key (salt "saltysalt", 1003 rounds) and an IV of 16
// spaces. Computed outside this code, with Python's hashlib and the openssl command.
constexpr uint8_t kChromeHello[] = {'v',  '1',  '0',  0x86, 0x90, 0xa7, 0xcb, 0x3c, 0x78, 0x71,
                                    0xd1, 0xa1, 0x60, 0x24, 0x46, 0x9c, 0xe3, 0x5b, 0x43};

TEST(CookieDecryptorTest, OpensChromesValues) {
  CookieDecryptor decryptor("peanuts");
  std::optional<std::vector<uint8_t>> plain = decryptor.Decrypt(kChromeHello);
  ASSERT_TRUE(plain);
  EXPECT_EQ(*plain, Bytes("hello"));
}

TEST(CookieDecryptorTest, RoundTrips) {
  CookieDecryptor decryptor("a keychain password");
  const std::vector<uint8_t> value = Bytes(std::string(40, 'x') + "session=abc");
  EXPECT_EQ(decryptor.Decrypt(decryptor.EncryptForTesting(value)), value);
}

TEST(CookieDecryptorTest, RefusesWhatItCantOpen) {
  CookieDecryptor right("right");
  CookieDecryptor wrong("wrong");
  const std::vector<uint8_t> encrypted = right.EncryptForTesting(Bytes("secret value 123"));
  EXPECT_FALSE(wrong.Decrypt(encrypted).has_value() &&
               *wrong.Decrypt(encrypted) == Bytes("secret value 123"));
  EXPECT_FALSE(right.Decrypt(Bytes("v11abcdefghijklmnop")));  // another scheme
  EXPECT_FALSE(right.Decrypt(Bytes("v10")));
  EXPECT_FALSE(right.Decrypt(Bytes("")));
  std::vector<uint8_t> damaged = encrypted;
  damaged.pop_back();
  EXPECT_FALSE(right.Decrypt(damaged));  // not a whole block
}

}  // namespace
}  // namespace lb
