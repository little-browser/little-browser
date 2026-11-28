// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_RUST_STRINGS_H_
#define LB_BROWSER_RUST_STRINGS_H_

#include <string>
#include <vector>

#include "third_party/rust/cxx/v1/cxx.h"

namespace lb {

// `strings` as lb-core's rules take a list of text, through the cxx bridge. Anything that isn't
// UTF-8 becomes U+FFFD on the way instead of failing the conversion: cxx aborts the browser on a
// strict rust::String from invalid UTF-8.
rust::Vec<rust::String> ToRustStrings(const std::vector<std::string>& strings);

}  // namespace lb

#endif  // LB_BROWSER_RUST_STRINGS_H_
