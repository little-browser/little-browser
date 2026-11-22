// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_IMPORTER_ARC_IMPORT_H_
#define LB_BROWSER_IMPORTER_ARC_IMPORT_H_

#include <cstdint>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "lb/browser/importer/chromium_import.h"

namespace lb {

// Imports Arc (docs/ux.md §13). `arc_dir` holds Arc's StorableSidebar.json and its "User Data"
// folder (~/Library/Application Support/Arc). With kImportSpaces, each Arc profile maps to a
// profile here (the default one to the default profile, or to `current_profile` if that was
// deleted; others to a profile of the same name, made if missing), and each Arc space becomes a
// space in its profile (an empty space with the same name and profile is filled instead), with
// Arc's pinned tabs (folders flattened), its unpinned tabs as today tabs and its Favourites at
// the top of each profile's first space. Without it, no profile is made and every Arc profile's
// data goes into `current_profile` (a profile folder name). The other `items` are imported from
// each Arc profile as from a Chrome-family profile; `keychain_*` name Arc's keychain item,
// read once for cookies. `done` runs on the UI thread.
void ImportArc(const base::FilePath& arc_dir,
               uint32_t items,
               const std::string& keychain_service,
               const std::string& keychain_account,
               const std::string& current_profile,
               base::OnceCallback<void(ImportResult)> done);

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_ARC_IMPORT_H_
