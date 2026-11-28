// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/version/release_version.h"

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/version_info/version_info.h"
#include "build/buildflag.h"
#include "lb/browser/version/release_buildflags.h"  // only this file, so a revision recompiles one

namespace lb {

std::string FormatReleaseVersion(std::string_view chromium_version, int revision) {
  if (revision <= 0) {
    return std::string(chromium_version);
  }
  return base::StrCat({chromium_version, "-", base::NumberToString(revision)});
}

int ReleaseRevision() {
  return BUILDFLAG(LB_RELEASE_REVISION);
}

std::string ReleaseVersion() {
  return FormatReleaseVersion(version_info::GetVersionNumber(), ReleaseRevision());
}

}  // namespace lb
