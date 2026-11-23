// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_ADBLOCK_FILTER_LISTS_H_
#define LB_BROWSER_ADBLOCK_FILTER_LISTS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/time/time.h"

class GURL;

namespace lb {

// A filter list the built-in ad blocker uses (docs/design.md section 5), downloaded from its
// maintainers in Adblock Plus syntax.
struct FilterList {
  const char* name;
  const char* url;
};

// The lists, in the order their rules are added.
base::span<const FilterList> FilterLists();

// How old the lists may get before they are downloaded again (EasyList asks for 4 days).
inline constexpr base::TimeDelta kFilterListMaxAge = base::Days(4);

// After a download fails, how soon to try again.
inline constexpr base::TimeDelta kFilterListRetryAfter = base::Hours(1);

// How long one download may take before it counts as failed.
inline constexpr base::TimeDelta kFilterListDownloadTimeout = base::Minutes(5);

// The most files one update downloads: the lists and the files they include.
inline constexpr size_t kMaxFilterListFiles = 32;

// The newest downloaded rules on disk.
struct DownloadedFilterRules {
  // There is a downloaded version.
  bool on_disk = false;
  // The subresource filter indexed its URL rules.
  bool indexed = false;
  // Its element-hiding rules loaded (the file may be missing, or damaged).
  bool hiding_loaded = false;
};

// Whether all the newest downloaded rules are in use: indexed by the subresource filter, with
// their element-hiding rules loaded. With none on disk there is nothing to redo.
bool FilterRulesInUse(const DownloadedFilterRules& rules);

// Whether to download the lists again: never downloaded, older than kFilterListMaxAge, downloaded
// "in the future" (the clock was set back), or the newest downloaded rules aren't all in use:
// never indexed (e.g. the browser crashed while the subresource filter indexed them, which it
// doesn't retry), or their element-hiding rules didn't load.
bool FilterListsNeedUpdate(std::optional<base::Time> last_update,
                           base::Time now,
                           const DownloadedFilterRules& rules);

// What to do once an update's downloads have finished.
struct FilterListsUpdate {
  // Build and use a new ruleset from the files that downloaded.
  bool write = false;
  // Record the update time, so the next download is kFilterListMaxAge later.
  bool stamp = false;
  // Try again after kFilterListRetryAfter.
  bool retry = false;
  // Forget when the lists were last downloaded: what was written is partial, so the retries
  // keep downloading until every file comes.
  bool forget_stamp = false;
};

// All files downloaded: use them. Some failed: keep the rules there are, or, with no rules yet in
// full (none on disk, or their element-hiding rules didn't load), use what did download rather
// than block or hide nothing, until every file comes; either way try again soon.
FilterListsUpdate DecideFilterListsUpdate(bool all_downloaded,
                                          bool any_downloaded,
                                          const DownloadedFilterRules& rules);

// The content version of a ruleset built at `now`, for Chromium's ruleset service: unique,
// readable and in time order when compared as strings, e.g. "lb-2026-11-21-150405".
std::string FilterRulesetVersion(base::Time now);

// The newest of the downloaded versions (folder names), ignoring anything that isn't one.
std::optional<std::string> NewestFilterRulesetVersion(const std::vector<std::string>& names);

// Whether a uBlock Origin "!#if" condition holds for this browser: Chromium on the desktop, with a
// network filter and no HTML filtering. Unknown tokens are false, as in uBlock Origin.
bool FilterListConditionHolds(std::string_view condition);

// The files `list` (downloaded from `list_url`) includes with "!#include", outside "!#if" blocks
// that don't apply: only files next to the list or below it, on the same host.
std::vector<GURL> FilterListIncludes(const GURL& list_url, std::string_view list);

// A rule with uBlock Origin's option names rewritten to the Adblock Plus names Chromium's parser
// knows (xhr, frame, 1p, 3p, from=, ...), and options it can't apply dropped (important, all).
struct NormalizedFilterRule {
  std::string text;
  // The rule had "badfilter": it disables the rule `text`, and does nothing else.
  bool badfilter = false;
};
NormalizedFilterRule NormalizeFilterRule(std::string_view rule);

struct FilterRulesetStats {
  // URL rules added to the ruleset.
  int url_rules = 0;
  // Element-hiding rules, which the subresource filter doesn't apply (BuildElementHidingRules
  // takes them).
  int element_hiding_rules = 0;
  // Lines the parser rejects (syntax Chromium's filter doesn't support).
  int unsupported_rules = 0;
  // Lines in "!#if" blocks for other browsers or platforms.
  int other_platform_rules = 0;
  // Rules a "badfilter" rule disables, and the "badfilter" rules themselves.
  int disabled_rules = 0;
};

// Parses filter lists into Chromium's unindexed ruleset format (a serialized protobuf stream the
// subresource filter's ruleset service indexes). Only URL rules are kept, outside "!#if" blocks
// that don't apply and not disabled by a "badfilter" rule in any of the lists; rules are
// normalized first (NormalizeFilterRule). Returns nullopt if the ruleset can't be written.
std::optional<std::string> BuildUnindexedFilterRuleset(const std::vector<std::string>& lists,
                                                       FilterRulesetStats* stats);

// The lists' element-hiding rules (lb_core::element_hiding), one per line, outside "!#if" blocks
// that don't apply, leaving out lines that aren't UTF-8, the exceptions a "badfilter" rule
// disables and the "badfilter" rules themselves: the browser saves them next to the ruleset and
// indexes them for pages. `count` (if given) gets how many there are.
std::string BuildElementHidingRules(const std::vector<std::string>& lists, size_t* count);

}  // namespace lb

#endif  // LB_BROWSER_ADBLOCK_FILTER_LISTS_H_
