// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/cookie_decryptor.h"

#include <array>

#include "base/containers/span.h"
#include "base/strings/string_view_util.h"
#include "crypto/aes_cbc.h"
#include "third_party/boringssl/src/include/openssl/evp.h"

namespace lb {

namespace {

constexpr std::string_view kSalt = "saltysalt";
constexpr uint32_t kIterations = 1003;
constexpr size_t kKeySize = 16;
constexpr std::string_view kPrefix = "v10";

std::array<uint8_t, crypto::aes_cbc::kBlockSize> Iv() {
  std::array<uint8_t, crypto::aes_cbc::kBlockSize> iv;
  iv.fill(' ');
  return iv;
}

}  // namespace

CookieDecryptor::CookieDecryptor(std::string_view keychain_password) : key_(kKeySize) {
  // BoringSSL directly: crypto::kdf's SHA-1 PBKDF2 is limited to a list of callers.
  PKCS5_PBKDF2_HMAC_SHA1(keychain_password.data(), keychain_password.size(),
                         reinterpret_cast<const uint8_t*>(kSalt.data()), kSalt.size(), kIterations,
                         key_.size(), key_.data());
}

CookieDecryptor::~CookieDecryptor() = default;

std::optional<std::vector<uint8_t>> CookieDecryptor::Decrypt(
    base::span<const uint8_t> encrypted) const {
  if (encrypted.size() <= kPrefix.size() ||
      base::as_string_view(encrypted.first(kPrefix.size())) != kPrefix) {
    return std::nullopt;
  }
  return crypto::aes_cbc::Decrypt(key_, Iv(), encrypted.subspan(kPrefix.size()));
}

std::vector<uint8_t> CookieDecryptor::EncryptForTesting(base::span<const uint8_t> plaintext) const {
  std::vector<uint8_t> out(kPrefix.begin(), kPrefix.end());
  std::vector<uint8_t> body = crypto::aes_cbc::Encrypt(key_, Iv(), plaintext);
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

}  // namespace lb
