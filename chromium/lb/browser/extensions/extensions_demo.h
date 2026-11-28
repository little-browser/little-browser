// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_EXTENSIONS_EXTENSIONS_DEMO_H_
#define LB_BROWSER_EXTENSIONS_EXTENSIONS_DEMO_H_

#include <string>

class BrowserWindowInterface;

namespace lb {

// --lb-sidebar-demo=<mode>: checks extension support in a running browser and logs, once,
// "lb demo: <mode> PASS ..." or "lb demo: <mode> FAIL ..." (chromium/lb/README.md).
//   web-store         opens the extension's Chrome Web Store page, presses its "Add to" button and
//                     accepts the install prompt; the extension must install from the store, and
//                     the page must get an empty referrer chain.
//   extension-update  run on the profile web-store left: checks the store for an update to that
//                     extension as the extension updater does, and records the request; it must
//                     go to the store, find the update and carry no pings and no hardware fields.
//   update-manifest   fetches the store's update manifest for the extension and parses it as
//                     ExtensionDownloader does (force-installed, external, self-hosted ones).
//   mv2-off           loads an unpacked Manifest V2 and a Manifest V3 extension; only the MV3 one
//                     may run.
//   store-links       on a profile with no extensions: checks that the app menu has "Visit Chrome
//                     Web Store", then follows it and the store links on chrome://extensions;
//                     each must load a store page.
//   password-manager  on a fresh profile: the first-run password manager choice must come after
//                     the search engine choice; picking Proton Pass and accepting the install
//                     prompt must install it from the store.
//   import-extensions on a profile without the extension: installs it as an import does, with
//                     any install prompt cancelled; it must install from the store all the same
//                     (no prompt), after one with no profile and one the store doesn't have.
// The extension is uBlock Origin Lite unless --lb-demo-extension=<id> names another. Returns false
// for any other mode.
bool MaybeRunExtensionsDemo(BrowserWindowInterface* browser, const std::string& mode);

}  // namespace lb

#endif  // LB_BROWSER_EXTENSIONS_EXTENSIONS_DEMO_H_
