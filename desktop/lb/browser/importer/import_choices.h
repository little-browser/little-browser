// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_IMPORTER_IMPORT_CHOICES_H_
#define LB_BROWSER_IMPORTER_IMPORT_CHOICES_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lb {

// The import dialog's rules (docs/ux.md §13), unit tested in import_choices_unittest.cc.

// What an import brings along, as a bit set.
enum ImportItem : uint32_t {
  kImportBookmarks = 1 << 0,
  kImportHistory = 1 << 1,
  kImportSearchShortcuts = 1 << 2,
  kImportAddresses = 1 << 3,
  kImportOpenTabs = 1 << 4,
  kImportSpaces = 1 << 5,  // Arc: spaces, profiles, pinned and unpinned tabs, Favourites
  kImportExtensions = 1 << 6,
  kImportCookies = 1 << 7,
};

// Where an import comes from.
enum class ImportSourceKind {
  kChromium,       // a Chrome-family browser's profile
  kArc,            // Arc, all its profiles
  kFirefox,        // a Firefox profile (Chromium's importer)
  kSafari,         // Safari's bookmarks (Chromium's importer; needs Full Disk Access)
  kSafariExport,   // Safari's export file (.zip)
  kBookmarksFile,  // a bookmarks HTML file
};

// The first-run import offer (docs/ux.md §12), asked once on the browser's very first run.
enum class ImportOfferStep {
  kSkip,         // already asked, or not the first run: go on with the other questions
  kWait,         // another window is asking or importing: go on once that is over
  kFindSources,  // look for other browsers, and ask if any is found
};
struct ImportOfferState {
  bool offered = false;      // Local State lb.import_offered
  bool first_run = false;    // the browser's very first run
  bool in_progress = false;  // a window shows the offer, the dialog or its installs
};
ImportOfferStep NextImportOfferStep(const ImportOfferState& state);

// The items a source has (the table in docs/ux.md §13).
uint32_t OfferedImportItems(ImportSourceKind kind);

// The items ticked when a source is picked: all it has, except cookies.
uint32_t DefaultImportItems(ImportSourceKind kind);

// Every item, in the order the dialog lists them.
std::vector<ImportItem> ImportItemsInOrder();

// An item's checkbox label.
std::u16string ImportItemLabel(ImportItem item);

// A source as the dialog lists it: "Chrome — Work", or the browser's name alone when the
// profile has no name.
std::u16string ImportSourceLabel(std::u16string_view browser, std::u16string_view profile);

// The line under "Stay signed in (cookies)": what macOS will ask.
std::u16string CookiesKeychainNote(std::u16string_view keychain_service);

// What an import added.
struct ImportCounts {
  size_t bookmarks = 0;
  size_t history = 0;  // pages of history
  size_t spaces = 0;
  size_t tabs = 0;
  size_t search_shortcuts = 0;
  size_t addresses = 0;
  size_t cookies = 0;

  ImportCounts& operator+=(const ImportCounts& other);
};

// The summary line: "812 bookmarks, 12,400 pages of history, 3 spaces, 41 tabs" (items with
// none left out), or "Nothing new to import".
std::u16string ImportSummary(const ImportCounts& counts);

// An extension's checkbox in the extensions step: its name, and the profile it goes into when
// the import brought extensions for more than one profile ("uBlock Origin — Work").
std::u16string ExtensionLabel(std::u16string_view name,
                              std::u16string_view profile,
                              bool several_profiles);

// The extensions step's line about extensions that can't come along ("2 aren't from the Chrome
// Web Store"); empty for none.
std::u16string NotFromStoreLine(size_t count);

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_IMPORT_CHOICES_H_
