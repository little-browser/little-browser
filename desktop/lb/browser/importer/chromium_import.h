// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_IMPORTER_CHROMIUM_IMPORT_H_
#define LB_BROWSER_IMPORTER_CHROMIUM_IMPORT_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/url_row.h"
#include "lb/browser/importer/import_choices.h"
#include "lb/core_bridge/lib.rs.h"

class Profile;

namespace lb {

// A Chrome Web Store extension the import offers to install.
struct ExtensionToInstall {
  ExtensionToInstall();
  ExtensionToInstall(const ExtensionToInstall&);
  ExtensionToInstall& operator=(const ExtensionToInstall&);
  ~ExtensionToInstall();

  std::string id;
  std::u16string name;
  // The profile it was imported for, and that profile's name.
  base::WeakPtr<Profile> profile;
  std::u16string profile_name;
  // Turned off in the source browser: offered unticked.
  bool enabled = true;
};

// What an import did, for its summary (docs/ux.md §13).
struct ImportResult {
  ImportResult();
  ImportResult(const ImportResult&);
  ImportResult& operator=(const ImportResult&);
  ~ImportResult();

  ImportResult& operator+=(const ImportResult& other);

  ImportCounts counts;
  // One line per item that couldn't be read or written ("History: ...").
  std::vector<std::string> problems;
  // Extensions to offer (the dialog's last step), and those that couldn't come along, each once
  // per profile ("<profile folder>/<extension id>"), so the summary counts each once.
  std::vector<ExtensionToInstall> extensions;
  std::set<std::string> extensions_not_from_store;
};

// What to import from one Chrome-family profile.
struct ChromiumImportRequest {
  ChromiumImportRequest();
  ChromiumImportRequest(const ChromiumImportRequest&);
  ChromiumImportRequest& operator=(const ChromiumImportRequest&);
  ~ChromiumImportRequest();

  base::FilePath profile_dir;
  uint32_t items = 0;  // ImportItem bits
  // kImportCookies: the browser's "<name> Safe Storage" keychain item, or its password when the
  // caller has already read it (once for several profiles of one browser).
  std::string keychain_service;
  std::string keychain_account;
  std::optional<std::string> keychain_password;
  // kImportOpenTabs: the space the tabs join.
  std::string space_id;
  // Run after each requested item (an ImportItem bit) has been written into the profile, or turned
  // out to have nothing to write, for a progress display; kImportOpenTabs once the open tabs have
  // joined their space.
  base::RepeatingCallback<void(uint32_t)> on_item_written;
};

// Imports a Chrome-family profile into `profile`: its files are read on a background sequence
// (the keychain too, for cookies; macOS asks the user), then written through the profile's
// bookmarks, history, search engines, addresses, cookies and the space's tabs once each has
// loaded. Bookmarks keep the source's tree and merge into the existing one; history adds only
// pages that are new or newer; search shortcuts and addresses already here aren't added again.
// Pages older than history keeps don't come along, and neither do addresses while saving them
// is turned off (that is a problem in the result). Extensions are only listed in the result.
// `done` runs on the UI thread, also when the profile goes away first (with that as a problem).
void ImportChromiumProfile(Profile* profile,
                           const ChromiumImportRequest& request,
                           base::OnceCallback<void(ImportResult)> done);

// Adds history pages to `profile`'s history: those it doesn't have, or has with an older visit
// (so importing twice adds nothing). `done` gets how many pages that was, on the UI thread, and
// always runs: 0 when history couldn't take them (its database didn't open).
void AddHistoryPages(Profile* profile,
                     history::URLRows rows,
                     history::VisitSource source,
                     base::OnceCallback<void(size_t)> done);

// Adds `bookmarks` (lb-core's flat list) to `profile`'s bookmarks, which must have loaded,
// merged into the existing ones; returns how many links were added.
size_t MergeIntoBookmarks(Profile* profile, const rust::Vec<core::FlatBookmark>& bookmarks);

// The password of a keychain item, such as "Chrome Safe Storage" (macOS asks the user first),
// or null when there is none or the user declines. For a demo's made-up folder
// (ReadsMadeUpImportKeychain) it reads a fake keychain instead, which gives every item the same
// password. Blocking.
std::optional<std::string> ReadImportKeychainPassword(const std::string& service,
                                                      const std::string& account);

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_CHROMIUM_IMPORT_H_
