// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_COMMAND_BAR_COMMAND_MATCHING_H_
#define LB_BROWSER_COMMAND_BAR_COMMAND_MATCHING_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "url/gurl.h"

namespace lb {

// Command bar text rules that don't need views (unit tested in command_matching_unittest.cc). The
// ranking is lb-core's (lb_core::command_bar); reading typed text as an address needs Chromium's
// URL parser and stays here.

// What the command bar can open: a new tab; a page in the current tab (a New Tab Group's blank
// first tab); the current tab's address, to edit; the space's name, to rename; a page in a new
// little window (Cmd+Option+N); a tab's name, to rename; or a pinned tab's link, to edit.
enum class CommandBarMode {
  kNewTab,
  kOpenHere,
  kEditAddress,
  kRenameSpace,
  kLittleWindow,
  kRenameTab,
  kEditPinnedUrl,
};

// The kinds of result the command bar lists.
enum class CommandBarResult { kOpenTab, kCommand, kBookmark, kHistory, kAddress, kSearch };

// Whether the bar in `mode` lists results of `kind`. In the little-window mode everything chosen
// opens a page in a new little window, so it lists no open tabs (they'd switch the window behind)
// and no commands (they'd act on it).
bool OffersResult(CommandBarMode mode, CommandBarResult kind);

// What is added to a result's match score for its kind, so that among equally good matches open
// tabs come first, then commands, then bookmarks, then history. The boost is small, so a better
// match of any kind still comes first (docs/ux.md §4 ranks by fuzzy match).
double ResultBoost(CommandBarResult kind);

// How many bookmarks the bar lists at most, the best matches.
size_t MaxBookmarkResults();

// Which bookmark matches the bar lists, given their scores: the indexes of the best
// MaxBookmarkResults(), best first (equal scores keep their order). Only these count as listed,
// so a page left out here can still come from history.
std::vector<size_t> PickBookmarkResults(const std::vector<double>& scores);

// A bookmark result's subtitle: "Bookmark · <its folder> · <host>", without the folder when it
// has none.
std::u16string BookmarkSubtitle(std::u16string_view folder, const GURL& url);

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
