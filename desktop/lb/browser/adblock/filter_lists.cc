// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/adblock/filter_lists.h"

#include <algorithm>
#include <set>
#include <string_view>

#include "base/functional/function_ref.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "components/subresource_filter/core/common/unindexed_ruleset.h"
#include "components/subresource_filter/tools/rule_parser/rule.h"
#include "components/subresource_filter/tools/rule_parser/rule_parser.h"
#include "components/url_pattern_index/proto/rules.pb.h"
#include "third_party/protobuf/src/google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "url/gurl.h"

namespace lb {

namespace {

// docs/design.md section 5. EasyList and EasyPrivacy: GPLv3 or CC BY-SA 3.0; uBlock filters:
// GPLv3; Peter Lowe's list: free to use and combine. The browser downloads them itself; we don't
// redistribute them.
constexpr FilterList kFilterLists[] = {
    {"EasyList", "https://easylist.to/easylist/easylist.txt"},
    {"EasyPrivacy", "https://easylist.to/easylist/easyprivacy.txt"},
    {"uBlock filters", "https://ublockorigin.github.io/uAssets/filters/filters.txt"},
    {"Peter Lowe's list",
     "https://pgl.yoyo.org/adservers/"
     "serverlist.php?hostformat=adblockplus&showintro=1&mimetype=plaintext"},
};

// uBlock Origin's "!#if" tokens that hold here: a Chromium browser that supports user
// stylesheets. Everything else (env_firefox, env_safari, env_mobile, env_mv3, ext_ubol,
// cap_html_filtering, adguard, false, ...) is false.
constexpr std::string_view kTrueConditions[] = {"env_chromium", "cap_user_stylesheet"};

// Evaluates a "!#if" condition: tokens, "!", "&&", "||" and parentheses. nullopt if malformed.
class ConditionParser {
 public:
  explicit ConditionParser(std::string_view text) : text_(text) {}

  std::optional<bool> Parse() {
    const std::optional<bool> value = Or();
    SkipSpaces();
    if (!value || pos_ != text_.size()) {
      return std::nullopt;
    }
    return value;
  }

 private:
  // Deep enough for any real list; stops runaway recursion on hostile input.
  static constexpr int kMaxDepth = 32;

  void SkipSpaces() {
    while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t')) {
      ++pos_;
    }
  }

  bool Take(std::string_view token) {
    SkipSpaces();
    if (text_.substr(pos_).starts_with(token)) {
      pos_ += token.size();
      return true;
    }
    return false;
  }

  std::optional<bool> Or() {
    std::optional<bool> value = And();
    while (value && Take("||")) {
      const std::optional<bool> right = And();
      if (!right) {
        return std::nullopt;
      }
      value = *value || *right;
    }
    return value;
  }

  std::optional<bool> And() {
    std::optional<bool> value = Unary();
    while (value && Take("&&")) {
      const std::optional<bool> right = Unary();
      if (!right) {
        return std::nullopt;
      }
      value = *value && *right;
    }
    return value;
  }

  std::optional<bool> Unary() {
    if (++depth_ > kMaxDepth) {
      return std::nullopt;
    }
    std::optional<bool> value;
    if (Take("!")) {
      value = Unary();
      if (value) {
        value = !*value;
      }
    } else if (Take("(")) {
      value = Or();
      if (value && !Take(")")) {
        value = std::nullopt;
      }
    } else {
      const size_t start = pos_;
      while (pos_ < text_.size() &&
             (base::IsAsciiAlphaNumeric(text_[pos_]) || text_[pos_] == '_')) {
        ++pos_;
      }
      if (pos_ > start) {
        value = std::ranges::find(kTrueConditions, text_.substr(start, pos_ - start)) !=
                std::end(kTrueConditions);
      }
    }
    --depth_;
    return value;
  }

  const std::string_view text_;
  size_t pos_ = 0;
  int depth_ = 0;
};

// Calls `on_line` for each non-empty line of `list` outside "!#if" blocks that don't apply, and
// returns how many rules (not comments) it left out.
int ForEachActiveLine(std::string_view list, base::FunctionRef<void(std::string_view)> on_line) {
  // One entry per open "!#if": whether its current branch applies.
  std::vector<bool> branches;
  int left_out = 0;
  for (std::string_view line :
       base::SplitStringPiece(list, "\r\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (line.starts_with("!#if ") || line == "!#if") {
      branches.push_back(FilterListConditionHolds(line.substr(4)));
      continue;
    }
    if (line == "!#else") {
      if (!branches.empty()) {
        branches.back() = !branches.back();
      }
      continue;
    }
    if (line == "!#endif") {
      if (!branches.empty()) {
        branches.pop_back();
      }
      continue;
    }
    if (std::ranges::all_of(branches, [](bool applies) { return applies; })) {
      on_line(line);
    } else if (line[0] != '!') {
      ++left_out;
    }
  }
  return left_out;
}

// uBlock Origin's option names and the Adblock Plus names Chromium's parser knows. `flip` turns
// "1p" (first party) into "~third-party".
struct OptionAlias {
  std::string_view ubo;
  std::string_view abp;
  bool flip = false;
};
constexpr OptionAlias kOptionAliases[] = {
    {"xhr", "xmlhttprequest"},
    {"frame", "subdocument"},
    {"doc", "document"},
    {"css", "stylesheet"},
    {"3p", "third-party"},
    {"1p", "third-party", true},
    {"first-party", "third-party", true},
    {"from", "domain"},
    {"ehide", "elemhide"},
    {"ghide", "generichide"},
};
// Options uBlock Origin has that change nothing here: "important" (Chromium's exceptions always
// win) and "all" (every type, which is what a rule without types already means here).
constexpr std::string_view kIgnoredOptions[] = {"important", "all"};

bool IsElementHidingRule(std::string_view rule) {
  for (std::string_view separator : {"##", "#@#", "#?#", "#$#", "#%#"}) {
    if (rule.find(separator) != std::string_view::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

base::span<const FilterList> FilterLists() {
  return kFilterLists;
}

bool FilterListsNeedUpdate(std::optional<base::Time> last_update,
                           base::Time now,
                           bool newest_rules_indexed) {
  if (!last_update || last_update->is_null() || !newest_rules_indexed) {
    return true;
  }
  return *last_update > now || now - *last_update >= kFilterListMaxAge;
}

FilterListsUpdate DecideFilterListsUpdate(bool all_downloaded,
                                          bool any_downloaded,
                                          bool have_rules) {
  if (all_downloaded) {
    return {.write = true, .stamp = true, .retry = false};
  }
  return {.write = any_downloaded && !have_rules, .stamp = false, .retry = true};
}

std::string FilterRulesetVersion(base::Time now) {
  base::Time::Exploded t;
  now.UTCExplode(&t);
  return base::StringPrintf("lb-%04d-%02d-%02d-%02d%02d%02d", t.year, t.month, t.day_of_month,
                            t.hour, t.minute, t.second);
}

std::optional<std::string> NewestFilterRulesetVersion(const std::vector<std::string>& names) {
  std::optional<std::string> newest;
  for (const std::string& name : names) {
    if (name.starts_with("lb-") && base::IsStringASCII(name) && (!newest || name > *newest)) {
      newest = name;
    }
  }
  return newest;
}

bool FilterListConditionHolds(std::string_view condition) {
  return ConditionParser(condition).Parse().value_or(false);
}

std::vector<GURL> FilterListIncludes(const GURL& list_url, std::string_view list) {
  std::vector<GURL> includes;
  if (!list_url.is_valid()) {
    return includes;
  }
  const std::string_view list_path = list_url.path();
  const std::string_view folder = list_path.substr(0, list_path.rfind('/') + 1);
  ForEachActiveLine(list, [&](std::string_view line) {
    if (!line.starts_with("!#include ")) {
      return;
    }
    const GURL include = list_url.Resolve(
        base::TrimWhitespaceASCII(line.substr(sizeof("!#include ") - 1), base::TRIM_ALL));
    if (include.is_valid() && include.scheme() == list_url.scheme() &&
        include.host() == list_url.host() && include.port() == list_url.port() &&
        include.path().starts_with(folder) && include != list_url) {
      includes.push_back(include);
    }
  });
  return includes;
}

NormalizedFilterRule NormalizeFilterRule(std::string_view rule) {
  NormalizedFilterRule normalized{.text = std::string(rule)};
  // As Chromium's parser splits off the options: at the last "$", unless the rule is a /regex/.
  const size_t dollar = rule.rfind('$');
  if (rule.starts_with('!') || IsElementHidingRule(rule) || dollar == std::string_view::npos ||
      rule.back() == '/') {
    return normalized;
  }
  std::string options;
  for (std::string_view option : base::SplitStringPiece(
           rule.substr(dollar + 1), ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    bool negated = option.starts_with('~');
    std::string_view name = negated ? option.substr(1) : option;
    std::string_view value;
    if (const size_t equals = name.find('='); equals != std::string_view::npos) {
      value = name.substr(equals);
      name = name.substr(0, equals);
    }
    if (name == "badfilter") {
      normalized.badfilter = true;
      continue;
    }
    if (std::ranges::find(kIgnoredOptions, name) != std::end(kIgnoredOptions)) {
      continue;
    }
    for (const OptionAlias& alias : kOptionAliases) {
      if (name == alias.ubo) {
        name = alias.abp;
        negated = negated != alias.flip;
        break;
      }
    }
    if (!options.empty()) {
      options += ',';
    }
    if (negated) {
      options += '~';
    }
    options.append(name).append(value);
  }
  normalized.text = std::string(rule.substr(0, dollar));
  if (!options.empty()) {
    normalized.text += '$' + options;
  }
  return normalized;
}

std::optional<std::string> BuildUnindexedFilterRuleset(const std::vector<std::string>& lists,
                                                       FilterRulesetStats* stats) {
  FilterRulesetStats counts;
  std::string ruleset;
  {
    google::protobuf::io::StringOutputStream output(&ruleset);
    subresource_filter::UnindexedRulesetWriter writer(&output);
    subresource_filter::RuleParser parser;
    // uBlock Origin's "badfilter": a rule that disables another rule, from any list.
    std::set<std::string> disabled;
    for (const std::string& list : lists) {
      ForEachActiveLine(list, [&](std::string_view line) {
        if (NormalizedFilterRule rule = NormalizeFilterRule(line); rule.badfilter) {
          disabled.insert(std::move(rule.text));
        }
      });
    }
    bool written = true;
    for (const std::string& list : lists) {
      counts.other_platform_rules += ForEachActiveLine(list, [&](std::string_view line) {
        const NormalizedFilterRule rule = NormalizeFilterRule(line);
        if (rule.badfilter || disabled.contains(rule.text)) {
          ++counts.disabled_rules;
          return;
        }
        switch (parser.Parse(rule.text)) {
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
      });
      if (!written) {
        return std::nullopt;
      }
    }
    if (!writer.Finish() || writer.had_error()) {
      return std::nullopt;
    }
  }
  if (stats) {
    *stats = counts;
  }
  return ruleset;
}

}  // namespace lb
