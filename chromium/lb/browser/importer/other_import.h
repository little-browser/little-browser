// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_IMPORTER_OTHER_IMPORT_H_
#define LB_BROWSER_IMPORTER_OTHER_IMPORT_H_

#include <cstdint>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "lb/browser/importer/chromium_import.h"
#include "ui/gfx/native_ui_types.h"

class Profile;

namespace lb {

// Imports through Chromium's own importers (docs/design.md §7.6): a Firefox profile, Safari's
// bookmarks or a bookmarks HTML file (`source` as Chromium's importer list describes it, or a
// TYPE_BOOKMARKS_FILE with its path). Their bookmarks keep their tree and merge into the
// existing ones as ours do. Firefox must be closed: Chromium's prompt over `parent` asks. Only
// kImportBookmarks and kImportHistory of `items` apply. `done` runs on the UI thread.
void ImportWithChromiumImporter(Profile* profile,
                                const user_data_importer::SourceProfile& source,
                                uint32_t items,
                                gfx::NativeWindow parent,
                                base::OnceCallback<void(ImportResult)> done);

// Imports several `sources` (Firefox's chosen profiles) into `profile`, one after another, and
// adds up what they brought.
void ImportWithChromiumImporter(Profile* profile,
                                std::vector<user_data_importer::SourceProfile> sources,
                                uint32_t items,
                                gfx::NativeWindow parent,
                                base::OnceCallback<void(ImportResult)> done);

// Imports Safari's export file (File → Export Browsing Data to File, a .zip): its bookmarks (an
// HTML bookmarks file, through Chromium's importer) and its history. Passwords and payment
// cards in it are left alone.
void ImportSafariExport(Profile* profile,
                        const base::FilePath& zip,
                        uint32_t items,
                        base::OnceCallback<void(ImportResult)> done);

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_OTHER_IMPORT_H_
