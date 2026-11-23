// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/adblock/filter_lists.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/time/time.h"
#include "components/subresource_filter/core/common/first_party_origin.h"
#include "components/subresource_filter/core/common/indexed_ruleset.h"
#include "components/subresource_filter/core/common/load_policy.h"
#include "components/subresource_filter/core/common/unindexed_ruleset.h"
#include "components/url_pattern_index/proto/rules.pb.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/protobuf/src/google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace lb {
namespace {

using url_pattern_index::proto::ELEMENT_TYPE_IMAGE;
using url_pattern_index::proto::ELEMENT_TYPE_SCRIPT;

// Indexes an unindexed ruleset the way Chromium's ruleset service does.
class IndexedRuleset {
 public:
  explicit IndexedRuleset(const std::string& unindexed) : indexer_(/*ruleset_id=*/1) {
    google::protobuf::io::ArrayInputStream input(unindexed.data(),
                                                 static_cast<int>(unindexed.size()));
    subresource_filter::UnindexedRulesetReader reader(&input);
    url_pattern_index::proto::FilteringRules chunk;
    while (reader.ReadNextChunk(&chunk)) {
      for (const auto& rule : chunk.url_rules()) {
        EXPECT_TRUE(indexer_.AddUrlRule(rule));
      }
    }
    indexer_.Finish();
    matcher_ = std::make_unique<subresource_filter::IndexedRulesetMatcher>(indexer_.data());
  }

  bool Blocks(const std::string& url,
              const std::string& page,
              url_pattern_index::proto::ElementType type) const {
    return matcher_->GetLoadPolicyForResourceLoad(
               GURL(url), subresource_filter::FirstPartyOrigin(url::Origin::Create(GURL(page))),
               type,
               /*disable_generic_rules=*/false,
               /*out_rule=*/nullptr) == subresource_filter::LoadPolicy::DISALLOW;
  }

 private:
  subresource_filter::RulesetIndexer indexer_;
  std::unique_ptr<subresource_filter::IndexedRulesetMatcher> matcher_;
};

TEST(FilterListsTest, TheListsFromTheSpecOverHttps) {
  std::vector<std::string> names;
  for (const FilterList& list : FilterLists()) {
    names.push_back(list.name);
    EXPECT_TRUE(GURL(list.url).SchemeIs("https")) << list.url;
  }
  EXPECT_EQ(names, (std::vector<std::string>{"EasyList", "EasyPrivacy", "uBlock filters",
                                             "Peter Lowe's list"}));
}

// Rules on disk and all in use.
constexpr DownloadedFilterRules kInUse = {.on_disk = true, .indexed = true, .hiding_loaded = true};

TEST(FilterListsTest, UpdateWhenNeverDownloaded) {
  const base::Time now = base::Time::Now();
  EXPECT_TRUE(FilterListsNeedUpdate(std::nullopt, now, kInUse));
  EXPECT_TRUE(FilterListsNeedUpdate(base::Time(), now, kInUse));
  EXPECT_TRUE(FilterListsNeedUpdate(std::nullopt, now, DownloadedFilterRules()));
}

TEST(FilterListsTest, UpdateWhenOld) {
  const base::Time now = base::Time::Now();
  EXPECT_FALSE(FilterListsNeedUpdate(now - base::Hours(1), now, kInUse));
  EXPECT_FALSE(FilterListsNeedUpdate(now - kFilterListMaxAge + base::Minutes(1), now, kInUse));
  EXPECT_TRUE(FilterListsNeedUpdate(now - kFilterListMaxAge, now, kInUse));
  EXPECT_TRUE(FilterListsNeedUpdate(now - base::Days(30), now, kInUse));
}

TEST(FilterListsTest, UpdateWhenTheClockWentBack) {
  const base::Time now = base::Time::Now();
  EXPECT_TRUE(FilterListsNeedUpdate(now + base::Hours(1), now, kInUse));
}

// The subresource filter never retries a version whose indexing was interrupted, so recent
// lists that were never indexed are downloaded again as a new version.
TEST(FilterListsTest, UpdateWhenTheNewestRulesWereNeverIndexed) {
  const base::Time now = base::Time::Now();
  DownloadedFilterRules rules = kInUse;
  rules.indexed = false;
  EXPECT_FALSE(FilterRulesInUse(rules));
  EXPECT_TRUE(FilterListsNeedUpdate(now - base::Hours(1), now, rules));
}

// A version saved without element-hiding rules, or with a damaged file, is downloaded again.
TEST(FilterListsTest, UpdateWhenTheHidingRulesDidntLoad) {
  const base::Time now = base::Time::Now();
  DownloadedFilterRules rules = kInUse;
  rules.hiding_loaded = false;
  EXPECT_FALSE(FilterRulesInUse(rules));
  EXPECT_TRUE(FilterListsNeedUpdate(now - base::Hours(1), now, rules));
  // Nothing on disk is nothing to redo: the update time decides.
  EXPECT_TRUE(FilterRulesInUse(DownloadedFilterRules()));
  EXPECT_FALSE(FilterListsNeedUpdate(now - base::Hours(1), now, DownloadedFilterRules()));
}

TEST(FilterListsTest, AllDownloadedIsUsedAndRecorded) {
  for (const DownloadedFilterRules& rules : {DownloadedFilterRules(), kInUse}) {
    const FilterListsUpdate update = DecideFilterListsUpdate(true, true, rules);
    EXPECT_TRUE(update.write);
    EXPECT_TRUE(update.stamp);
    EXPECT_FALSE(update.retry);
    EXPECT_FALSE(update.forget_stamp);
  }
}

TEST(FilterListsTest, AFailedDownloadKeepsTheRulesAndRetries) {
  for (bool indexed : {true, false}) {
    DownloadedFilterRules rules = kInUse;
    rules.indexed = indexed;
    const FilterListsUpdate update = DecideFilterListsUpdate(false, true, rules);
    EXPECT_FALSE(update.write);
    EXPECT_FALSE(update.stamp);
    EXPECT_TRUE(update.retry);
    EXPECT_FALSE(update.forget_stamp);
  }
}

// With no rules yet, three lists are better than none; it isn't recorded as an update, and the
// retries keep downloading until every file comes.
TEST(FilterListsTest, AFailedDownloadWithNoRulesUsesWhatArrived) {
  const FilterListsUpdate update = DecideFilterListsUpdate(false, true, DownloadedFilterRules());
  EXPECT_TRUE(update.write);
  EXPECT_FALSE(update.stamp);
  EXPECT_TRUE(update.retry);
  EXPECT_TRUE(update.forget_stamp);

  const FilterListsUpdate nothing = DecideFilterListsUpdate(false, false, DownloadedFilterRules());
  EXPECT_FALSE(nothing.write);
  EXPECT_FALSE(nothing.stamp);
  EXPECT_TRUE(nothing.retry);
  EXPECT_FALSE(nothing.forget_stamp);
}

// Rules on disk without their element-hiding rules count as none: what arrived fills them in,
// rather than waiting for every file while nothing is hidden.
TEST(FilterListsTest, AFailedDownloadFillsInMissingHidingRules) {
  DownloadedFilterRules rules = kInUse;
  rules.hiding_loaded = false;
  const FilterListsUpdate update = DecideFilterListsUpdate(false, true, rules);
  EXPECT_TRUE(update.write);
  EXPECT_FALSE(update.stamp);
  EXPECT_TRUE(update.retry);
  EXPECT_TRUE(update.forget_stamp);
}

TEST(FilterListsTest, DownloadsGiveUpAndRetryInReasonableTime) {
  EXPECT_GE(kFilterListDownloadTimeout, base::Minutes(1));
  EXPECT_LE(kFilterListDownloadTimeout, base::Minutes(10));
  EXPECT_LT(kFilterListRetryAfter, kFilterListMaxAge);
}

TEST(FilterListsTest, VersionIsReadableAndSortsInTimeOrder) {
  base::Time t;
  ASSERT_TRUE(base::Time::FromUTCString("2026-11-21 15:04:05", &t));
  EXPECT_EQ(FilterRulesetVersion(t), "lb-2026-11-21-150405");
  EXPECT_LT(FilterRulesetVersion(t), FilterRulesetVersion(t + base::Seconds(1)));
  EXPECT_LT(FilterRulesetVersion(t), FilterRulesetVersion(t + base::Days(10)));
  EXPECT_LT(FilterRulesetVersion(t), FilterRulesetVersion(t + base::Days(400)));
}

TEST(FilterListsTest, NewestVersionIgnoresOtherFolders) {
  EXPECT_EQ(NewestFilterRulesetVersion({}), std::nullopt);
  EXPECT_EQ(NewestFilterRulesetVersion({"tmp", ".DS_Store"}), std::nullopt);
  EXPECT_EQ(NewestFilterRulesetVersion(
                {"lb-2026-11-21-150405", "lb-2026-12-01-000000", "zzz", "lb-2026-02-28-235959"}),
            "lb-2026-12-01-000000");
}

TEST(FilterListsTest, ConditionsForChromiumOnTheDesktop) {
  EXPECT_TRUE(FilterListConditionHolds("env_chromium"));
  EXPECT_TRUE(FilterListConditionHolds(" env_chromium "));
  EXPECT_FALSE(FilterListConditionHolds("env_firefox"));
  EXPECT_FALSE(FilterListConditionHolds("env_safari"));
  EXPECT_FALSE(FilterListConditionHolds("env_mobile"));
  EXPECT_FALSE(FilterListConditionHolds("ext_ubol"));
  EXPECT_FALSE(FilterListConditionHolds("cap_html_filtering"));
  EXPECT_FALSE(FilterListConditionHolds("false"));
  EXPECT_FALSE(FilterListConditionHolds("something_new"));
  EXPECT_TRUE(FilterListConditionHolds("!env_mobile"));
  EXPECT_TRUE(FilterListConditionHolds("!!env_chromium"));
  EXPECT_TRUE(FilterListConditionHolds("env_chromium && !env_firefox"));
  EXPECT_FALSE(FilterListConditionHolds("env_chromium && env_mobile"));
  EXPECT_TRUE(FilterListConditionHolds("env_firefox || env_chromium"));
  EXPECT_TRUE(FilterListConditionHolds("!(env_firefox || env_safari)"));
  EXPECT_FALSE(FilterListConditionHolds("(env_firefox || env_safari) && env_chromium"));
}

TEST(FilterListsTest, MalformedConditionsDontHold) {
  EXPECT_FALSE(FilterListConditionHolds(""));
  EXPECT_FALSE(FilterListConditionHolds("env_chromium &&"));
  EXPECT_FALSE(FilterListConditionHolds("(env_chromium"));
  EXPECT_FALSE(FilterListConditionHolds("env_chromium)"));
  EXPECT_FALSE(FilterListConditionHolds("!"));
  EXPECT_FALSE(FilterListConditionHolds(std::string(10000, '!') + "env_firefox"));
  EXPECT_FALSE(FilterListConditionHolds(std::string(10000, '(') + "env_chromium"));
}

TEST(FilterListsTest, KeepsUrlRulesAndCountsTheRest) {
  FilterRulesetStats stats;
  const std::optional<std::string> ruleset =
      BuildUnindexedFilterRuleset({"[Adblock Plus 2.0]\n! Title: test\n||ads.example^\n##.banner\n"
                                   "example.com,~www.example.com##.slot\n",
                                   "@@||ads.example/allowed.js\r\n||x.example^$notanoption\n\n"},
                                  &stats);
  ASSERT_TRUE(ruleset);
  EXPECT_FALSE(ruleset->empty());
  EXPECT_EQ(stats.url_rules, 2);
  // Element hiding takes rules Chromium's parser can't (one that excludes a site): they aren't
  // counted as unsupported.
  EXPECT_EQ(stats.element_hiding_rules, 2);
  EXPECT_EQ(stats.unsupported_rules, 1);
}

TEST(FilterListsTest, IncludesNextToTheList) {
  const GURL list("https://ublockorigin.github.io/uAssets/filters/filters.txt");
  const std::vector<GURL> includes = FilterListIncludes(list,
                                                        "! Title: uBlock filters\n"
                                                        "!#include filters-2024.txt\n"
                                                        "!#if env_mobile\n"
                                                        "!#include filters-mobile.txt\n"
                                                        "!#endif\n"
                                                        "!#include  sub/more.txt \n"
                                                        "!#include ../elsewhere.txt\n"
                                                        "!#include /uAssets/elsewhere.txt\n"
                                                        "!#include https://other.example/x.txt\n"
                                                        "!#include filters.txt\n"
                                                        "||ads.example^\n");
  EXPECT_EQ(includes, (std::vector<GURL>{
                          GURL("https://ublockorigin.github.io/uAssets/filters/filters-2024.txt"),
                          GURL("https://ublockorigin.github.io/uAssets/filters/sub/more.txt")}));
  EXPECT_TRUE(FilterListIncludes(GURL(), "!#include a.txt").empty());
}

TEST(FilterListsTest, NormalizesUblockOptionNames) {
  EXPECT_EQ(NormalizeFilterRule("@@||captcha.example^$frame").text,
            "@@||captcha.example^$subdocument");
  EXPECT_EQ(NormalizeFilterRule("@@||player.example/sdk.js$script,xhr,domain=news.example").text,
            "@@||player.example/sdk.js$script,xmlhttprequest,domain=news.example");
  EXPECT_EQ(NormalizeFilterRule("||ads.example^$3p").text, "||ads.example^$third-party");
  EXPECT_EQ(NormalizeFilterRule("||ads.example^$1p").text, "||ads.example^$~third-party");
  EXPECT_EQ(NormalizeFilterRule("||ads.example^$~1p").text, "||ads.example^$third-party");
  EXPECT_EQ(NormalizeFilterRule("||ads.example^$css,from=a.example|~b.example").text,
            "||ads.example^$stylesheet,domain=a.example|~b.example");
  EXPECT_EQ(NormalizeFilterRule("@@||site.example^$doc").text, "@@||site.example^$document");
  EXPECT_EQ(NormalizeFilterRule("||ads.example^$important").text, "||ads.example^");
  EXPECT_EQ(NormalizeFilterRule("||ads.example^$all,important").text, "||ads.example^");
  EXPECT_EQ(NormalizeFilterRule("||ads.example^$script,important").text, "||ads.example^$script");
}

TEST(FilterListsTest, LeavesOtherRulesAlone) {
  for (const char* rule : {"||ads.example^", "||ads.example^$script,third-party", "! a $comment",
                           "example.com##.ad$x", "/ads[0-9]+\\.js$/", "@@||a.example^$ghide2"}) {
    const NormalizedFilterRule normalized = NormalizeFilterRule(rule);
    EXPECT_EQ(normalized.text, rule);
    EXPECT_FALSE(normalized.badfilter);
  }
}

TEST(FilterListsTest, RecognizesBadfilter) {
  const NormalizedFilterRule rule = NormalizeFilterRule("||ads.example^$3p,badfilter");
  EXPECT_TRUE(rule.badfilter);
  EXPECT_EQ(rule.text, "||ads.example^$third-party");
}

// uBlock filters' exceptions use uBlock's option names; Chromium's parser would drop them, and
// the sites they fix would stay broken.
TEST(FilterListsTest, KeepsExceptionsWrittenWithUblockOptionNames) {
  FilterRulesetStats stats;
  const std::optional<std::string> ruleset = BuildUnindexedFilterRuleset(
      {"||captcha.example^\n||player.example^\n",
       "@@||captcha.example^$frame\n@@||player.example/sdk.js$script,xhr,domain=news.example\n"},
      &stats);
  ASSERT_TRUE(ruleset);
  EXPECT_EQ(stats.url_rules, 4);
  EXPECT_EQ(stats.unsupported_rules, 0);
  const IndexedRuleset indexed(*ruleset);
  const std::string page = "https://news.example/";
  EXPECT_FALSE(indexed.Blocks("https://captcha.example/c", page,
                              url_pattern_index::proto::ELEMENT_TYPE_SUBDOCUMENT));
  EXPECT_FALSE(indexed.Blocks("https://player.example/sdk.js", page,
                              url_pattern_index::proto::ELEMENT_TYPE_XMLHTTPREQUEST));
  EXPECT_TRUE(indexed.Blocks("https://player.example/ad.js", page, ELEMENT_TYPE_SCRIPT));
}

// "badfilter" in one list turns off a rule from another.
TEST(FilterListsTest, BadfilterDisablesTheRuleItNames) {
  FilterRulesetStats stats;
  const std::optional<std::string> ruleset = BuildUnindexedFilterRuleset(
      {"||cdn.example^$third-party\n||ads.example^\n", "||cdn.example^$3p,badfilter\n"}, &stats);
  ASSERT_TRUE(ruleset);
  EXPECT_EQ(stats.url_rules, 1);
  EXPECT_EQ(stats.disabled_rules, 2);
  const IndexedRuleset indexed(*ruleset);
  const std::string page = "https://news.example/";
  EXPECT_FALSE(indexed.Blocks("https://cdn.example/lib.js", page, ELEMENT_TYPE_SCRIPT));
  EXPECT_TRUE(indexed.Blocks("https://ads.example/ad.js", page, ELEMENT_TYPE_SCRIPT));
}

TEST(FilterListsTest, RulesetBlocksWhatTheListsSay) {
  const std::optional<std::string> ruleset = BuildUnindexedFilterRuleset(
      {"||ads.example^\n@@||ads.example/allowed.js\n||tracker.example^$third-party\n"}, nullptr);
  ASSERT_TRUE(ruleset);
  const IndexedRuleset indexed(*ruleset);
  const std::string page = "https://news.example/story";

  EXPECT_TRUE(indexed.Blocks("https://ads.example/banner.js", page, ELEMENT_TYPE_SCRIPT));
  EXPECT_TRUE(indexed.Blocks("https://cdn.ads.example/pixel.gif", page, ELEMENT_TYPE_IMAGE));
  // Exceptions win.
  EXPECT_FALSE(indexed.Blocks("https://ads.example/allowed.js", page, ELEMENT_TYPE_SCRIPT));
  // $third-party: blocked on other sites only.
  EXPECT_TRUE(indexed.Blocks("https://tracker.example/t.js", page, ELEMENT_TYPE_SCRIPT));
  EXPECT_FALSE(indexed.Blocks("https://tracker.example/t.js", "https://tracker.example/",
                              ELEMENT_TYPE_SCRIPT));
  // Everything else loads.
  EXPECT_FALSE(indexed.Blocks("https://news.example/app.js", page, ELEMENT_TYPE_SCRIPT));
  EXPECT_FALSE(indexed.Blocks("https://example.org/ads.example.js", page, ELEMENT_TYPE_SCRIPT));
}

// uBlock filters has rules for other browsers in "!#if" blocks, such as exceptions that only
// Firefox needs; here they would unblock ads.
TEST(FilterListsTest, LeavesOutRulesForOtherPlatforms) {
  FilterRulesetStats stats;
  const std::optional<std::string> ruleset = BuildUnindexedFilterRuleset(
      {"||ads.example^\n"
       "!#if env_firefox\n"
       "@@||ads.example/firefox.js$script,domain=news.example\n"
       "!#endif\n"
       "!#if !env_mobile\n"
       "@@||ads.example/desktop.js\n"
       "!#else\n"
       "@@||ads.example/mobile.js\n"
       "!#endif\n"
       "!#if env_chromium\n"
       "!#if env_safari\n"
       "@@||ads.example/safari.js\n"
       "!#endif\n"
       "@@||ads.example/chromium.js\n"
       "!#endif\n",
       // Each list starts outside any block, even if the one before left one open.
       "!#if env_firefox\n"},
      &stats);
  ASSERT_TRUE(ruleset);
  EXPECT_EQ(stats.url_rules, 3);
  EXPECT_EQ(stats.other_platform_rules, 3);
  const IndexedRuleset indexed(*ruleset);
  const std::string page = "https://news.example/";
  EXPECT_TRUE(indexed.Blocks("https://ads.example/firefox.js", page, ELEMENT_TYPE_SCRIPT));
  EXPECT_FALSE(indexed.Blocks("https://ads.example/desktop.js", page, ELEMENT_TYPE_SCRIPT));
  EXPECT_TRUE(indexed.Blocks("https://ads.example/mobile.js", page, ELEMENT_TYPE_SCRIPT));
  EXPECT_TRUE(indexed.Blocks("https://ads.example/safari.js", page, ELEMENT_TYPE_SCRIPT));
  EXPECT_FALSE(indexed.Blocks("https://ads.example/chromium.js", page, ELEMENT_TYPE_SCRIPT));

  // A stray "!#else" or "!#endif" outside a block changes nothing.
  const std::optional<std::string> stray =
      BuildUnindexedFilterRuleset({"!#endif\n!#else\n||ads.example^\n"}, &stats);
  ASSERT_TRUE(stray);
  EXPECT_EQ(stats.url_rules, 1);
  EXPECT_EQ(stats.other_platform_rules, 0);
}

// Element hiding keeps the lists' CSS rules and site switches, in the blocks that apply, and
// nothing that needs script.
TEST(FilterListsTest, ElementHidingRulesFromTheActiveLines) {
  size_t count = 0;
  const std::string rules = BuildElementHidingRules({"! Title: a list\n"
                                                     "||ads.example^\n"
                                                     "##.ad-box\n"
                                                     "news.example##.slot\n"
                                                     "news.example#@#.ad-box\n"
                                                     "@@||shop.example^$generichide\n"
                                                     "news.example##+js(set-constant, x, 1)\n"
                                                     "news.example#?#.post:has-text(Sponsored)\n"
                                                     "!#if env_firefox\n"
                                                     "##.firefox-only\n"
                                                     "!#endif\n",
                                                     "!#if env_chromium\n"
                                                     "##[data-ad]\n"
                                                     "!#endif\n"},
                                                    &count);
  EXPECT_EQ(rules,
            "##.ad-box\n"
            "news.example##.slot\n"
            "news.example#@#.ad-box\n"
            "@@||shop.example^$generichide\n"
            "##[data-ad]\n");
  EXPECT_EQ(count, 5u);
  EXPECT_EQ(BuildElementHidingRules({"||ads.example^\n"}, nullptr), "");
}

// A line that isn't UTF-8 (a stray Latin-1 byte, a proxy's page in another encoding) is left
// out: lb-core takes only UTF-8, and cxx would abort the browser.
TEST(FilterListsTest, ElementHidingLeavesOutLinesThatArentUtf8) {
  size_t count = 0;
  const std::string rules = BuildElementHidingRules(
      {"! Title: Caf\xe9 list\n##.ad\nexample.com##.caf\xe9\n\xff\xfe##.x\n##.ok\n"}, &count);
  EXPECT_EQ(rules, "##.ad\n##.ok\n");
  EXPECT_EQ(count, 2u);
}

// "badfilter" cancels an exception across lists, as it cancels URL rules; neither line remains.
TEST(FilterListsTest, ElementHidingAppliesBadfilter) {
  const std::string rules = BuildElementHidingRules(
      {"@@||news.example^$generichide\n@@||shop.example^$ehide\n##.ad\n",
       "@@||news.example^$generichide,badfilter\n@@||other.example^$ghide,badfilter\n"},
      nullptr);
  EXPECT_EQ(rules, "@@||shop.example^$ehide\n##.ad\n");
}

}  // namespace
}  // namespace lb
