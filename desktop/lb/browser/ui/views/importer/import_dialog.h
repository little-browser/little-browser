// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_IMPORTER_IMPORT_DIALOG_H_
#define LB_BROWSER_UI_VIEWS_IMPORTER_IMPORT_DIALOG_H_

#include "base/functional/callback.h"

class BrowserWindowInterface;
class PrefRegistrySimple;

namespace lb {

// Whether the first-run import offer was made (Local State, browser-wide).
inline constexpr char kImportOfferedPref[] = "lb.import_offered";
void RegisterImportLocalStatePrefs(PrefRegistrySimple* registry);

// The import dialog (docs/ux.md §13) over `browser`: pick a source and what comes along, import,
// see the summary, then install the source's extensions. `on_closed` runs when it closes. If it
// is already open, it comes to the front.
void ShowImportDialog(BrowserWindowInterface* browser, base::OnceClosure on_closed = {});

// Whether the open import dialog's Import button can be pressed (the import-dialog demo).
bool ImportDialogCanImportForTesting();

// First run (docs/ux.md §12): on the browser's very first run, when another browser's data is on
// the Mac, asks "Bring your tabs and bookmarks from ‹browser›?" before the other questions;
// Import… opens the dialog with that browser picked. `then` runs once that is over, or right
// away when there is nothing to ask.
void MaybeShowImportOffer(BrowserWindowInterface* browser, base::OnceClosure then);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_IMPORTER_IMPORT_DIALOG_H_
