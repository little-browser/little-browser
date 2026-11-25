// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_IMPORTER_IMPORT_DIALOG_H_
#define LB_BROWSER_UI_VIEWS_IMPORTER_IMPORT_DIALOG_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"

class BrowserWindowInterface;
class PrefRegistrySimple;

namespace lb {

// Whether the first-run import offer was made (Local State, browser-wide).
inline constexpr char kImportOfferedPref[] = "lb.import_offered";
void RegisterImportLocalStatePrefs(PrefRegistrySimple* registry);

// The import dialog (docs/ux.md §13) over `browser`: pick a browser (or a file), then its
// profiles or spaces and what comes along, import, see the summary, then install the browser's
// extensions. `on_closed` runs when it has closed and its extensions are installed. If it is
// already open, it comes to the front, and `on_closed` waits for it.
void ShowImportDialog(BrowserWindowInterface* browser, base::OnceClosure on_closed = {});

// The open import dialog as the import-dialog demo sees it.
struct ImportDialogState {
  ImportDialogState();
  ImportDialogState(const ImportDialogState&);
  ImportDialogState& operator=(const ImportDialogState&);
  ~ImportDialogState();

  // 1: pick a browser; 2: its profiles or spaces and what comes along; 3: the summary and
  // extensions; else 0
  int step = 0;
  // Step 1: its rows (browsers, then the two files). Step 2: the profiles or spaces listed.
  std::vector<std::u16string> rows;
  // Step 2: the items (ImportItem bits) ticked, and those whose checkbox can be ticked.
  uint32_t ticked_items = 0;
  uint32_t tickable_items = 0;
  // Step 3: the extensions list as shown, profile headings as "‹name›:" followed by their
  // extensions' names, " (unticked)" after one that starts unticked.
  std::vector<std::u16string> extensions;
  // The items (ImportItem bits) reported imported so far this import, for the progress display.
  uint32_t imported_seen = 0;
  std::u16string title;
  std::u16string cancel_label;  // "Cancel", or "Not now" when it opened by itself on first run
  // The dialog's height on screen, and whether its body scrolls (it is taller than it shows).
  int dialog_height = 0;
  bool scrolls = false;
  bool can_accept = false;  // Next or Import can be pressed
};
std::optional<ImportDialogState> GetImportDialogStateForTesting();
// In step 1, picks the row labelled `row` and presses Next; false if there is no such row.
bool ImportDialogNextForTesting(const std::u16string& row);
// In step 2, presses Import; false if it can't be pressed.
bool ImportDialogImportForTesting();
// Closes the dialog as its Cancel (or Not now) does; false if none is open.
bool ImportDialogCancelForTesting();
// In step 2, presses Back.
bool ImportDialogBackForTesting();

// First run (docs/ux.md §12): on the browser's very first run, when another browser's data is on
// the Mac, opens the import dialog itself before the other questions, with an introduction and
// "Not now". `then` runs once it closes (and its extensions are installed), or right away when
// there is nothing to ask.
void MaybeShowImportOffer(BrowserWindowInterface* browser, base::OnceClosure then);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_IMPORTER_IMPORT_DIALOG_H_
