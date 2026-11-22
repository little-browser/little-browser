// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_IMPORTER_IMPORT_SOURCES_H_
#define LB_BROWSER_IMPORTER_IMPORT_SOURCES_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "lb/browser/importer/import_choices.h"

namespace lb {

// A profile, or an Arc space, that the dialog's second step lists for a browser.
struct ImportChoice {
  ImportChoice();
  ImportChoice(const ImportChoice&);
  ImportChoice& operator=(const ImportChoice&);
  ~ImportChoice();

  // Chrome-family: the profile's folder in the browser's User Data (empty: User Data itself).
  // Arc: the space's id.
  std::string id;
  std::u16string
      label;  // "Work", or for an Arc space outside Arc's default profile "Work — Client"
  // Firefox: the profile as Chromium's importer list found it.
  user_data_importer::SourceProfile chromium_source;
};

// A browser to import from (docs/ux.md §13).
struct ImportSource {
  ImportSource();
  ImportSource(const ImportSource&);
  ImportSource& operator=(const ImportSource&);
  ~ImportSource();

  ImportSourceKind kind = ImportSourceKind::kChromium;
  std::u16string browser;  // "Chrome", as the dialog lists it
  std::string bundle_id;   // its macOS bundle id, to put the default browser first
  // Chrome-family: its User Data folder. Arc: its folder in Application Support.
  base::FilePath path;
  std::string keychain_service;
  std::string keychain_account;
  // Its profiles (Chrome-family, Firefox) or spaces (Arc), in the browser's order.
  std::vector<ImportChoice> choices;
  // Arc: why its spaces couldn't be listed; empty when they could.
  std::string problem;
  // Safari: as Chromium's importer list found it.
  user_data_importer::SourceProfile chromium_source;
};

// Finds the browsers that can be imported from on this Mac, each once with its profiles or
// spaces: the Chrome-family browsers, Arc, Firefox and Safari, the default browser first. `done`
// runs on the UI thread. The two file choices (a Safari export file, a bookmarks file) aren't in
// the list.
void FindImportSources(base::OnceCallback<void(std::vector<ImportSource>)> done);

// The Application Support folder sources are read from; a demo can point it elsewhere with
// --lb-import-home=<folder>.
base::FilePath ImportSupportFolder();

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_IMPORT_SOURCES_H_
