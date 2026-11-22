// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_IMPORTER_IMPORT_APP_ICON_H_
#define LB_BROWSER_IMPORTER_IMPORT_APP_ICON_H_

#include <string>

#include "ui/gfx/image/image_skia.h"

namespace lb {

// The macOS app icon of the installed app with `bundle_id`, sized to `size` DIP square, or an
// empty image when it isn't installed (the import dialog then falls back to a generic icon).
// macOS only.
gfx::ImageSkia AppIconForBundleId(const std::string& bundle_id, int size);

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_IMPORT_APP_ICON_H_
