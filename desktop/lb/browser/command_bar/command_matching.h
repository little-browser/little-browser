// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_COMMAND_BAR_COMMAND_MATCHING_H_
#define LB_BROWSER_COMMAND_BAR_COMMAND_MATCHING_H_

#include <optional>
#include <string>
#include <string_view>

#include "url/gurl.h"

namespace lb {

// Command bar text rules that don't need views (unit tested in command_matching_unittest.cc).

// How well `query` matches `text`, case-insensitively; 0 means no match. A substring scores
// above 2 (more at the start of the text or of a word); letters in order with gaps score
// below 1, less the bigger the gaps.
double MatchScore(std::u16string_view query, std::u16string_view text);

// The host without "www.", for result subtitles; the whole spec when there is no host.
std::u16string DisplayHost(const GURL& url);

// Typed text read as an address.
struct TypedAddress {
  GURL url;
  // True when the text is clearly an address (a scheme, a known domain, an IP address or
  // localhost): the command bar offers it first. False for something like "node.js" or
  // "printer.local", which may be a search: the search comes first, the address second.
  bool certain = false;
};

// Reads typed text as an address, or null when it is a search: plain words, numbers like "3.14",
// an email address. Text starting with a scheme ("https://…", "about:", "chrome:") is taken as
// is. A bare host gets https://, except localhost and IP addresses, which get http:// (they rarely
// have certificates).
std::optional<TypedAddress> InputAsAddress(std::u16string_view input);

}  // namespace lb

#endif  // LB_BROWSER_COMMAND_BAR_COMMAND_MATCHING_H_
