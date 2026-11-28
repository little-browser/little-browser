// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_VERSION_RELEASE_VERSION_H_
#define LB_BROWSER_VERSION_RELEASE_VERSION_H_

#include <string>
#include <string_view>

namespace lb {

// The release people install, as its tag names it: "<Chromium version>-<revision>", such as
// "157.0.8103.78-3" (docs/design.md §14). A revision of 0 or less means a build that isn't a
// release, which is the Chromium version alone.
std::string FormatReleaseVersion(std::string_view chromium_version, int revision);

// This build's revision: the lb_release_revision GN arg (chromium/lb/BUILD.gn), which build.sh
// sets from LB_RELEASE; 0 when it isn't a release build.
int ReleaseRevision();

// This build's release version, which the About page and chrome://version show. Everything else
// (the user agent, client hints, extensions' update checks) keeps the Chromium version alone.
std::string ReleaseVersion();

}  // namespace lb

#endif  // LB_BROWSER_VERSION_RELEASE_VERSION_H_
