// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_IMPORTER_IMPORT_TEXT_H_
#define LB_BROWSER_IMPORTER_IMPORT_TEXT_H_

#include <string>
#include <string_view>

namespace lb {

// lb-core takes only valid UTF-8 (cxx aborts the browser on anything else), while other
// browsers' files and databases can hold any bytes (cookie names may, damaged files do).

// `bytes` as valid UTF-8: unchanged when it already is, else with each invalid sequence replaced
// by U+FFFD. For text whose exact bytes don't matter (titles, file contents to parse).
std::string ToCoreText(std::string_view bytes);

// Whether `bytes` can go to lb-core unchanged. For values that must stay byte for byte (cookie
// names and values): those that can't are left behind.
bool IsCoreText(std::string_view bytes);

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_IMPORT_TEXT_H_
