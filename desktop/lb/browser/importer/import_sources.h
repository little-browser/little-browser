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

// Something to import from (docs/ux.md §13).
struct ImportSource {
  ImportSource();
  ImportSource(const ImportSource&);
  ImportSource& operator=(const ImportSource&);
  ~ImportSource();

  ImportSourceKind kind = ImportSourceKind::kChromium;
  std::u16string label;    // "Chrome — Work"
  std::u16string browser;  // "Chrome"
  std::string bundle_id;   // its macOS bundle id, to put the default browser first
  // Chrome-family: the profile folder. Arc: its folder in Application Support.
  base::FilePath path;
  std::string keychain_service;
  std::string keychain_account;
  // Firefox and Safari: as Chromium's importer list found them.
  user_data_importer::SourceProfile chromium_source;
};

// Finds what can be imported on this Mac: each Chrome-family browser's profiles, Arc, Firefox's
// profiles and Safari, the default browser first. `done` runs on the UI thread. The two file
// choices (a Safari export file, a bookmarks file) aren't in the list.
void FindImportSources(base::OnceCallback<void(std::vector<ImportSource>)> done);

// The Application Support folder sources are read from; a demo can point it elsewhere with
// --lb-import-home=<folder>.
base::FilePath ImportSupportFolder();

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_IMPORT_SOURCES_H_
