// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The native side of app.littlebrowser.lb.search.SearchEngines: the first-run search engine
// choice (lb/browser/search/search_engine_choice.h, tested by lb_unittests) for a profile, with
// lb-core's rule (tested in lb-core). This file only gathers the profile's state and converts
// types.

#include <jni.h>

#include <cstdint>
#include <string>
#include <vector>

#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url_service.h"
#include "lb/browser/prefs/lb_prefs.h"
#include "lb/browser/search/search_engine_choice.h"
#include "lb/core_bridge/rules.rs.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "lb/android/jni_headers/SearchEngines_jni.h"

namespace lb {

static std::vector<std::string> JNI_SearchEngines_List(JNIEnv* env) {
  std::vector<std::string> fields;
  for (const SearchChoiceEngine& engine : SearchChoiceEngines()) {
    fields.push_back(engine.id);
    fields.push_back(base::UTF16ToUTF8(engine.name));
    fields.push_back(base::UTF16ToUTF8(engine.domain));
  }
  return fields;
}

static int32_t JNI_SearchEngines_NextStep(JNIEnv* env, Profile* profile, bool open) {
  TemplateURLService* service =
      profile->IsOffTheRecord() ? nullptr : TemplateURLServiceFactory::GetForProfile(profile);
  const core::SearchChoiceState state =
      SearchChoiceStateFor(service, profile->IsOffTheRecord(), open,
                           profile->GetPrefs()->GetBoolean(kSearchChoiceShownPref));
  // SearchEngines.Step's values, by name: the bridge's enum may be reordered.
  switch (core::search_choice_step(state)) {
    case core::SearchChoiceStep::Ask:
      return 0;
    case core::SearchChoiceStep::Skip:
      return 1;
    case core::SearchChoiceStep::WaitForOpenChoice:
      return 2;
    case core::SearchChoiceStep::WaitForEngines:
      // Java asks again shortly (SearchEngineScreen).
      service->Load();
      return 3;
  }
  return 1;
}

static bool JNI_SearchEngines_Choose(JNIEnv* env, Profile* profile, const std::string& id) {
  TemplateURLService* service = TemplateURLServiceFactory::GetForProfile(profile);
  if (!service || !SetSearchEngine(*service, id)) {
    return false;
  }
  profile->GetPrefs()->SetBoolean(kSearchChoiceShownPref, true);
  return true;
}

}  // namespace lb

DEFINE_JNI(SearchEngines)
