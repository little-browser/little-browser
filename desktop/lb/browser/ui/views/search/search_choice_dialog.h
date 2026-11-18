// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_SEARCH_SEARCH_CHOICE_DIALOG_H_
#define LB_BROWSER_UI_VIEWS_SEARCH_SEARCH_CHOICE_DIALOG_H_

#include "base/functional/callback.h"

class BrowserWindowInterface;

namespace lb {

// Profile pref: the first-run search engine choice was offered (registered with the sidebar's
// prefs).
inline constexpr char kSearchChoiceShownPref[] = "lb.search_choice_shown";

// No default search engine: the user picks one on first run. ungoogled-chromium ships a
// "No Search" placeholder as the default. Shows the choice once per profile while that
// placeholder is still the default.
void MaybeShowSearchChoice(BrowserWindowInterface* browser);

// Shows the choice now (a search with no engine set). `on_chosen` runs after the user picks an
// engine (not on "Not now"), e.g. to run the search they typed. If the dialog is already open
// it is brought to the front instead.
void ShowSearchChoice(BrowserWindowInterface* browser, base::OnceClosure on_chosen);

// Closes the dialog as "Not now" does (the --lb-sidebar-demo=search-again check).
void CloseSearchChoiceForTesting();

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_SEARCH_SEARCH_CHOICE_DIALOG_H_
