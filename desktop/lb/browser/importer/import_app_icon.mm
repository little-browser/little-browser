// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#import <Cocoa/Cocoa.h>

#include "lb/browser/importer/import_app_icon.h"

#include "base/strings/sys_string_conversions.h"
#include "ui/gfx/image/image_skia_util_mac.h"

namespace lb {

gfx::ImageSkia AppIconForBundleId(const std::string& bundle_id, int size) {
  if (bundle_id.empty()) {
    return gfx::ImageSkia();
  }
  NSURL* app = [NSWorkspace.sharedWorkspace
      URLForApplicationWithBundleIdentifier:base::SysUTF8ToNSString(bundle_id)];
  if (!app) {
    return gfx::ImageSkia();
  }
  NSImage* icon = [NSWorkspace.sharedWorkspace iconForFile:app.path];
  if (!icon) {
    return gfx::ImageSkia();
  }
  return gfx::ImageSkiaFromResizedNSImage(icon, NSMakeSize(size, size));
}

}  // namespace lb
