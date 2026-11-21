// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/search/search_choice.h"

namespace lb {

SearchChoiceStep NextSearchChoiceStep(const SearchChoiceState& state) {
  if (state.off_the_record) {
    return SearchChoiceStep::kSkip;
  }
  // Before "offered before": that is recorded as soon as the choice opens.
  if (state.open) {
    return SearchChoiceStep::kWaitForOpenChoice;
  }
  if (state.offered_before || !state.has_engines) {
    return SearchChoiceStep::kSkip;
  }
  if (!state.engines_loaded) {
    return SearchChoiceStep::kWaitForEngines;
  }
  return state.no_search_engine ? SearchChoiceStep::kAsk : SearchChoiceStep::kSkip;
}

}  // namespace lb
