// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_IMPORTER_BROWSER_IMPORT_H_
#define LB_BROWSER_IMPORTER_BROWSER_IMPORT_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "lb/browser/importer/chromium_import.h"
#include "lb/browser/importer/import_choices.h"

namespace lb {

// What to import from a Chrome-family browser or from Arc (docs/ux.md §13).
struct BrowserImportRequest {
  BrowserImportRequest();
  BrowserImportRequest(const BrowserImportRequest&);
  BrowserImportRequest& operator=(const BrowserImportRequest&);
  ~BrowserImportRequest();

  ImportSourceKind kind = ImportSourceKind::kChromium;  // kChromium or kArc
  // Chrome-family: its User Data folder. Arc: its folder, holding StorableSidebar.json and
  // "User Data".
  base::FilePath dir;
  // Chrome-family: the chosen profiles' folders in `dir` (empty: `dir` itself). Arc: the chosen
  // spaces' ids.
  std::vector<std::string> chosen;
  uint32_t items = 0;  // ImportItem bits
  // The browser's keychain item, read once for all its profiles (kImportCookies).
  std::string keychain_service;
  std::string keychain_account;
  // The profile the import was started from (a profile folder name here).
  std::string current_profile;
  // The browser's name, for a space whose profile has none ("Edge").
  std::string browser;
  // Run on the UI thread once each requested item (an ImportItem bit) has come over: written by
  // every source profile that imports it, or found to have nothing to write, for a progress
  // display. Spaces, profiles and tabs report together as kImportSpaces, once the tabs are in.
  base::RepeatingCallback<void(uint32_t)> on_item;
};

// Imports a Chrome-family browser's chosen profiles, or Arc's chosen spaces.
//
// With kImportSpaces each source profile maps to a profile here: the browser's default one to
// the default profile (or to `current_profile` if that was deleted), each other one to the
// profile with the same name, made if missing. Arc: each chosen space becomes a space in its
// profile, with Arc's pinned tabs (its pinned folders as pinned groups), its unpinned tabs as
// today tabs and its Favourites at the top of each profile's first chosen space. Chrome-family:
// each chosen profile becomes a space named after it, holding its open tabs. An empty space with
// the same name and profile is filled instead of making one.
//
// Without kImportSpaces no profile or space is made, no tabs come, and everything goes into
// `current_profile`.
//
// The other items come from each source profile (for Arc, the profiles of the chosen spaces)
// into its profile, one profile after another. `done` runs on the UI thread.
void ImportBrowser(const BrowserImportRequest& request,
                   base::OnceCallback<void(ImportResult)> done);

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_BROWSER_IMPORT_H_
