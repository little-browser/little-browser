// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/import_text.h"

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"

namespace lb {

bool IsCoreText(std::string_view bytes) {
  // Rust strings may hold noncharacters, so only the encoding is checked.
  return base::IsStringUTF8AllowingNoncharacters(bytes);
}

std::string ToCoreText(std::string_view bytes) {
  if (IsCoreText(bytes)) {
    return std::string(bytes);
  }
  // The conversion replaces each invalid sequence with U+FFFD.
  return base::UTF16ToUTF8(base::UTF8ToUTF16(bytes));
}

}  // namespace lb
