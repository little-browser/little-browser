// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_IMPORTER_EXTENSION_INSTALL_H_
#define LB_BROWSER_IMPORTER_EXTENSION_INSTALL_H_

#include <vector>

#include "base/functional/callback_forward.h"
#include "lb/browser/importer/chromium_import.h"

namespace lb {

// Installs the imported extensions the user ticked from the Chrome Web Store, one after another,
// each into its profile, without Chromium's install prompt: ticking them was the consent
// (docs/ux.md §13). One that fails, or whose profile went away or is shutting down, doesn't stop
// the others. `then` runs once the last one is over.
void InstallImportedExtensions(std::vector<ExtensionToInstall> extensions, base::OnceClosure then);

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_EXTENSION_INSTALL_H_
