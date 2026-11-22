// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_IMPORTER_CHROMIUM_PROFILE_READER_H_
#define LB_BROWSER_IMPORTER_CHROMIUM_PROFILE_READER_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/time/time.h"
#include "lb/browser/importer/import_choices.h"
#include "lb/core_bridge/lib.rs.h"

namespace lb {

// How to read a profile.
struct ChromiumReadOptions {
  ChromiumReadOptions();
  ChromiumReadOptions(const ChromiumReadOptions&);
  ChromiumReadOptions& operator=(const ChromiumReadOptions&);
  ~ChromiumReadOptions();

  // Pages last visited before this don't come along (history here keeps nothing older).
  base::Time oldest_visit;
  // Cookies that have expired by then don't come along.
  base::Time now;
  // Extensions already installed here, which aren't offered again.
  std::vector<std::string> installed_extensions;
  // The browser's "<name> Safe Storage" keychain password: cookies are read only with it.
  std::optional<std::string> keychain_password;
};

// What was read from a Chrome-family profile, already through lb-core's rules (which pages, which
// search engines, ...). An item that couldn't be read has no data and its reason in `problems`; the
// other items still come along.
struct ChromiumProfileData {
  ChromiumProfileData();
  ChromiumProfileData(ChromiumProfileData&&);
  ChromiumProfileData& operator=(ChromiumProfileData&&);
  ~ChromiumProfileData();

  // `Bookmarks` and then `AccountBookmarks`, as a flat list (lb::core::FlatBookmark).
  rust::Vec<core::FlatBookmark> bookmarks;
  rust::Vec<core::HistoryPage> history;
  rust::Vec<core::SearchShortcut> search_shortcuts;
  rust::Vec<core::Address> addresses;
  // In the order they join the space (lb::core::place_open_tabs).
  rust::Vec<core::OpenTab> open_tabs;
  // Chrome Web Store extensions not installed here, with their names; and how many others the
  // profile has, which can't come along.
  rust::Vec<core::StoreExtension> extensions;
  size_t extensions_not_from_store = 0;
  rust::Vec<core::Cookie> cookies;
  // "History: ...", one line per item that couldn't be read.
  std::vector<std::string> problems;
};

// Chromium is moving profile files to encrypted copies (`Sessions_Encrypted`,
// `EncryptedBookmarks2`), which need the browser's key: a browser first writes both at the same
// moment, then only the encrypted one. True when the plain file, last changed at `plain` (null when
// there is none), is stale next to an encrypted copy last changed at `encrypted` (null when there
// is none): the copy is more than a minute newer.
bool PlainCopyIsStale(base::Time plain, base::Time encrypted);

// Reads `items` (ImportItem bits, import_choices.h) from the Chrome-family profile folder
// `profile_dir`. Its SQLite files and session files are copied into a temporary folder first
// (SQLite files with their `-wal` and `-journal` files) and the copies are read, so the other
// browser can stay open and its files stay as they are. A missing file means nothing to import, not
// a problem. Blocking: run it on a MayBlock single-threaded runner (Chromium's session backend,
// which reads open tabs, needs one).
ChromiumProfileData ReadChromiumProfile(const base::FilePath& profile_dir,
                                        uint32_t items,
                                        const ChromiumReadOptions& options);

}  // namespace lb

#endif  // LB_BROWSER_IMPORTER_CHROMIUM_PROFILE_READER_H_
