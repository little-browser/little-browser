// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/adblock/filter_lists.h"

#include <string_view>

#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "components/subresource_filter/core/common/unindexed_ruleset.h"
#include "components/subresource_filter/tools/rule_parser/rule.h"
#include "components/subresource_filter/tools/rule_parser/rule_parser.h"
#include "components/url_pattern_index/proto/rules.pb.h"
#include "lb/core_bridge/rules.rs.h"
#include "third_party/protobuf/src/google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "url/gurl.h"

namespace lb {

namespace {

// Filter lists are downloaded text: anything that isn't UTF-8 becomes U+FFFD on the way to
// lb-core instead of failing the conversion (cxx aborts the browser on anything else).
rust::String Lossy(std::string_view text) {
  return rust::String::lossy(text.data(), text.size());
}

rust::Vec<rust::String> LossyLists(const std::vector<std::string>& lists) {
  rust::Vec<rust::String> texts;
  texts.reserve(lists.size());
  for (const std::string& list : lists) {
    texts.push_back(Lossy(list));
  }
  return texts;
}

core::DownloadedFilterRules ToCore(const DownloadedFilterRules& rules) {
  return {.on_disk = rules.on_disk, .indexed = rules.indexed, .hiding_loaded = rules.hiding_loaded};
}

}  // namespace

const std::vector<FilterList>& FilterLists() {
  static const base::NoDestructor<std::vector<FilterList>> lists([] {
    std::vector<FilterList> all;
    for (const core::FilterListInfo& list : core::filter_lists()) {
      all.push_back({std::string(list.name), std::string(list.url)});
    }
    return all;
  }());
  return *lists;
}

base::TimeDelta FilterListMaxAge() {
  return base::Seconds(core::filter_lists_max_age_secs());
}

base::TimeDelta FilterListRetryAfter() {
  return base::Seconds(core::filter_lists_retry_after_secs());
}

base::TimeDelta FilterListDownloadTimeout() {
  return base::Seconds(core::filter_lists_download_timeout_secs());
}

size_t MaxFilterListFiles() {
  return core::filter_lists_max_files();
}

bool FilterRulesInUse(const DownloadedFilterRules& rules) {
  return core::filter_rules_in_use(ToCore(rules));
}

bool FilterListsNeedUpdate(std::optional<base::Time> last_update,
                           base::Time now,
                           const DownloadedFilterRules& rules) {
  const bool has_last = last_update && !last_update->is_null();
  return core::filter_lists_need_update(
      has_last, has_last ? last_update->InMillisecondsSinceUnixEpoch() : 0,
      now.InMillisecondsSinceUnixEpoch(), ToCore(rules));
}

FilterListsUpdate DecideFilterListsUpdate(bool all_downloaded,
                                          bool any_downloaded,
                                          const DownloadedFilterRules& rules) {
  const core::FilterListsUpdateDecision d =
      core::decide_filter_lists_update(all_downloaded, any_downloaded, ToCore(rules));
  return {.write = d.write, .stamp = d.stamp, .retry = d.retry, .forget_stamp = d.forget_stamp};
}

std::string FilterRulesetVersion(base::Time now) {
  return std::string(core::filter_ruleset_version(now.ToTimeT()));
}

std::optional<std::string> NewestFilterRulesetVersion(const std::vector<std::string>& names) {
  const rust::String newest = core::newest_filter_ruleset_version(LossyLists(names));
  if (newest.empty()) {
    return std::nullopt;
  }
  return std::string(newest);
}

bool FilterListConditionHolds(std::string_view condition) {
  return core::filter_condition_holds(Lossy(condition));
}

std::vector<GURL> FilterListIncludes(const GURL& list_url, std::string_view list) {
  std::vector<GURL> includes;
  if (!list_url.is_valid()) {
    return includes;
  }
  const std::string_view list_path = list_url.path();
  const std::string_view folder = list_path.substr(0, list_path.rfind('/') + 1);
  for (const rust::String& target : core::filter_list_includes(Lossy(list))) {
    const GURL include = list_url.Resolve(std::string(target));
    if (include.is_valid() && include.scheme() == list_url.scheme() &&
        include.host() == list_url.host() && include.port() == list_url.port() &&
        include.path().starts_with(folder) && include != list_url) {
      includes.push_back(include);
    }
  }
  return includes;
}

NormalizedFilterRule NormalizeFilterRule(std::string_view rule) {
  const core::NormalizedFilterRule normalized = core::normalize_filter_rule(Lossy(rule));
  return {.text = std::string(normalized.text), .badfilter = normalized.badfilter};
}

std::optional<std::string> BuildUnindexedFilterRuleset(const std::vector<std::string>& lists,
                                                       FilterRulesetStats* stats) {
  // The URL rules that apply here, normalized, without element hiding (BuildElementHidingRules),
  // "badfilter" rules and what they disable.
  const core::PreparedFilterRules prepared = core::prepare_filter_rules(LossyLists(lists));
  FilterRulesetStats counts;
  counts.element_hiding_rules = static_cast<int>(prepared.element_hiding_rules);
  counts.other_platform_rules = static_cast<int>(prepared.other_platform_rules);
  counts.disabled_rules = static_cast<int>(prepared.disabled_rules);
  std::string ruleset;
  {
    google::protobuf::io::StringOutputStream output(&ruleset);
    subresource_filter::UnindexedRulesetWriter writer(&output);
    subresource_filter::RuleParser parser;
    bool written = true;
    for (const rust::String& rule : prepared.rules) {
      switch (parser.Parse(std::string(rule))) {
        case url_pattern_index::proto::RULE_TYPE_URL:
          written = written && writer.AddUrlRule(parser.url_rule().ToProtobuf());
          ++counts.url_rules;
          break;
        case url_pattern_index::proto::RULE_TYPE_STYLE:
          ++counts.element_hiding_rules;
          break;
        case url_pattern_index::proto::RULE_TYPE_COMMENT:
          break;
        default:
          ++counts.unsupported_rules;
          break;
      }
    }
    if (!written || !writer.Finish() || writer.had_error()) {
      return std::nullopt;
    }
  }
  if (stats) {
    *stats = counts;
  }
  return ruleset;
}

std::string BuildElementHidingRules(const std::vector<std::string>& lists, size_t* count) {
  const core::FilterHidingRules hiding = core::filter_hiding_rules(LossyLists(lists));
  if (count) {
    *count = hiding.count;
  }
  return std::string(hiding.text);
}

}  // namespace lb
