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
  // A Chrome-family profile's open tabs, into a space. Not a choice in the dialog: kImportSpaces
  // brings them.
  kImportOpenTabs = 1 << 4,
  // Spaces, profiles and tabs: Arc's spaces (pinned and unpinned tabs, Favourites), or a
  // Chrome-family profile as a space with its open tabs; each in its matching profile.
  kImportSpaces = 1 << 5,
  kImportExtensions = 1 << 6,
  kImportCookies = 1 << 7,
};

// Where an import comes from.
enum class ImportSourceKind {
  kChromium,       // a Chrome-family browser, its profiles chosen
  kArc,            // Arc, its spaces chosen
  kFirefox,        // Firefox, its profiles chosen (Chromium's importer)
  kSafari,         // Safari's bookmarks (Chromium's importer; needs Full Disk Access)
  kSafariExport,   // Safari's export file (.zip)
  kBookmarksFile,  // a bookmarks HTML file
};

// The first-run import offer (docs/ux.md §12): the import dialog opens by itself once, on the
// browser's very first run, when another browser's data is on the Mac.
enum class ImportOfferStep {
  kSkip,         // already offered, or not the first run: go on with the other questions
  kWait,         // another window is looking for browsers or importing: go on once that is over
  kFindSources,  // look for other browsers, and open the dialog if any is found
};
struct ImportOfferState {
  bool offered = false;      // Local State lb.import_offered
  bool first_run = false;    // the browser's very first run
  bool in_progress = false;  // a window is looking for browsers, or shows the dialog or installs
};
ImportOfferStep NextImportOfferStep(const ImportOfferState& state);

// The items a source has (the table in docs/ux.md §13).
uint32_t OfferedImportItems(ImportSourceKind kind);

// The items ticked when a source is picked: all it has, except cookies.
uint32_t DefaultImportItems(ImportSourceKind kind);

// Every item the dialog lists, in its order.
std::vector<ImportItem> ImportItemsInOrder();

// An item's checkbox label.
std::u16string ImportItemLabel(ImportItem item);

// The heading over a browser's profiles or spaces in the dialog's second step: "Profiles",
// "Spaces" (Arc), or empty for a source that has neither.
std::u16string ImportChoicesHeading(ImportSourceKind kind);

// Whether the dialog lists the profiles or spaces: only when there is more than one to pick
// from (one is simply taken).
bool ShowsImportChoices(size_t choices);

// Whether Import can be pressed: something to bring is ticked and, for a browser with profiles
// or spaces, at least one of its `choices` is picked (`picked` of them).
bool CanImport(ImportSourceKind kind, size_t choices, size_t picked, uint32_t items);

// A name and the profile it belongs to: "Work — Client", or the name alone when `profile` is
// empty. `name` empty is "Untitled".
std::u16string NameWithProfile(std::u16string_view name, std::u16string_view profile);

// How the dialog lists a source profile: the name the browser shows for it, else its folder, else
// (a profile kept in the browser's data folder itself) the browser's name.
std::u16string ProfileLabel(std::u16string_view name,
                            std::string_view folder,
                            std::u16string_view browser);

// A problem line of an import from several source profiles names the profile it came from
// ("Work: History: ..."); from one, or from a profile without a name, it is the problem alone.
std::string ProfileProblem(std::string_view problem,
                           std::string_view profile,
                           bool several_profiles);

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

// The extensions step's list, grouped by the profile each extension goes into: one group per
// profile in the order they first come in `profiles` (each extension's profile, by something
// unique to it such as its folder, since two profiles may share a name), with the indices of its
// extensions in their order. When all go into one profile there is one group with an empty
// `profile`, and the list shows no heading; else the dialog heads each group with the profile's
// name.
struct ExtensionGroup {
  ExtensionGroup();
  ExtensionGroup(const ExtensionGroup&);
  ExtensionGroup& operator=(const ExtensionGroup&);
  ~ExtensionGroup();
  bool operator==(const ExtensionGroup&) const;

  std::u16string profile;
  std::vector<size_t> extensions;
};
std::vector<ExtensionGroup> GroupExtensionsByProfile(const std::vector<std::u16string>& profiles);

// The extensions step's line about extensions that can't come along ("2 aren't from the Chrome
// Web Store"); empty for none.
std::u16string NotFromStoreLine(size_t count);

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_IMPORT_CHOICES_H_
