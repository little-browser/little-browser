// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_SEARCH_SEARCH_ENGINE_CHOICE_H_
#define LB_BROWSER_SEARCH_SEARCH_ENGINE_CHOICE_H_

#include <string>
#include <string_view>
#include <vector>

#include "lb/core_bridge/rules.rs.h"

class TemplateURLService;

namespace lb {

// The first-run search engine choice on Chromium's search engines (docs/ux.md §12, docs/design.md
// §5), for every platform's question: the desktop's dialog and Android's screen. Which engines are
// offered and when to ask are lb-core's (lb_core::search_choice).

// One engine the choice offers.
struct SearchChoiceEngine {
  // lb-core's id: Chromium's prepopulated-engine name, or "google" (our own definition).
  std::string id;
  std::u16string name;
  std::u16string domain;
};

// The engines offered, in lb-core's order.
const std::vector<SearchChoiceEngine>& SearchChoiceEngines();

// Whether the profile still has no engine the user chose: none at all, ungoogled-chromium's
// "No Search" placeholder (the desktop's default), or Chromium's own fallback default (Android's,
// which is Google). Only meaningful once `service` is loaded.
bool NoSearchEngineChosen(const TemplateURLService& service);

// What lb-core's rule needs to know about a profile: `service` is its search engines (null for
// one without, such as an incognito profile), `open` whether the choice is open in one of its
// windows, `offered_before` its lb.search_choice_shown pref.
core::SearchChoiceState SearchChoiceStateFor(const TemplateURLService* service,
                                             bool off_the_record,
                                             bool open,
                                             bool offered_before);

// Makes the engine lb-core calls `id` the profile's default, as the user's choice. Google is our
// own definition, search only with no suggestion URL, so typing isn't sent to it; the others are
// Chromium's built-in ones. False, changing nothing, for an id the choice doesn't offer.
bool SetSearchEngine(TemplateURLService& service, std::string_view id);

}  // namespace lb

#endif  // LB_BROWSER_SEARCH_SEARCH_ENGINE_CHOICE_H_
