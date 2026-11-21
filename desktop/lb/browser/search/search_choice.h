// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_SEARCH_SEARCH_CHOICE_H_
#define LB_BROWSER_SEARCH_SEARCH_CHOICE_H_

namespace lb {

// What decides the first-run search engine choice (docs/ux.md section 12) when a window opens.
struct SearchChoiceState {
  bool off_the_record = false;
  // The choice is open in some window of this profile.
  bool open = false;
  // It was offered to this profile before.
  bool offered_before = false;
  // The profile has search engines at all.
  bool has_engines = true;
  // They have been loaded from disk.
  bool engines_loaded = true;
  // The default is still ungoogled-chromium's "No Search" placeholder.
  bool no_search_engine = true;
};

enum class SearchChoiceStep {
  // Show the choice; the next first-run question waits until it closes.
  kAsk,
  // Don't show it; go on to the next question now.
  kSkip,
  // It is open in another window: the next question waits until that one closes.
  kWaitForOpenChoice,
  // Load the search engines and decide again.
  kWaitForEngines,
};

SearchChoiceStep NextSearchChoiceStep(const SearchChoiceState& state);

}  // namespace lb

#endif  // LB_BROWSER_SEARCH_SEARCH_CHOICE_H_
