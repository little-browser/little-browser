// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_IMPORTER_IMPORT_DEMO_H_
#define LB_BROWSER_IMPORTER_IMPORT_DEMO_H_

#include <string>

class BrowserWindowInterface;

namespace lb {

// Import checks in a running browser, each logging "lb demo: <mode> PASS ..." or "... FAIL ..."
// (desktop/lb/README.md). Run them on a fresh profile with --use-mock-keychain. The made-up
// sources are in desktop/lb/test/data/import/home, passed as --lb-import-home=<folder>.
//   import-fixture  imports its Chrome profile (SQLite files built from the .sql next to them;
//                   a session file and cookies encrypted with the mock keychain's password made
//                   in code) and checks the bookmarks in their folders, history, search
//                   shortcut, address, open tabs (pinned, grouped) and cookie arrived and its
//                   store extension is offered; then
//                   imports it again: nothing may be added twice.
//   import-arc      imports its Arc: the empty "Personal" space is filled (Favourites first,
//                   then the pinned tabs with the folder a pinned group, then today tabs), "Work"
//                   becomes a space in a new "Client" profile, and each Arc profile's bookmarks
//                   go to its matching profile.
//   import-dialog   checks the dialog lists Arc, "Chrome — Personal" and "Dia — Studio", then
//                   opens it with the app menu's command (Settings' row and the command bar run
//                   it too) and checks Import can be pressed (with --lb-ui-snapshot-dialog=<png>
//                   it is captured).
//   import-dia      imports its Dia profile ("Profile 10", no "Default"), whose bookmarks and
//                   session files each have an encrypted copy saved just after them, and checks
//                   the plain ones are read: the bookmark and the open tabs arrive, no problems.
//   import-files    imports its bookmarks.html through Chromium's importer (it must merge into
//                   the bar, not an "Imported" folder), then a Safari export .zip made from it
//                   with a history page (the bookmarks are already there; the page arrives).
//   import          --lb-import-source=<a real Chrome-family profile folder>: imports it (all
//                   but cookies, so the keychain isn't asked) and logs only how many of each
//                   item came along, never what they are.
// Returns false for any other mode.
bool MaybeRunImportDemo(BrowserWindowInterface* browser, const std::string& mode);

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_IMPORT_DEMO_H_
