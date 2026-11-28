// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/chromium_profile_reader.h"

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/cstring_view.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "components/sessions/core/command_storage_backend.h"
#include "components/sessions/core/command_storage_manager.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "components/sessions/core/session_id.h"
#include "components/sessions/core/session_service_commands.h"
#include "components/sessions/core/session_types.h"
#include "components/tab_groups/tab_group_id.h"
#include "lb/browser/importer/cookie_decryptor.h"
#include "sql/database.h"
#include "sql/test/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace lb {
namespace {

constexpr uint32_t kAll =
    kImportBookmarks | kImportHistory | kImportSearchShortcuts | kImportAddresses;

ChromiumReadOptions Options(base::Time oldest_visit = base::Time::UnixEpoch()) {
  ChromiumReadOptions options;
  options.oldest_visit = oldest_visit;
  options.now = base::Time::UnixEpoch() + base::Microseconds(1704067200000000);  // 2024-01-01
  return options;
}

// A made-up Chrome-family profile folder.
class ChromiumProfileReaderTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(dir_.CreateUniqueTempDir()); }

  const base::FilePath& profile() const { return dir_.GetPath(); }

  void Write(const char* name, const std::string& text) {
    ASSERT_TRUE(base::WriteFile(profile().AppendASCII(name), text));
  }

  // Creates the SQLite file `name` and runs `script` in it.
  void Database(const char* name, base::cstring_view script) {
    sql::Database db(sql::test::kTestTag);
    ASSERT_TRUE(db.Open(profile().AppendASCII(name)));
    ASSERT_TRUE(db.ExecuteScriptForTesting(script));
  }

  void WriteCurrentHistory() {
    Database("History", R"(
      CREATE TABLE urls (id INTEGER PRIMARY KEY, url LONGVARCHAR, title LONGVARCHAR,
                         visit_count INTEGER, typed_count INTEGER, last_visit_time INTEGER,
                         hidden INTEGER);
      INSERT INTO urls VALUES (1, 'https://news.example/', 'News', 4, 1, 13348540800000000, 0);
      INSERT INTO urls VALUES (2, 'chrome://settings/', 'Settings', 1, 0, 13348540800000000, 0);
      INSERT INTO urls VALUES (3, 'https://frame.example/', '', 1, 0, 13348540800000000, 1);
    )");
  }

  void WriteCurrentWebData() {
    Database("Web Data", R"(
      CREATE TABLE keywords (id INTEGER PRIMARY KEY, short_name VARCHAR, keyword VARCHAR,
                             url VARCHAR, safe_for_autoreplace INTEGER, prepopulate_id INTEGER,
                             created_by_policy INTEGER, is_active INTEGER,
                             starter_pack_id INTEGER);
      INSERT INTO keywords VALUES (1, 'Wikipedia', 'w',
          'https://en.wikipedia.org/w/index.php?search={searchTerms}', 0, 0, 0, 1, 0);
      INSERT INTO keywords VALUES (2, 'Google', 'google.com',
          'https://www.google.com/search?q={searchTerms}', 0, 1, 0, 0, 0);
      CREATE TABLE local_addresses_type_tokens (guid VARCHAR, type INTEGER, value VARCHAR,
                                                verification_status INTEGER);
      INSERT INTO local_addresses_type_tokens VALUES ('a', 7, 'Ana Lee', 0);
      INSERT INTO local_addresses_type_tokens VALUES ('a', 33, 'Porto', 0);
      INSERT INTO local_addresses_type_tokens VALUES ('a', 4242, 'internal', 0);
    )");
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir dir_;
};

TEST_F(ChromiumProfileReaderTest, ReadsEverything) {
  Write("Bookmarks", R"({"roots": {"bookmark_bar": {"children": [
      {"type": "url", "name": "Docs", "url": "https://docs.example/"}]}}})");
  Write("AccountBookmarks", R"({"roots": {"other": {"children": [
      {"type": "url", "name": "Later", "url": "https://later.example/"}]}}})");
  WriteCurrentHistory();
  WriteCurrentWebData();

  ChromiumProfileData data = ReadChromiumProfile(profile(), kAll, Options());
  EXPECT_TRUE(data.problems.empty());

  ASSERT_EQ(data.bookmarks.size(), 2u);
  EXPECT_EQ(std::string(data.bookmarks[0].url), "https://docs.example/");
  EXPECT_EQ(data.bookmarks[0].place, 0);
  EXPECT_EQ(std::string(data.bookmarks[1].url), "https://later.example/");
  EXPECT_EQ(data.bookmarks[1].place, 1);

  ASSERT_EQ(data.history.size(), 1u);
  EXPECT_EQ(std::string(data.history[0].url), "https://news.example/");
  EXPECT_EQ(data.history[0].visit_count, 4);
  EXPECT_EQ(data.history[0].last_visit_unix_micros, 1704067200000000);

  ASSERT_EQ(data.search_shortcuts.size(), 1u);
  EXPECT_EQ(std::string(data.search_shortcuts[0].keyword), "w");

  ASSERT_EQ(data.addresses.size(), 1u);
  ASSERT_EQ(data.addresses[0].fields.size(), 2u);
  EXPECT_EQ(std::string(data.addresses[0].fields[1].value), "Porto");
}

TEST_F(ChromiumProfileReaderTest, ReadsOnlyTheItemsAskedFor) {
  Write("Bookmarks", R"({"roots": {"bookmark_bar": {"children": [
      {"type": "url", "name": "Docs", "url": "https://docs.example/"}]}}})");
  WriteCurrentHistory();
  WriteCurrentWebData();

  ChromiumProfileData data = ReadChromiumProfile(profile(), kImportHistory, Options());
  EXPECT_TRUE(data.bookmarks.empty());
  EXPECT_EQ(data.history.size(), 1u);
  EXPECT_TRUE(data.search_shortcuts.empty());
  EXPECT_TRUE(data.addresses.empty());

  data = ReadChromiumProfile(profile(), kImportAddresses, Options());
  EXPECT_TRUE(data.history.empty());
  EXPECT_TRUE(data.search_shortcuts.empty());
  EXPECT_EQ(data.addresses.size(), 1u);
}

TEST_F(ChromiumProfileReaderTest, PagesOlderThanHistoryKeepsDontCome) {
  WriteCurrentHistory();  // News was last visited on 2024-01-01.
  const base::Time new_year = base::Time::UnixEpoch() + base::Microseconds(1704067200000000);
  EXPECT_EQ(ReadChromiumProfile(profile(), kImportHistory, Options(new_year)).history.size(), 1u);
  EXPECT_TRUE(
      ReadChromiumProfile(profile(), kImportHistory, Options(new_year + base::Microseconds(1)))
          .history.empty());
}

TEST_F(ChromiumProfileReaderTest, MissingFilesAreNothingToImport) {
  ChromiumProfileData data = ReadChromiumProfile(profile(), kAll, Options());
  EXPECT_TRUE(data.problems.empty());
  EXPECT_TRUE(data.bookmarks.empty());
  EXPECT_TRUE(data.history.empty());
  EXPECT_TRUE(data.search_shortcuts.empty());
  EXPECT_TRUE(data.addresses.empty());

  // Files without the tables a profile has: nothing to import either.
  Database("Web Data", "CREATE TABLE meta (key LONGVARCHAR, value LONGVARCHAR);");
  data = ReadChromiumProfile(profile(), kAll, Options());
  EXPECT_TRUE(data.problems.empty());
}

TEST_F(ChromiumProfileReaderTest, ReadsOlderLayouts) {
  // Before `hidden`, `is_active`, `starter_pack_id` and the local_ table name.
  Database("History", R"(
    CREATE TABLE urls (id INTEGER PRIMARY KEY, url LONGVARCHAR, title LONGVARCHAR,
                       visit_count INTEGER, typed_count INTEGER, last_visit_time INTEGER);
    INSERT INTO urls VALUES (1, 'https://old.example/', 'Old', 1, 0, 13348540800000000);
  )");
  Database("Web Data", R"(
    CREATE TABLE keywords (id INTEGER PRIMARY KEY, short_name VARCHAR, keyword VARCHAR,
                           url VARCHAR, safe_for_autoreplace INTEGER, prepopulate_id INTEGER,
                           created_by_policy INTEGER);
    INSERT INTO keywords VALUES (1, 'MDN', 'mdn',
        'https://developer.mozilla.org/search?q={searchTerms}', 0, 0, 0);
    CREATE TABLE address_type_tokens (guid VARCHAR, type INTEGER, value VARCHAR);
    INSERT INTO address_type_tokens VALUES ('b', 9, 'bo@example.com');
  )");
  ChromiumProfileData data = ReadChromiumProfile(profile(), kAll, Options());
  EXPECT_TRUE(data.problems.empty());
  EXPECT_EQ(data.history.size(), 1u);
  ASSERT_EQ(data.search_shortcuts.size(), 1u);
  EXPECT_EQ(std::string(data.search_shortcuts[0].keyword), "mdn");
  ASSERT_EQ(data.addresses.size(), 1u);
  EXPECT_EQ(data.addresses[0].fields[0].field_type, 9);
}

TEST_F(ChromiumProfileReaderTest, DamagedFilesAreProblemsAndTheRestStillComes) {
  Write("Bookmarks", "{ not json");
  Write("AccountBookmarks", R"({"roots": {"bookmark_bar": {"children": [
      {"type": "url", "name": "Docs", "url": "https://docs.example/"}]}}})");
  Write("History", std::string(4096, 'x'));
  WriteCurrentWebData();

  ChromiumProfileData data = ReadChromiumProfile(profile(), kAll, Options());
  ASSERT_EQ(data.problems.size(), 2u);
  EXPECT_EQ(data.problems[0].rfind("Bookmarks: Bookmarks not valid JSON", 0), 0u)
      << data.problems[0];
  EXPECT_EQ(data.problems[1].rfind("History: ", 0), 0u) << data.problems[1];
  EXPECT_EQ(data.bookmarks.size(), 1u);
  EXPECT_TRUE(data.history.empty());
  EXPECT_EQ(data.search_shortcuts.size(), 1u);
  EXPECT_EQ(data.addresses.size(), 1u);
}

TEST_F(ChromiumProfileReaderTest, LeavesTheSourceFilesAlone) {
  WriteCurrentHistory();
  // A leftover journal: reading a copy must not roll it into the other browser's file.
  Write("History-journal", std::string(512, '\0'));
  std::string before;
  ASSERT_TRUE(base::ReadFileToString(profile().AppendASCII("History"), &before));

  ChromiumProfileData data = ReadChromiumProfile(profile(), kImportHistory, Options());
  EXPECT_EQ(data.history.size(), 1u);

  std::string after;
  ASSERT_TRUE(base::ReadFileToString(profile().AppendASCII("History"), &after));
  EXPECT_EQ(before, after);
  EXPECT_TRUE(base::PathExists(profile().AppendASCII("History-journal")));
}

// Writes a session file like Chrome's: one window with a pinned tab, a tab in a group, a new-tab
// page and a plain tab; and a second window with one tab. The group has no title or colour: reading
// those needs Chromium's resource bundle, which these tests don't load (the import demo checks them
// in the browser).
void WriteSession(const base::FilePath& profile) {
  using sessions::SerializedNavigationEntry;
  std::vector<std::unique_ptr<sessions::SessionCommand>> commands;
  const tab_groups::TabGroupId group = tab_groups::TabGroupId::GenerateNew();
  auto add_tab = [&](SessionID window, int index, const char* url, const char* title, bool pinned,
                     bool grouped) {
    const SessionID tab = SessionID::NewUnique();
    commands.push_back(sessions::CreateSetTabWindowCommand(window, tab));
    commands.push_back(sessions::CreateSetTabIndexInWindowCommand(tab, index));
    SerializedNavigationEntry nav;
    nav.set_index(0);
    nav.set_virtual_url(GURL(url));
    nav.set_title(base::UTF8ToUTF16(title));
    commands.push_back(sessions::CreateUpdateTabNavigationCommand(tab, nav));
    commands.push_back(sessions::CreateSetSelectedNavigationIndexCommand(tab, 0));
    if (pinned) {
      commands.push_back(sessions::CreatePinnedStateCommand(tab, true));
    }
    if (grouped) {
      commands.push_back(sessions::CreateTabGroupCommand(tab, group));
    }
  };
  const SessionID first = SessionID::NewUnique();
  commands.push_back(
      sessions::CreateSetWindowTypeCommand(first, sessions::SessionWindow::TYPE_NORMAL));
  add_tab(first, 0, "https://mail.example/", "Mail", /*pinned=*/true, false);
  add_tab(first, 1, "https://ci.example/", "CI", false, /*grouped=*/true);
  add_tab(first, 2, "chrome://newtab/", "New Tab", false, false);
  add_tab(first, 3, "https://news.example/", "News", false, false);
  const SessionID second = SessionID::NewUnique();
  commands.push_back(
      sessions::CreateSetWindowTypeCommand(second, sessions::SessionWindow::TYPE_NORMAL));
  add_tab(second, 0, "https://docs.example/", "Docs", false, false);

  auto backend = base::MakeRefCounted<sessions::CommandStorageBackend>(
      base::SequencedTaskRunner::GetCurrentDefault(), profile,
      sessions::CommandStorageManager::SessionType::kSessionRestore, nullptr);
  backend->AppendCommands(std::move(commands), /*truncate=*/true, base::DoNothing());
}

TEST_F(ChromiumProfileReaderTest, ReadsOpenTabsFromACopyOfTheSession) {
  WriteSession(profile());
  std::vector<base::FilePath> before;
  base::FileEnumerator files(profile().AppendASCII("Sessions"), false, base::FileEnumerator::FILES);
  for (base::FilePath f = files.Next(); !f.empty(); f = files.Next()) {
    before.push_back(f);
  }
  ASSERT_FALSE(before.empty());

  ChromiumProfileData data = ReadChromiumProfile(profile(), kImportOpenTabs, Options());
  EXPECT_TRUE(data.problems.empty());
  ASSERT_EQ(data.open_tabs.size(), 4u);
  EXPECT_EQ(std::string(data.open_tabs[0].url), "https://mail.example/");
  EXPECT_TRUE(data.open_tabs[0].pinned);
  EXPECT_EQ(std::string(data.open_tabs[1].url), "https://ci.example/");
  EXPECT_FALSE(std::string(data.open_tabs[1].group).empty());
  EXPECT_EQ(std::string(data.open_tabs[2].title), "News");
  EXPECT_EQ(std::string(data.open_tabs[3].url), "https://docs.example/");
  EXPECT_EQ(data.open_tabs[3].window, 1u);

  // The other browser's session files are left as they were.
  for (const base::FilePath& f : before) {
    EXPECT_TRUE(base::PathExists(f)) << f;
  }
}

TEST_F(ChromiumProfileReaderTest, ReadsStoreExtensionsWithTheirNames) {
  const std::string a(32, 'a');
  const std::string b(32, 'b');
  const std::string c(32, 'c');
  Write("Secure Preferences",
        R"({"extensions": {"settings": {")" + a +
            R"(": {"location": 1, "creation_flags": 8, "path": ")" + a +
            R"(/1.0", "manifest": {"name": "__MSG_appName__",
                                       "default_locale": "en_GB"}},
      ")" + b +
            R"(": {"location": 1, "creation_flags": 8, "manifest": {"name": "Reader"}},
      ")" + c +
            R"(": {"location": 4}}}})");
  const base::FilePath locale = profile()
                                    .AppendASCII("Extensions")
                                    .AppendASCII(a)
                                    .AppendASCII("1.0")
                                    .AppendASCII("_locales")
                                    .AppendASCII("en_GB");
  ASSERT_TRUE(base::CreateDirectory(locale));
  ASSERT_TRUE(base::WriteFile(locale.AppendASCII("messages.json"),
                              R"({"appName": {"message": "Dark Theme"}})"));

  ChromiumReadOptions options = Options();
  ChromiumProfileData data = ReadChromiumProfile(profile(), kImportExtensions, options);
  ASSERT_EQ(data.extensions.size(), 2u);
  EXPECT_EQ(std::string(data.extensions[0].name), "Dark Theme");
  EXPECT_EQ(std::string(data.extensions[1].name), "Reader");
  EXPECT_EQ(data.extensions_not_from_store.size(), 1u);

  options.installed_extensions = {b};
  data = ReadChromiumProfile(profile(), kImportExtensions, options);
  ASSERT_EQ(data.extensions.size(), 1u);
  EXPECT_EQ(std::string(data.extensions[0].id), a);
}

TEST_F(ChromiumProfileReaderTest, ReadsCookiesOnlyWithTheKeychainPassword) {
  CookieDecryptor decryptor("peanuts");
  std::string hash(32, 'h');  // version 24 puts the domain's SHA-256 first
  auto encrypted = [&](const std::string& value) {
    const std::string plain = hash + value;
    const std::vector<uint8_t> bytes = decryptor.EncryptForTesting(base::as_byte_span(plain));
    return std::string("X'") + base::HexEncode(bytes) + "'";
  };
  // 2024-01-02 and 2023-12-31 in Chromium's time base.
  const std::string later = "13348627200000000";
  const std::string earlier = "13348454400000000";
  const std::string script =
      "CREATE TABLE meta (key LONGVARCHAR, value LONGVARCHAR);"
      "INSERT INTO meta VALUES ('version', '24');"
      "CREATE TABLE cookies (host_key TEXT, name TEXT, value TEXT, "
      "encrypted_value BLOB,"
      " path TEXT, creation_utc INTEGER, expires_utc INTEGER, last_access_utc "
      "INTEGER,"
      " is_secure INTEGER, is_httponly INTEGER, has_expires INTEGER, "
      "is_persistent INTEGER,"
      " samesite INTEGER, priority INTEGER, top_frame_site_key TEXT, "
      "source_scheme INTEGER,"
      " source_port INTEGER);"
      "INSERT INTO cookies VALUES ('.a.example', 'sid', '', " +
      encrypted("abc") + ", '/', " + earlier + ", " + later + ", " + earlier +
      ", 1, 1, 1, 1, 1, 1, '', 2, 443);"
      "INSERT INTO cookies VALUES ('.b.example', 'old', '', " +
      encrypted("x") + ", '/', " + earlier + ", " + earlier + ", " + earlier +
      ", 1, 1, 1, 1, 1, 1, '', 2, 443);";
  Database("Cookies", script);

  ChromiumReadOptions options = Options();
  EXPECT_TRUE(ReadChromiumProfile(profile(), kImportCookies, options).cookies.empty());

  options.keychain_password = "peanuts";
  ChromiumProfileData data = ReadChromiumProfile(profile(), kImportCookies, options);
  EXPECT_TRUE(data.problems.empty());
  ASSERT_EQ(data.cookies.size(), 1u);  // the expired one stays behind
  EXPECT_EQ(std::string(data.cookies[0].name), "sid");
  EXPECT_EQ(std::string(data.cookies[0].value), "abc");

  options.keychain_password = "wrong";
  data = ReadChromiumProfile(profile(), kImportCookies, options);
  EXPECT_TRUE(data.cookies.empty());
  ASSERT_EQ(data.problems.size(), 1u);
  EXPECT_EQ(data.problems[0], "Cookies: couldn't be decrypted");
}

TEST_F(ChromiumProfileReaderTest, CookiesThatArentTextStayBehind) {
  CookieDecryptor decryptor("peanuts");
  auto encrypted = [&](const std::string& value) {
    const std::string plain = std::string(32, 'h') + value;
    return std::string("X'") +
           base::HexEncode(decryptor.EncryptForTesting(base::as_byte_span(plain))) + "'";
  };
  const std::string later = "13348627200000000";  // 2024-01-02
  const std::string script =
      "CREATE TABLE meta (key LONGVARCHAR, value LONGVARCHAR);"
      "INSERT INTO meta VALUES ('version', '24');"
      "CREATE TABLE cookies (host_key TEXT, name TEXT, value TEXT, "
      "encrypted_value BLOB,"
      " path TEXT, creation_utc INTEGER, expires_utc INTEGER, last_access_utc "
      "INTEGER,"
      " is_secure INTEGER, is_httponly INTEGER, has_expires INTEGER, "
      "is_persistent INTEGER,"
      " samesite INTEGER, priority INTEGER, top_frame_site_key TEXT, "
      "source_scheme INTEGER,"
      " source_port INTEGER);"
      // A Latin-1 name (bytes Chromium allows), then a good one.
      "INSERT INTO cookies VALUES ('.a.example', CAST(X'70E9' AS TEXT), '', " +
      encrypted("1") + ", '/', " + later + ", " + later + ", " + later +
      ", 1, 1, 1, 1, 1, 1, '', 2, 443);"
      "INSERT INTO cookies VALUES ('.a.example', 'ok', '', " +
      encrypted("2") + ", '/', " + later + ", " + later + ", " + later +
      ", 1, 1, 1, 1, 3, 1, '', 2, 443);";
  Database("Cookies", script);
  ChromiumReadOptions options = Options();
  options.keychain_password = "peanuts";
  ChromiumProfileData data = ReadChromiumProfile(profile(), kImportCookies, options);
  EXPECT_TRUE(data.problems.empty());
  ASSERT_EQ(data.cookies.size(), 1u);
  EXPECT_EQ(std::string(data.cookies[0].name), "ok");
  EXPECT_EQ(data.cookies[0].samesite,
            -1);  // the old "extended" value 3 reads as unspecified
}

TEST_F(ChromiumProfileReaderTest, FilesThatArentTextDontCrash) {
  Write("Bookmarks",
        std::string("{\"roots\": {\"bookmark_bar\": {\"children\": [{\"type\": \"url\","
                    " \"name\": \"Caf\xE9\", \"url\": \"https://cafe.example/\"}]}}}"));
  ChromiumProfileData data = ReadChromiumProfile(profile(), kImportBookmarks, Options());
  EXPECT_TRUE(data.problems.empty());
  ASSERT_EQ(data.bookmarks.size(), 1u);
  EXPECT_EQ(std::string(data.bookmarks[0].title), "Caf\xEF\xBF\xBD");
}

TEST_F(ChromiumProfileReaderTest, OnlyLocalAddressesComeAlong) {
  // Web Data 134 and later: local and Google-account addresses in one table.
  Database("Web Data", R"(
    CREATE TABLE addresses (guid VARCHAR PRIMARY KEY, record_type INTEGER);
    INSERT INTO addresses VALUES ('local', 0);
    INSERT INTO addresses VALUES ('account', 1);
    INSERT INTO addresses VALUES ('name-email', 4);
    CREATE TABLE address_type_tokens (guid VARCHAR, type INTEGER, value VARCHAR);
    INSERT INTO address_type_tokens VALUES ('local', 7, 'Ana Lee');
    INSERT INTO address_type_tokens VALUES ('account', 7, 'Account Person');
    INSERT INTO address_type_tokens VALUES ('name-email', 9, 'ana@example.com');
  )");
  ChromiumProfileData data = ReadChromiumProfile(profile(), kImportAddresses, Options());
  ASSERT_EQ(data.addresses.size(), 1u);
  EXPECT_EQ(std::string(data.addresses[0].fields[0].value), "Ana Lee");
}

TEST(PlainCopyIsStaleTest, OnlyAnEncryptedCopyFromALaterSaveMakesThePlainFileStale) {
  const base::Time t = base::Time::UnixEpoch() + base::Days(20000);
  // No encrypted copy: the plain file (or nothing) is what there is.
  EXPECT_FALSE(PlainCopyIsStale(t, base::Time()));
  EXPECT_FALSE(PlainCopyIsStale(base::Time(), base::Time()));
  // Only an encrypted copy.
  EXPECT_TRUE(PlainCopyIsStale(base::Time(), t));
  // Written in the same save (a browser may write the encrypted copy up to some milliseconds
  // later).
  EXPECT_FALSE(PlainCopyIsStale(t, t));
  EXPECT_FALSE(PlainCopyIsStale(t, t + base::Milliseconds(30)));
  EXPECT_FALSE(PlainCopyIsStale(t, t + base::Minutes(1)));
  EXPECT_FALSE(PlainCopyIsStale(t + base::Hours(1), t));
  // The browser has stopped writing the plain one.
  EXPECT_TRUE(PlainCopyIsStale(t, t + base::Minutes(1) + base::Milliseconds(1)));
  EXPECT_TRUE(PlainCopyIsStale(t, t + base::Days(3)));
}

// Sets when every file in `dir` was last changed.
void TouchFilesIn(const base::FilePath& dir, base::Time when) {
  base::FileEnumerator files(dir, /*recursive=*/false, base::FileEnumerator::FILES);
  for (base::FilePath f = files.Next(); !f.empty(); f = files.Next()) {
    ASSERT_TRUE(base::TouchFile(f, when, when));
  }
}

TEST_F(ChromiumProfileReaderTest, ReadsPlainSessionsWrittenWithAnEncryptedCopy) {
  // Sessions_Encrypted is saved with Sessions, a few milliseconds after it, here in the
  // next second (folder times are read to the second).
  WriteSession(profile());
  const base::Time saved = base::Time::FromTimeT((base::Time::Now() - base::Hours(1)).ToTimeT()) +
                           base::Milliseconds(990);
  TouchFilesIn(profile().AppendASCII("Sessions"), saved);
  const base::FilePath encrypted = profile().AppendASCII("Sessions_Encrypted");
  ASSERT_TRUE(base::CreateDirectory(encrypted));
  ASSERT_TRUE(base::WriteFile(encrypted.AppendASCII("Session_13400000000000000"), "x"));
  TouchFilesIn(encrypted, saved + base::Milliseconds(30));

  ChromiumProfileData data = ReadChromiumProfile(profile(), kImportOpenTabs, Options());
  EXPECT_TRUE(data.problems.empty());
  EXPECT_EQ(data.open_tabs.size(), 4u);
}

TEST_F(ChromiumProfileReaderTest, StaleSessionsNextToEncryptedOnesAreAProblem) {
  WriteSession(profile());
  const base::Time saved = base::Time::Now() - base::Days(2);
  TouchFilesIn(profile().AppendASCII("Sessions"), saved);
  const base::FilePath encrypted = profile().AppendASCII("Sessions_Encrypted");
  ASSERT_TRUE(base::CreateDirectory(encrypted));
  ASSERT_TRUE(base::WriteFile(encrypted.AppendASCII("Session_13400000000000000"), "x"));
  TouchFilesIn(encrypted, saved + base::Days(1));

  ChromiumProfileData data = ReadChromiumProfile(profile(), kImportOpenTabs, Options());
  EXPECT_TRUE(data.open_tabs.empty());
  ASSERT_EQ(data.problems.size(), 1u);
  EXPECT_EQ(data.problems[0].rfind("Open tabs: this browser keeps them encrypted", 0), 0u);
}

TEST_F(ChromiumProfileReaderTest, ReadsPlainBookmarksWrittenWithAnEncryptedCopy) {
  Write("Bookmarks", R"({"roots": {"bookmark_bar": {"children": [
      {"type": "url", "name": "Docs", "url": "https://docs.example/"}]}}})");
  Write("EncryptedBookmarks2", "v10 not read");
  const base::Time saved = base::Time::Now() - base::Hours(1);
  ASSERT_TRUE(base::TouchFile(profile().AppendASCII("Bookmarks"), saved, saved));
  ASSERT_TRUE(base::TouchFile(profile().AppendASCII("EncryptedBookmarks2"), saved, saved));

  ChromiumProfileData data = ReadChromiumProfile(profile(), kImportBookmarks, Options());
  EXPECT_TRUE(data.problems.empty());
  ASSERT_EQ(data.bookmarks.size(), 1u);
  EXPECT_EQ(std::string(data.bookmarks[0].url), "https://docs.example/");
}

TEST_F(ChromiumProfileReaderTest, EncryptedOnlyBookmarksAreOneProblem) {
  // The account's bookmarks only encrypted, and the plain local ones stale: one line for both, and
  // the stale ones don't come along.
  Write("Bookmarks", R"({"roots": {"bookmark_bar": {"children": [
      {"type": "url", "name": "Old", "url": "https://old.example/"}]}}})");
  Write("EncryptedBookmarks2", "v10 not read");
  Write("EncryptedAccountBookmarks2", "v10 not read");
  const base::Time saved = base::Time::Now() - base::Days(2);
  ASSERT_TRUE(base::TouchFile(profile().AppendASCII("Bookmarks"), saved, saved));

  ChromiumProfileData data = ReadChromiumProfile(profile(), kImportBookmarks, Options());
  EXPECT_TRUE(data.bookmarks.empty());
  ASSERT_EQ(data.problems.size(), 1u);
  EXPECT_EQ(data.problems[0],
            "Bookmarks: this browser keeps them encrypted, so they couldn't be read");
}

TEST_F(ChromiumProfileReaderTest, EncryptedSessionsAreNamedAsAProblem) {
  const base::FilePath encrypted = profile().AppendASCII("Sessions_Encrypted");
  ASSERT_TRUE(base::CreateDirectory(encrypted));
  ASSERT_TRUE(base::WriteFile(encrypted.AppendASCII("Session_13400000000000000"), "x"));
  ChromiumProfileData data = ReadChromiumProfile(profile(), kImportOpenTabs, Options());
  EXPECT_TRUE(data.open_tabs.empty());
  ASSERT_EQ(data.problems.size(), 1u);
  EXPECT_EQ(data.problems[0].rfind("Open tabs: this browser keeps them encrypted", 0), 0u);
}

}  // namespace
}  // namespace lb
