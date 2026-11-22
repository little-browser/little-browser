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
//   import-arc      imports both spaces of its Arc: the empty "Personal" space is filled
//                   (Favourites first, then the pinned tabs with the folder a pinned group, then
//                   today tabs), "Work" becomes a space in a new "Client" profile, and each Arc
//                   profile's bookmarks go to its matching profile.
//   import-dialog   checks the browsers found are Arc, Chrome and Edge, then opens the dialog
//                   with the app menu's command (Settings' row and the command bar run it too)
//                   and steps through it: the browsers and two files; Chrome's single profile
//                   isn't listed; Back; Arc's spaces ("Work — Client"); Edge's two profiles.
//                   Import can be pressed each time. Then imports Edge: the extensions list must
//                   group its extensions under each profile's name, one turned off in Edge
//                   unticked, and its fifteen rows must scroll inside a dialog of bounded height
//                   (with --lb-ui-snapshot-dialog=<png> that list is captured).
//   import-choices  opens the dialog and steps to Chrome's "what comes along" step, then holds it
//                   (--lb-ui-snapshot-dialog=<png> captures the item rows, each with its icon).
//   import-progress imports Edge through the dialog and checks the progress step reports every
//                   item (each spinner turning into a check); --lb-ui-snapshot-dialog=<png>
//                   captures that step.
//   import-first-run run without --no-first-run: the dialog must open by itself on its first
//                   step, with the first-run title, the introduction and "Not now"; the next
//                   question and a second Import wait for it and go on once it closes.
//   import-arc-unreadable  with a copy whose Arc sidebar file is broken: Arc is listed with its
//                   profiles instead, as a Chrome-family browser, the problem named.
//   import-edge     imports one of a two-profile Edge's profiles ("Profile 10", "Studio"; this
//                   one has no "Default"), whose bookmarks and session files each have an
//                   encrypted copy saved just after them: it becomes a "Studio" space with its
//                   open tabs in a new "Studio" profile (the pinned one pinned, the group a
//                   group, collapsed), its bookmark goes there, and the other profile's bookmark
//                   doesn't come.
//   import-files    imports its bookmarks.html through Chromium's importer (it must merge into
//                   the bar, not an "Imported" folder), then a Safari export .zip made from it
//                   with a history page (the bookmarks are already there; the page arrives),
//                   then two bookmarks files in one import, one after another (both must come).
//   import          --lb-import-source=<a real Chrome-family profile folder>: imports it (all
//                   but cookies, so the keychain isn't asked) and logs only how many of each
//                   item came along, never what they are.
// Returns false for any other mode.
bool MaybeRunImportDemo(BrowserWindowInterface* browser, const std::string& mode);

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_IMPORT_DEMO_H_
