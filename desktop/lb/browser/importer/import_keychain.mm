// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>

#include "base/command_line.h"
#include "base/no_destructor.h"
#include "crypto/apple/fake_keychain_v2.h"
#include "crypto/apple/keychain_v2.h"
#include "lb/browser/importer/chromium_import.h"
#include "lb/browser/importer/import_choices.h"

namespace lb {

namespace {

// The user's keychain, or for a demo's made-up folder a fake one for this run.
crypto::apple::KeychainV2& Keychain() {
  if (ReadsMadeUpImportKeychain(*base::CommandLine::ForCurrentProcess())) {
    static base::NoDestructor<crypto::apple::FakeKeychainV2> fake("lb-import");
    return *fake;
  }
  return crypto::apple::KeychainV2::GetInstance();
}

}  // namespace

std::optional<std::string> ReadImportKeychainPassword(const std::string& service,
                                                      const std::string& account) {
  // Only reads: unlike Chromium's own KeychainPassword, never adds an item that is missing.
  auto password = Keychain().FindGenericPassword(service, account);
  if (!password.has_value()) {
    return std::nullopt;
  }
  return std::string(password->begin(), password->end());
}

}  // namespace lb
