// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/chromium_profile_reader.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/cstring_view.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/time/time.h"
#include "components/sessions/core/command_storage_backend.h"
#include "components/sessions/core/command_storage_manager.h"
#include "components/sessions/core/session_id.h"
#include "components/sessions/core/session_service_commands.h"
#include "components/sessions/core/session_types.h"
#include "lb/browser/importer/cookie_decryptor.h"
#include "lb/browser/importer/import_text.h"
#include "sql/database.h"
#include "sql/statement.h"

namespace lb {

namespace {

// Problem lines (shown in the import summary).
constexpr char kCouldntBeCopied[] = "couldn't be copied";
constexpr char kCouldntBeOpened[] = "couldn't be opened; quit the browser and try again";
constexpr char kKeptEncrypted[] = "this browser keeps them encrypted, so they couldn't be read";

// Chromium's database tags must come from a fixed list (sql/histograms.xml); this one names an
// importer, and Little Browser records no metrics anyway.
constexpr sql::Database::Tag kTag = "FirefoxImporter";

// Copies the SQLite file `name` of `profile_dir`, with its journal files, into `temp_dir`, and
// opens the copy. Returns false when `name` doesn't exist; sets `problem` when it can't be copied
// or opened.
bool OpenCopy(const base::FilePath& profile_dir,
              const base::FilePath& temp_dir,
              const char* name,
              sql::Database& db,
              std::string& problem) {
  const base::FilePath source = profile_dir.AppendASCII(name);
  if (!base::PathExists(source)) {
    return false;
  }
  const base::FilePath copy = temp_dir.AppendASCII(name);
  if (!base::CopyFile(source, copy)) {
    problem = kCouldntBeCopied;
    return true;
  }
  for (const char* suffix : {"-wal", "-journal"}) {
    const base::FilePath from(source.value() + suffix);
    if (base::PathExists(from) && !base::CopyFile(from, base::FilePath(copy.value() + suffix))) {
      problem = kCouldntBeCopied;
      return true;
    }
  }
  // Another browser's file may be damaged or half-written: report it, don't treat it as a bug.
  db.set_error_callback(base::BindRepeating([](int, sql::Statement*) {}));
  if (!db.Open(copy)) {
    problem = kCouldntBeOpened;
  }
  return true;
}

// `column` if `table` has it, else "0", for a SELECT that works across versions.
std::string ColumnOrZero(sql::Database& db, base::cstring_view table, base::cstring_view column) {
  return db.DoesColumnExist(table, column) ? std::string(column) : std::string("0");
}

// When `path` was last changed; null when it doesn't exist.
base::Time LastModified(const base::FilePath& path) {
  base::File::Info info;
  return base::GetFileInfo(path, &info) ? info.last_modified : base::Time();
}

void ReadBookmarks(const base::FilePath& profile_dir, ChromiumProfileData& data) {
  bool kept_encrypted = false;
  for (const auto [name, encrypted_name] :
       {std::pair{"Bookmarks", "EncryptedBookmarks2"},
        std::pair{"AccountBookmarks", "EncryptedAccountBookmarks2"}}) {
    std::string text;
    const base::FilePath path = profile_dir.AppendASCII(name);
    if (PlainCopyIsStale(LastModified(path),
                         LastModified(profile_dir.AppendASCII(encrypted_name)))) {
      kept_encrypted = true;
      continue;
    }
    if (!base::PathExists(path)) {
      continue;
    }
    if (!base::ReadFileToString(path, &text)) {
      data.problems.push_back(std::string("Bookmarks: ") + name + " couldn't be read");
      continue;
    }
    core::BookmarksResult read = core::chromium_bookmarks(ToCoreText(text));
    if (!read.error.empty()) {
      data.problems.push_back(std::string("Bookmarks: ") + name + " " + std::string(read.error));
      continue;
    }
    for (core::FlatBookmark& b : read.bookmarks) {
      data.bookmarks.push_back(std::move(b));
    }
  }
  if (kept_encrypted) {
    data.problems.push_back(std::string("Bookmarks: ") + kKeptEncrypted);
  }
}

void ReadHistory(const base::FilePath& profile_dir,
                 const base::FilePath& temp_dir,
                 base::Time oldest_visit,
                 ChromiumProfileData& data) {
  sql::Database db(kTag);
  std::string problem;
  if (!OpenCopy(profile_dir, temp_dir, "History", db, problem)) {
    return;
  }
  if (problem.empty() && !db.DoesTableExist("urls")) {
    problem = "has no pages";
  }
  if (!problem.empty()) {
    data.problems.push_back("History: " + problem);
    return;
  }
  const std::string sql = "SELECT url, title, visit_count, typed_count, last_visit_time, " +
                          ColumnOrZero(db, "urls", "hidden") + " FROM urls";
  sql::Statement s(db.GetUniqueStatement(sql));
  rust::Vec<core::HistoryRow> rows;
  while (s.Step()) {
    rows.push_back(core::HistoryRow{ToCoreText(s.ColumnString(0)), ToCoreText(s.ColumnString(1)),
                                    s.ColumnInt64(2), s.ColumnInt64(3), s.ColumnInt64(4),
                                    s.ColumnBool(5)});
  }
  if (!s.Succeeded()) {
    data.problems.push_back("History: couldn't be read; quit the browser and try again");
    return;
  }
  data.history = core::history_pages(std::move(rows),
                                     (oldest_visit - base::Time::UnixEpoch()).InMicroseconds());
}

// When the newest file in `dir` was last changed; null when there is none.
base::Time NewestFileTime(const base::FilePath& dir) {
  base::Time newest;
  base::FileEnumerator files(dir, /*recursive=*/false, base::FileEnumerator::FILES);
  for (base::FilePath file = files.Next(); !file.empty(); file = files.Next()) {
    newest = std::max(newest, files.GetInfo().GetLastModifiedTime());
  }
  return newest;
}

void ReadOpenTabs(const base::FilePath& profile_dir,
                  const base::FilePath& temp_dir,
                  ChromiumProfileData& data) {
  const base::FilePath sessions = profile_dir.AppendASCII("Sessions");
  if (PlainCopyIsStale(NewestFileTime(sessions),
                       NewestFileTime(profile_dir.AppendASCII("Sessions_Encrypted")))) {
    data.problems.push_back(std::string("Open tabs: ") + kKeptEncrypted);
    return;
  }
  if (!base::DirectoryExists(sessions)) {
    return;
  }
  // Chromium's session backend tidies its folder (it deletes older files), so it only ever sees the
  // copy.
  const base::FilePath copy = temp_dir.AppendASCII("Sessions");
  bool copied = base::CreateDirectory(copy);
  base::FileEnumerator files(sessions, /*recursive=*/false, base::FileEnumerator::FILES);
  for (base::FilePath file = files.Next(); copied && !file.empty(); file = files.Next()) {
    copied = base::CopyFile(file, copy.Append(file.BaseName()));
  }
  if (!copied) {
    data.problems.push_back(std::string("Open tabs: ") + kCouldntBeCopied);
    return;
  }
  auto backend = base::MakeRefCounted<sessions::CommandStorageBackend>(
      base::SequencedTaskRunner::GetCurrentDefault(), temp_dir,
      sessions::CommandStorageManager::SessionType::kSessionRestore,
      /*encryptor=*/nullptr);
  sessions::CommandStorageBackend::ReadCommandsResult read = backend->ReadLastSessionCommands();

  if (read.commands.empty()) {
    return;
  }
  std::vector<std::unique_ptr<sessions::SessionWindow>> windows;
  SessionID active = SessionID::InvalidValue();
  std::string platform_session_id;
  std::set<SessionID> discarded;
  sessions::RestoreSessionFromCommands(read.commands, &windows, &active, &platform_session_id,
                                       &discarded);
  rust::Vec<core::OpenTab> rows;
  size_t window_index = 0;
  for (const auto& window : windows) {
    if (window->type != sessions::SessionWindow::TYPE_NORMAL) {
      continue;
    }
    std::vector<const sessions::SessionTab*> tabs;
    for (const auto& tab : window->tabs) {
      if (!tab->navigations.empty()) {
        tabs.push_back(tab.get());
      }
    }
    std::ranges::stable_sort(tabs, {}, &sessions::SessionTab::tab_visual_index);
    for (const sessions::SessionTab* tab : tabs) {
      const sessions::SerializedNavigationEntry& nav =
          tab->navigations[tab->normalized_navigation_index()];
      core::OpenTab row{window_index,
                        nav.virtual_url().spec(),
                        base::UTF16ToUTF8(nav.title()),
                        tab->pinned,
                        std::string(),
                        std::string(),
                        0};
      if (tab->group) {
        row.group = tab->group->ToString();
        for (const auto& group : window->tab_groups) {
          if (group->id == *tab->group) {
            row.group_title = base::UTF16ToUTF8(group->visual_data.title());
            row.group_color = static_cast<int32_t>(group->visual_data.color());
          }
        }
      }
      rows.push_back(std::move(row));
    }
    ++window_index;
  }
  data.open_tabs = core::place_open_tabs(std::move(rows));
}

// The file's text, made valid UTF-8; empty when it is missing or can't be read whole.
std::string ReadIfExists(const base::FilePath& path) {
  std::string text;
  if (!base::PathExists(path) || !base::ReadFileToString(path, &text)) {
    return std::string();
  }
  return ToCoreText(text);
}

void ReadExtensions(const base::FilePath& profile_dir,
                    const std::vector<std::string>& installed,
                    ChromiumProfileData& data) {
  rust::Vec<rust::String> installed_ids;
  for (const std::string& id : installed) {
    installed_ids.push_back(rust::String(id));
  }
  core::ExtensionsResult list =
      core::extensions(ReadIfExists(profile_dir.AppendASCII("Preferences")),
                       ReadIfExists(profile_dir.AppendASCII("Secure Preferences")), installed_ids);
  if (!list.error.empty()) {
    data.problems.push_back("Extensions: " + std::string(list.error));
    return;
  }
  for (core::StoreExtension& e : list.store) {
    // A name kept in the extension's messages ("__MSG_appName__"): read it from its files.
    if (e.name.empty() && !e.name_message.empty() && !e.path.empty()) {
      const std::string locale = e.default_locale.empty() ? "en" : std::string(e.default_locale);
      const base::FilePath messages = profile_dir.AppendASCII("Extensions")
                                          .AppendASCII(std::string(e.path))
                                          .AppendASCII("_locales")
                                          .AppendASCII(locale)
                                          .AppendASCII("messages.json");
      e.name = core::localized_message(ReadIfExists(messages), e.name_message);
    }
    if (e.name.empty()) {
      e.name = e.id;
    }
    data.extensions.push_back(std::move(e));
  }
  data.extensions = core::sort_store_extensions(std::move(data.extensions));
  for (const rust::String& id : list.not_from_store) {
    data.extensions_not_from_store.push_back(std::string(id));
  }
}

void ReadCookies(const base::FilePath& profile_dir,
                 const base::FilePath& temp_dir,
                 const ChromiumReadOptions& options,
                 ChromiumProfileData& data) {
  sql::Database db(kTag);
  std::string problem;
  if (!OpenCopy(profile_dir, temp_dir, "Cookies", db, problem)) {
    return;
  }
  if (problem.empty() && !db.DoesTableExist("cookies")) {
    problem = "has no cookies";
  }
  if (!problem.empty()) {
    data.problems.push_back("Cookies: " + problem);
    return;
  }
  int64_t version = 0;
  if (db.DoesTableExist("meta")) {
    sql::Statement meta(db.GetUniqueStatement("SELECT value FROM meta WHERE key = 'version'"));
    if (meta.Step()) {
      version = meta.ColumnInt64(0);
    }
  }
  const std::string sql =
      "SELECT host_key, name, value, encrypted_value, path, creation_utc, "
      "expires_utc, "
      "last_access_utc, is_secure, is_httponly, " +
      ColumnOrZero(db, "cookies", "has_expires") + ", " +
      ColumnOrZero(db, "cookies", "is_persistent") + ", " +
      (db.DoesColumnExist("cookies", "samesite") ? std::string("samesite")
                                                 : std::string("-1")) +  // unspecified
      ", " +
      ColumnOrZero(db, "cookies", "priority") + ", " +
      (db.DoesColumnExist("cookies", "top_frame_site_key") ? std::string("top_frame_site_key")
                                                           : std::string("''")) +
      ", " + ColumnOrZero(db, "cookies", "source_scheme") + ", " +
      (db.DoesColumnExist("cookies", "source_port") ? std::string("source_port")
                                                    : std::string("-1")) +
      " FROM cookies";
  CookieDecryptor decryptor(*options.keychain_password);
  sql::Statement s(db.GetUniqueStatement(sql));
  rust::Vec<core::CookieRow> rows;
  size_t opened = 0;
  size_t unopened = 0;
  while (s.Step()) {
    // A cookie's name and value must stay byte for byte; one that isn't text can't go through
    // lb-core, so it is left behind.
    const std::string host = s.ColumnString(0);
    const std::string name = s.ColumnString(1);
    const std::string path = s.ColumnString(4);
    if (!IsCoreText(host) || !IsCoreText(name) || !IsCoreText(path)) {
      continue;
    }
    std::string value = s.ColumnString(2);
    const std::vector<uint8_t> encrypted = s.ColumnBlobAsVector(3);
    if (!encrypted.empty()) {
      rust::String decrypted;
      std::optional<std::vector<uint8_t>> plain = decryptor.Decrypt(encrypted);
      if (!plain ||
          !core::decrypted_cookie_value(rust::Slice<const uint8_t>(plain->data(), plain->size()),
                                        version, decrypted)) {
        ++unopened;
        continue;
      }
      value = std::string(decrypted);
    } else if (!IsCoreText(value)) {
      continue;
    }
    ++opened;
    rows.push_back(core::CookieRow{
        host, name, value, path, s.ColumnInt64(5), s.ColumnInt64(6), s.ColumnInt64(7),
        s.ColumnBool(8), s.ColumnBool(9), s.ColumnBool(10), s.ColumnBool(11), s.ColumnInt64(12),
        s.ColumnInt64(13), ToCoreText(s.ColumnString(14)), s.ColumnInt64(15), s.ColumnInt64(16)});
  }
  if (!s.Succeeded()) {
    data.problems.push_back("Cookies: couldn't be read");
    return;
  }
  if (unopened && !opened) {
    data.problems.push_back("Cookies: couldn't be decrypted");
    return;
  }
  data.cookies = core::importable_cookies(std::move(rows),
                                          (options.now - base::Time::UnixEpoch()).InMicroseconds());
}

void ReadWebData(const base::FilePath& profile_dir,
                 const base::FilePath& temp_dir,
                 uint32_t items,
                 ChromiumProfileData& data) {
  sql::Database db(kTag);
  std::string problem;
  if (!OpenCopy(profile_dir, temp_dir, "Web Data", db, problem)) {
    return;
  }
  if (!problem.empty()) {
    if (items & kImportSearchShortcuts) {
      data.problems.push_back("Search shortcuts: " + problem);
    }
    if (items & kImportAddresses) {
      data.problems.push_back("Addresses: " + problem);
    }
    return;
  }

  if ((items & kImportSearchShortcuts) && db.DoesTableExist("keywords")) {
    const std::string sql = "SELECT short_name, keyword, url, " +
                            ColumnOrZero(db, "keywords", "safe_for_autoreplace") + ", " +
                            ColumnOrZero(db, "keywords", "prepopulate_id") + ", " +
                            ColumnOrZero(db, "keywords", "created_by_policy") + ", " +
                            ColumnOrZero(db, "keywords", "is_active") + ", " +
                            ColumnOrZero(db, "keywords", "starter_pack_id") + " FROM keywords";
    sql::Statement s(db.GetUniqueStatement(sql));
    rust::Vec<core::KeywordRow> rows;
    while (s.Step()) {
      rows.push_back(core::KeywordRow{ToCoreText(s.ColumnString(0)), ToCoreText(s.ColumnString(1)),
                                      ToCoreText(s.ColumnString(2)), s.ColumnInt64(3),
                                      s.ColumnInt64(4), s.ColumnInt64(5), s.ColumnInt64(6),
                                      s.ColumnInt64(7)});
    }
    if (s.Succeeded()) {
      data.search_shortcuts = core::search_shortcuts(std::move(rows));
    } else {
      data.problems.push_back("Search shortcuts: couldn't be read");
    }
  }

  if (items & kImportAddresses) {
    // Older versions keep local addresses in local_addresses_type_tokens. Newer ones (Web Data 134)
    // keep local and Google-account addresses together in address_type_tokens, told apart by
    // addresses.record_type; only local ones (record type 0, kLocalOrSyncable) come along.
    std::string sql;
    if (db.DoesTableExist("local_addresses_type_tokens")) {
      sql =
          "SELECT guid, type, value FROM local_addresses_type_tokens ORDER BY "
          "rowid";
    } else if (db.DoesTableExist("address_type_tokens")) {
      sql = db.DoesColumnExist("addresses", "record_type")
                ? "SELECT t.guid, t.type, t.value FROM address_type_tokens t "
                  "JOIN addresses a "
                  "ON a.guid = t.guid WHERE a.record_type = 0 ORDER BY t.rowid"
                : "SELECT guid, type, value FROM address_type_tokens ORDER BY "
                  "rowid";
    }
    if (!sql.empty()) {
      sql::Statement s(db.GetUniqueStatement(sql));
      rust::Vec<core::AddressFieldRow> rows;
      while (s.Step()) {
        rows.push_back(core::AddressFieldRow{ToCoreText(s.ColumnString(0)), s.ColumnInt64(1),
                                             ToCoreText(s.ColumnString(2))});
      }
      if (s.Succeeded()) {
        data.addresses = core::addresses(std::move(rows));
      } else {
        data.problems.push_back("Addresses: couldn't be read");
      }
    }
  }
}

}  // namespace

ChromiumProfileData::ChromiumProfileData() = default;
ChromiumProfileData::ChromiumProfileData(ChromiumProfileData&&) = default;
ChromiumProfileData& ChromiumProfileData::operator=(ChromiumProfileData&&) = default;
ChromiumProfileData::~ChromiumProfileData() = default;

ChromiumReadOptions::ChromiumReadOptions() = default;
ChromiumReadOptions::ChromiumReadOptions(const ChromiumReadOptions&) = default;
ChromiumReadOptions& ChromiumReadOptions::operator=(const ChromiumReadOptions&) = default;
ChromiumReadOptions::~ChromiumReadOptions() = default;

bool PlainCopyIsStale(base::Time plain, base::Time encrypted) {
  // Both copies are written in the same save, moments apart.
  constexpr base::TimeDelta kSameSave = base::Minutes(1);
  return !encrypted.is_null() && (plain.is_null() || encrypted - plain > kSameSave);
}

ChromiumProfileData ReadChromiumProfile(const base::FilePath& profile_dir,
                                        uint32_t items,
                                        const ChromiumReadOptions& options) {
  base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
  ChromiumProfileData data;
  if (items & kImportBookmarks) {
    ReadBookmarks(profile_dir, data);
  }
  if (items & kImportExtensions) {
    ReadExtensions(profile_dir, options.installed_extensions, data);
  }
  const bool cookies = (items & kImportCookies) && options.keychain_password;
  if (!(items & (kImportHistory | kImportSearchShortcuts | kImportAddresses | kImportOpenTabs)) &&
      !cookies) {
    return data;
  }
  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir()) {
    data.problems.push_back("No temporary folder to copy the browser's files into");
    return data;
  }
  if (items & kImportHistory) {
    ReadHistory(profile_dir, temp_dir.GetPath(), options.oldest_visit, data);
  }
  if (items & (kImportSearchShortcuts | kImportAddresses)) {
    ReadWebData(profile_dir, temp_dir.GetPath(), items, data);
  }
  if (items & kImportOpenTabs) {
    ReadOpenTabs(profile_dir, temp_dir.GetPath(), data);
  }
  if (cookies) {
    ReadCookies(profile_dir, temp_dir.GetPath(), options, data);
  }
  return data;
}

}  // namespace lb
