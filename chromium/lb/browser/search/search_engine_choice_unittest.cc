// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/search/search_engine_choice.h"

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "base/test/task_environment.h"
#include "components/search_engines/search_engines_test_environment.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

class SearchEngineChoiceTest : public testing::Test {
 protected:
  SearchEngineChoiceTest() { service().Load(); }

  TemplateURLService& service() { return *search_engines_.template_url_service(); }

  base::test::TaskEnvironment task_environment_;
  search_engines::SearchEnginesTestEnvironment search_engines_;
};

TEST_F(SearchEngineChoiceTest, OffersLbCoresEnginesInItsOrder) {
  const std::vector<SearchChoiceEngine>& engines = SearchChoiceEngines();
  ASSERT_EQ(engines.size(), core::search_choice_engines().size());
  EXPECT_EQ(engines.front().id, "brave");
  EXPECT_EQ(engines.front().name, u"Brave Search");
  EXPECT_EQ(engines.front().domain, u"search.brave.com");
  EXPECT_EQ(engines.back().id, "startpage");
}

TEST_F(SearchEngineChoiceTest, ChromiumsFallbackDefaultIsNoChoice) {
  ASSERT_TRUE(service().loaded());
  EXPECT_TRUE(NoSearchEngineChosen(service()));
}

TEST_F(SearchEngineChoiceTest, ABuiltInEngineBecomesTheUsersChoice) {
  ASSERT_TRUE(SetSearchEngine(service(), "duckduckgo"));
  const TemplateURL* dse = service().GetDefaultSearchProvider();
  ASSERT_TRUE(dse);
  EXPECT_EQ(dse->keyword(), u"duckduckgo.com");
  EXPECT_FALSE(NoSearchEngineChosen(service()));
}

TEST_F(SearchEngineChoiceTest, GoogleIsOursSearchOnlyAndAddedOnce) {
  ASSERT_TRUE(SetSearchEngine(service(), "google"));
  const TemplateURL* google = service().GetDefaultSearchProvider();
  ASSERT_TRUE(google);
  EXPECT_EQ(google->url(), std::string(core::google_search_url()));
  EXPECT_TRUE(google->suggestions_url().empty());
  EXPECT_EQ(google->keyword(), base::UTF8ToUTF16(std::string(core::google_keyword())));
  EXPECT_FALSE(NoSearchEngineChosen(service()));

  // Picked again after another: the same definition, not a second one.
  ASSERT_TRUE(SetSearchEngine(service(), "bing"));
  const size_t count = service().GetTemplateURLs().size();
  ASSERT_TRUE(SetSearchEngine(service(), "google"));
  EXPECT_EQ(service().GetTemplateURLs().size(), count);
  EXPECT_EQ(service().GetDefaultSearchProvider(), google);
}

TEST_F(SearchEngineChoiceTest, AnEngineNotOfferedChangesNothing) {
  const TemplateURL* before = service().GetDefaultSearchProvider();
  EXPECT_FALSE(SetSearchEngine(service(), "yahoo"));
  EXPECT_EQ(service().GetDefaultSearchProvider(), before);
  EXPECT_TRUE(NoSearchEngineChosen(service()));
}

TEST_F(SearchEngineChoiceTest, StateForAProfile) {
  core::SearchChoiceState state = SearchChoiceStateFor(&service(), false, true, true);
  EXPECT_FALSE(state.off_the_record);
  EXPECT_TRUE(state.open);
  EXPECT_TRUE(state.offered_before);
  EXPECT_TRUE(state.has_engines);
  EXPECT_TRUE(state.engines_loaded);
  EXPECT_TRUE(state.no_search_engine);

  SetSearchEngine(service(), "ecosia");
  EXPECT_FALSE(SearchChoiceStateFor(&service(), false, false, false).no_search_engine);
}

TEST_F(SearchEngineChoiceTest, StateWithoutEngines) {
  core::SearchChoiceState state = SearchChoiceStateFor(nullptr, true, false, false);
  EXPECT_TRUE(state.off_the_record);
  EXPECT_FALSE(state.has_engines);
  EXPECT_FALSE(state.engines_loaded);
  EXPECT_FALSE(state.no_search_engine);
}

}  // namespace
}  // namespace lb
