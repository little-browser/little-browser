// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/search/search_engine_choice.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "components/search_engines/default_search_manager.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "third_party/search_engines_data/resources/definitions/prepopulated_engines.h"
#include "url/gurl.h"

namespace lb {

namespace {

// ungoogled-chromium's "No Search" placeholder: its keyword.
constexpr char16_t kNoSearchKeyword[] = u"nosearch";

// Chromium's built-in definitions of the engines lb-core offers, by their ids. Google has none
// here: ungoogled-chromium replaced its built-in entry, and we add our own (SetSearchEngine).
const TemplateURLPrepopulateData::PrepopulatedEngine* Prepopulated(std::string_view id) {
  static const std::pair<std::string_view, const TemplateURLPrepopulateData::PrepopulatedEngine*>
      kBuiltIn[] = {{"bing", &TemplateURLPrepopulateData::bing},
                    {"brave", &TemplateURLPrepopulateData::brave},
                    {"duckduckgo", &TemplateURLPrepopulateData::duckduckgo},
                    {"ecosia", &TemplateURLPrepopulateData::ecosia},
                    {"qwant", &TemplateURLPrepopulateData::qwant},
                    {"startpage", &TemplateURLPrepopulateData::startpage}};
  for (const auto& [name, engine] : kBuiltIn) {
    if (name == id) {
      return engine;
    }
  }
  return nullptr;
}

const SearchChoiceEngine* Offered(std::string_view id) {
  for (const SearchChoiceEngine& engine : SearchChoiceEngines()) {
    if (engine.id == id) {
      return &engine;
    }
  }
  return nullptr;
}

}  // namespace

const std::vector<SearchChoiceEngine>& SearchChoiceEngines() {
  static const base::NoDestructor<std::vector<SearchChoiceEngine>> engines([] {
    std::vector<SearchChoiceEngine> list;
    for (const core::ChoiceEngine& e : core::search_choice_engines()) {
      list.push_back({std::string(e.id), base::UTF8ToUTF16(std::string(e.name)),
                      base::UTF8ToUTF16(std::string(e.domain))});
    }
    return list;
  }());
  return *engines;
}

bool NoSearchEngineChosen(const TemplateURLService& service) {
  const TemplateURL* dse = service.GetDefaultSearchProvider();
  return !dse || dse->keyword() == kNoSearchKeyword ||
         service.default_search_provider_source() == DefaultSearchManager::FROM_FALLBACK;
}

core::SearchChoiceState SearchChoiceStateFor(const TemplateURLService* service,
                                             bool off_the_record,
                                             bool open,
                                             bool offered_before) {
  core::SearchChoiceState state;
  state.off_the_record = off_the_record;
  state.open = open;
  state.offered_before = offered_before;
  state.has_engines = service != nullptr;
  state.engines_loaded = service && service->loaded();
  state.no_search_engine = state.engines_loaded && NoSearchEngineChosen(*service);
  return state;
}

bool SetSearchEngine(TemplateURLService& service, std::string_view id) {
  const SearchChoiceEngine* engine = Offered(id);
  if (!engine) {
    return false;
  }
  if (const TemplateURLPrepopulateData::PrepopulatedEngine* prepopulated = Prepopulated(id)) {
    service.SetUserSelectedDefaultSearchProviderByPrepopulateId(prepopulated->id);
    return true;
  }
  // Google: our own definition (search only — no suggestions, so typing isn't sent to it), found
  // by its URL: Chromium's built-in Google (on Android) has the same keyword, and suggestions.
  const std::string url(core::google_search_url());
  TemplateURL* google = nullptr;
  for (TemplateURL* existing : service.GetTemplateURLs()) {
    if (existing->url() == url && existing->suggestions_url().empty()) {
      google = existing;
      break;
    }
  }
  if (!google) {
    TemplateURLData data;
    data.SetShortName(engine->name);
    data.SetKeyword(base::UTF8ToUTF16(std::string(core::google_keyword())));
    data.SetURL(url);
    data.favicon_url = GURL(std::string(core::google_favicon_url()));
    data.input_encodings.push_back("UTF-8");
    google = service.Add(std::make_unique<TemplateURL>(data));
  }
  if (!google) {
    return false;
  }
  service.SetUserSelectedDefaultSearchProvider(google);
  return true;
}

}  // namespace lb
