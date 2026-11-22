// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/import_demo.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/autofill/core/browser/data_manager/addresses/address_data_manager.h"
#include "components/autofill/core/browser/data_manager/personal_data_manager.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_profile.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/search_engines/template_url_service.h"
#include "components/sessions/core/command_storage_backend.h"
#include "components/sessions/core/command_storage_manager.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "components/sessions/core/session_id.h"
#include "components/sessions/core/session_service_commands.h"
#include "components/sessions/core/session_types.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "lb/browser/demo/demo_report.h"
#include "lb/browser/importer/arc_import.h"
#include "lb/browser/importer/chromium_import.h"
#include "lb/browser/importer/cookie_decryptor.h"
#include "lb/browser/importer/import_sources.h"
#include "lb/browser/importer/other_import.h"
#include "lb/browser/spaces/space_model.h"
#include "lb/browser/ui/views/importer/import_dialog.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_access_result.h"
#include "net/cookies/cookie_options.h"
#include "net/cookies/cookie_partition_key_collection.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "sql/database.h"
#include "third_party/zlib/google/zip.h"
#include "ui/base/base_window.h"
#include "url/gurl.h"

namespace lb {

namespace {

constexpr char kSourceSwitch[] = "lb-import-source";
constexpr char kFixtureMode[] = "import-fixture";
constexpr char kArcMode[] = "import-arc";
constexpr char kDialogMode[] = "import-dialog";
constexpr char kImportMode[] = "import";
constexpr sql::Database::Tag kTag = "FirefoxImporter";
constexpr char kFilesMode[] = "import-files";
constexpr char kDiaMode[] = "import-dia";
constexpr uint32_t kChromiumItems = kImportBookmarks | kImportHistory | kImportSearchShortcuts |
                                    kImportAddresses | kImportOpenTabs | kImportCookies |
                                    kImportExtensions;

std::string Counts(const ImportResult& r) {
  const ImportCounts& c = r.counts;
  std::string out = "bookmarks=" + base::NumberToString(c.bookmarks) +
                    " history=" + base::NumberToString(c.history) +
                    " search_shortcuts=" + base::NumberToString(c.search_shortcuts) +
                    " addresses=" + base::NumberToString(c.addresses) +
                    " spaces=" + base::NumberToString(c.spaces) +
                    " tabs=" + base::NumberToString(c.tabs) +
                    " cookies=" + base::NumberToString(c.cookies) +
                    " extensions=" + base::NumberToString(r.extensions.size()) +
                    " problems=" + base::NumberToString(r.problems.size());
  for (const std::string& problem : r.problems) {
    out += " | " + problem;
  }
  return out;
}

bool RunScript(sql::Database& db, const std::string& script) {
  std::string statement;
  for (const std::string& line :
       base::SplitString(script, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (base::StartsWith(base::TrimWhitespaceASCII(line, base::TRIM_LEADING), "--")) {
      continue;
    }
    statement += line + "\n";
    if (base::EndsWith(base::TrimWhitespaceASCII(line, base::TRIM_TRAILING), ";")) {
      if (!db.Execute(statement)) {
        return false;
      }
      statement.clear();
    }
  }
  return true;
}

// A session file like Chrome's: a pinned tab, two tabs in a "Work" group, a new-tab page.
void WriteSession(const base::FilePath& profile) {
  std::vector<std::unique_ptr<sessions::SessionCommand>> commands;
  const SessionID window = SessionID::NewUnique();
  const tab_groups::TabGroupId group = tab_groups::TabGroupId::GenerateNew();
  const tab_groups::TabGroupVisualData visuals(u"Work", tab_groups::TabGroupColorId::kGreen);
  commands.push_back(
      sessions::CreateSetWindowTypeCommand(window, sessions::SessionWindow::TYPE_NORMAL));
  int index = 0;
  for (const auto& [url, title, pinned, grouped] :
       std::vector<std::tuple<const char*, const char*, bool, bool>>{
           {"https://mail.example/", "Mail", true, false},
           {"https://ci.example/", "CI", false, true},
           {"https://tracker.example/", "Tracker", false, true},
           {"chrome://newtab/", "New Tab", false, false}}) {
    const SessionID tab = SessionID::NewUnique();
    commands.push_back(sessions::CreateSetTabWindowCommand(window, tab));
    commands.push_back(sessions::CreateSetTabIndexInWindowCommand(tab, index++));
    sessions::SerializedNavigationEntry nav;
    nav.set_index(0);
    nav.set_virtual_url(GURL(url));
    nav.set_title(base::UTF8ToUTF16(std::string(title)));
    commands.push_back(sessions::CreateUpdateTabNavigationCommand(tab, nav));
    commands.push_back(sessions::CreateSetSelectedNavigationIndexCommand(tab, 0));
    if (pinned) {
      commands.push_back(sessions::CreatePinnedStateCommand(tab, true));
    }
    if (grouped) {
      commands.push_back(sessions::CreateTabGroupCommand(tab, group));
    }
  }
  commands.push_back(sessions::CreateTabGroupMetadataUpdateCommand(group, &visuals));
  auto backend = base::MakeRefCounted<sessions::CommandStorageBackend>(
      base::SequencedTaskRunner::GetCurrentDefault(), profile,
      sessions::CommandStorageManager::SessionType::kSessionRestore, nullptr);
  backend->AppendCommands(std::move(commands), /*truncate=*/true, base::DoNothing());
}

// A Cookies file like Chrome's (version 24) with one live cookie, encrypted with the password
// the mock keychain gives every item.
bool WriteCookies(const base::FilePath& profile) {
  std::optional<std::string> password = ReadImportKeychainPassword("Chrome Safe Storage", "Chrome");
  if (!password) {
    return false;
  }
  CookieDecryptor key(*password);
  const std::string plain = std::string(32, 'h') + "abc";
  const std::string blob = base::HexEncode(key.EncryptForTesting(base::as_byte_span(plain)));
  const std::string tomorrow = base::NumberToString(
      (base::Time::Now() + base::Days(1)).ToDeltaSinceWindowsEpoch().InMicroseconds());
  const std::string now =
      base::NumberToString(base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds());
  sql::Database db(kTag);
  return db.Open(profile.AppendASCII("Cookies")) &&
         RunScript(db,
                   "CREATE TABLE meta (key LONGVARCHAR, value LONGVARCHAR);\n"
                   "INSERT INTO meta VALUES ('version', '24');\n"
                   "CREATE TABLE cookies (host_key TEXT, name TEXT, value TEXT, "
                   "encrypted_value BLOB, path TEXT, creation_utc INTEGER, expires_utc INTEGER, "
                   "last_access_utc INTEGER, is_secure INTEGER, is_httponly INTEGER, "
                   "has_expires INTEGER, is_persistent INTEGER, samesite INTEGER, "
                   "priority INTEGER, top_frame_site_key TEXT, source_scheme INTEGER, "
                   "source_port INTEGER);\n"
                   "INSERT INTO cookies VALUES ('.fixture.example', 'sid', '', X'" +
                       blob + "', '/', " + now + ", " + tomorrow + ", " + now +
                       ", 1, 1, 1, 1, 1, 1, '', 2, 443);\n");
}

// Copies the made-up Chrome profile into `dir`, building its SQLite, session and cookie files.
bool PrepareChrome(const base::FilePath& home, const base::FilePath& dir) {
  const base::FilePath fixture = home.AppendASCII("Google/Chrome/Default");
  if (!base::CopyFile(fixture.AppendASCII("Bookmarks"), dir.AppendASCII("Bookmarks")) ||
      !base::CopyFile(fixture.AppendASCII("Secure Preferences"),
                      dir.AppendASCII("Secure Preferences"))) {
    return false;
  }
  for (const char* name : {"History", "Web Data"}) {
    std::string script;
    sql::Database db(kTag);
    if (!base::ReadFileToString(fixture.AppendASCII(std::string(name) + ".sql"), &script) ||
        !db.Open(dir.AppendASCII(name)) || !RunScript(db, script)) {
      return false;
    }
    if (std::string(name) == "History") {
      // Visits a day ago, except the page marked old, which is older than history keeps.
      const int64_t yesterday =
          (base::Time::Now() - base::Days(1)).ToDeltaSinceWindowsEpoch().InMicroseconds();
      if (!db.Execute("UPDATE urls SET last_visit_time = " + base::NumberToString(yesterday) +
                      " WHERE title != 'Old'")) {
        return false;
      }
    }
  }
  WriteSession(dir);
  return WriteCookies(dir);
}

// A check's state, and what it found.
struct Run {
  std::string mode;
  base::WeakPtr<BrowserWindowInterface> browser;
  base::WeakPtr<Profile> profile;
  base::ScopedTempDir dir;
  std::vector<std::string> failures;

  void Expect(bool ok, const std::string& what) {
    if (!ok) {
      failures.push_back(what);
    }
  }
};

void Report(std::unique_ptr<Run> run, const std::string& detail) {
  ReportDemo(run->mode, run->failures.empty(),
             run->failures.empty() ? detail : base::JoinString(run->failures, "; "));
}

const bookmarks::BookmarkNode* Child(const bookmarks::BookmarkNode* parent,
                                     const std::u16string& title) {
  for (const auto& child : parent->children()) {
    if (child->GetTitle() == title) {
      return child.get();
    }
  }
  return nullptr;
}

bool HasAnaLee(Profile* profile) {
  autofill::PersonalDataManager* pdm =
      autofill::PersonalDataManagerFactory::GetForBrowserContext(profile);
  if (!pdm) {
    return false;
  }
  size_t found = 0;
  for (const autofill::AutofillProfile* p : pdm->address_data_manager().GetProfiles()) {
    found += p->GetRawInfo(autofill::NAME_FULL) == u"Ana Lee" &&
             p->GetRawInfo(autofill::ADDRESS_HOME_CITY) == u"Porto";
  }
  return found == 1;
}

// The window's tabs as "url" (pinned ones "P url", grouped ones "url [group title]").
std::vector<std::string> TabsOf(BrowserWindowInterface* browser) {
  std::vector<std::string> tabs;
  TabStripModel* model = browser->GetTabStripModel();
  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    std::string entry = (model->IsTabPinned(i) ? "P " : "") + contents->GetVisibleURL().spec();
    if (std::optional<tab_groups::TabGroupId> group = model->GetTabGroupForTab(i)) {
      if (TabGroup* g = model->group_model()->GetTabGroup(*group)) {
        entry += " [" + base::UTF16ToUTF8(g->visual_data()->title()) + "]";
      }
    }
    tabs.push_back(entry);
  }
  return tabs;
}

bool Contains(const std::vector<std::string>& list, const std::string& item) {
  return std::ranges::find(list, item) != list.end();
}

// --- import-fixture ---------------------------------------------------------------------------

ChromiumImportRequest FixtureRequest(Run* run, uint32_t items) {
  ChromiumImportRequest request;
  request.profile_dir = run->dir.GetPath();
  request.items = items;
  request.keychain_service = "Chrome Safe Storage";
  request.keychain_account = "Chrome";
  request.space_id = GetWindowSpaceId(run->browser.get());
  return request;
}

void SecondImportDone(std::unique_ptr<Run> run, ImportResult second) {
  run->Expect(second.counts.bookmarks == 0 && second.counts.history == 0 &&
                  second.counts.search_shortcuts == 0 && second.counts.addresses == 0 &&
                  second.problems.empty(),
              "second import added something: " + Counts(second));
  if (Profile* profile = run->profile.get()) {
    run->Expect(HasAnaLee(profile), "the address isn't there exactly once");
    const bookmarks::BookmarkNode* work =
        Child(BookmarkModelFactory::GetForBrowserContext(profile)->bookmark_bar_node(), u"Work");
    run->Expect(work && work->children().size() == 2, "Work holds its two links once");
  }
  Report(std::move(run), "imported, then nothing added twice");
}

// Waits (a few seconds at most) for the address to be saved, then imports again (without tabs
// and cookies, which an import adds again by design).
void ImportAgainOnceSaved(std::unique_ptr<Run> run, int tries) {
  Profile* profile = run->profile.get();
  if (!profile || !run->browser) {
    return;
  }
  if (!HasAnaLee(profile) && tries > 0) {
    PostDemoTask(base::BindOnce(&ImportAgainOnceSaved, std::move(run), tries - 1),
                 base::Milliseconds(200));
    return;
  }
  Run* raw = run.get();
  ImportChromiumProfile(profile,
                        FixtureRequest(raw, kImportBookmarks | kImportHistory |
                                                kImportSearchShortcuts | kImportAddresses),
                        base::BindOnce(&SecondImportDone, std::move(run)));
}

void CheckCookie(std::unique_ptr<Run> run) {
  Profile* profile = run->profile.get();
  if (!profile) {
    return;
  }
  profile->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess()->GetCookieList(
      GURL("https://fixture.example/"), net::CookieOptions::MakeAllInclusive(),
      net::CookiePartitionKeyCollection(),
      base::BindOnce(
          [](std::unique_ptr<Run> run, const net::CookieAccessResultList& included,
             const net::CookieAccessResultList& excluded) {
            bool found = false;
            for (const net::CookieWithAccessResult& c : included) {
              found = found || (c.cookie.Name() == "sid" && c.cookie.Value() == "abc");
            }
            run->Expect(found, "the decrypted cookie is in the profile");
            ImportAgainOnceSaved(std::move(run), /*tries=*/25);
          },
          std::move(run)));
}

void FirstImportDone(std::unique_ptr<Run> run, ImportResult first) {
  Profile* profile = run->profile.get();
  BrowserWindowInterface* browser = run->browser.get();
  if (!profile || !browser) {
    return;
  }
  run->Expect(first.counts.bookmarks == 4 && first.counts.history == 2 &&
                  first.counts.search_shortcuts == 1 && first.counts.addresses == 1 &&
                  first.counts.tabs == 3 && first.counts.cookies == 1 && first.problems.empty(),
              "first import: " + Counts(first));
  run->Expect(first.extensions.size() == 1 && first.extensions[0].name == u"Made-up Reader" &&
                  first.extensions_not_from_store == 1,
              "the store extension is offered, the unpacked one counted");

  bookmarks::BookmarkModel* model = BookmarkModelFactory::GetForBrowserContext(profile);
  const bookmarks::BookmarkNode* work = Child(model->bookmark_bar_node(), u"Work");
  run->Expect(Child(model->bookmark_bar_node(), u"Docs") != nullptr, "Docs is in the bar");
  run->Expect(work && work->is_folder() && Child(work, u"CI"), "CI is in the bar's Work folder");
  run->Expect(Child(model->other_node(), u"Read later") != nullptr,
              "Read later is in other bookmarks");
  TemplateURLService* search = TemplateURLServiceFactory::GetForProfile(profile);
  run->Expect(search->GetTemplateURLForKeyword(u"w") != nullptr, "the w search shortcut");
  run->Expect(search->GetTemplateURLForKeyword(u"google.com") == nullptr,
              "the built-in Google engine didn't come along");

  const std::vector<std::string> tabs = TabsOf(browser);
  run->Expect(Contains(tabs, "P https://mail.example/"), "Mail is a pinned tab");
  run->Expect(Contains(tabs, "https://ci.example/ [Work]") &&
                  Contains(tabs, "https://tracker.example/ [Work]"),
              "CI and Tracker are in the Work group: " + base::JoinString(tabs, ", "));

  static base::NoDestructor<base::CancelableTaskTracker> tracker;
  HistoryServiceFactory::GetForProfile(profile, ServiceAccessType::EXPLICIT_ACCESS)
      ->QueryURL(GURL("https://news.example/"),
                 base::BindOnce(
                     [](std::unique_ptr<Run> run, history::QueryURLResult result) {
                       run->Expect(result.success && result.row.visit_count() >= 4 &&
                                       result.row.title() == u"News",
                                   "News is in history with its visits");
                       CheckCookie(std::move(run));
                     },
                     std::move(run)),
                 tracker.get());
}

void RunFixture(std::unique_ptr<Run> run, const base::FilePath& home) {
  FailDemoAfter(kFixtureMode, base::Seconds(40));
  const base::FilePath dir = run->dir.GetPath();
  // Chromium's session backend needs a sequence of its own to write the session file on.
  base::ThreadPool::CreateSingleThreadTaskRunner({base::MayBlock()})
      ->PostTaskAndReplyWithResult(
          FROM_HERE, base::BindOnce(&PrepareChrome, home, dir),
          base::BindOnce(
              [](std::unique_ptr<Run> run, bool ok) {
                Profile* profile = run->profile.get();
                if (!ok || !profile) {
                  ReportDemo(kFixtureMode, false, "couldn't build the fixture profile");
                  return;
                }
                Run* raw = run.get();
                ImportChromiumProfile(profile, FixtureRequest(raw, kChromiumItems),
                                      base::BindOnce(&FirstImportDone, std::move(run)));
              },
              std::move(run)));
}

// --- import-arc -------------------------------------------------------------------------------

void ArcImported(std::unique_ptr<Run> run, ImportResult result) {
  Profile* profile = run->profile.get();
  BrowserWindowInterface* browser = run->browser.get();
  if (!profile || !browser) {
    return;
  }
  run->Expect(result.counts.spaces == 2 && result.counts.tabs == 6 && result.problems.empty(),
              "import: " + Counts(result));

  // Personal was empty, so it was filled, in this window: Favourites, the pinned tabs (the
  // folder flattened), then the today tab.
  std::vector<std::string> tabs = TabsOf(browser);
  std::erase_if(tabs,
                [](const std::string& t) { return t.find(".example/") == std::string::npos; });
  run->Expect(
      tabs == std::vector<std::string>{"P https://calendar.example/", "P https://mail.example/",
                                       "P https://blog.example/", "P https://news.example/",
                                       "https://recipe.example/"},
      "Personal's tabs: " + base::JoinString(tabs, ", "));

  size_t personal = 0;
  std::optional<Space> work;
  for (const auto& [space, empty] : SidebarController::GetSpacesForImport()) {
    personal += space.name == "Personal";
    if (space.name == "Work") {
      work = space;
    }
  }
  run->Expect(personal == 1, "Personal was filled, not added again");
  run->Expect(work && work->icon == "💼", "Work is a space with its emoji");
  std::u16string work_profile_name;
  for (ProfileAttributesEntry* entry : g_browser_process->profile_manager()
                                           ->GetProfileAttributesStorage()
                                           .GetAllProfilesAttributes()) {
    if (work && entry->GetPath().BaseName().AsUTF8Unsafe() == work->profile) {
      work_profile_name = entry->GetLocalProfileName();
    }
  }
  std::string profiles;
  for (ProfileAttributesEntry* entry : g_browser_process->profile_manager()
                                           ->GetProfileAttributesStorage()
                                           .GetAllProfilesAttributes()) {
    profiles += entry->GetPath().BaseName().AsUTF8Unsafe() + "=" +
                base::UTF16ToUTF8(entry->GetLocalProfileName()) + " ";
  }
  run->Expect(work_profile_name == u"Client",
              "Work is in a new profile called Client (Work's profile: " +
                  (work ? work->profile : std::string("none")) + "; profiles: " + profiles + ")");
  const bookmarks::BookmarkNode* bar =
      BookmarkModelFactory::GetForBrowserContext(profile)->bookmark_bar_node();
  run->Expect(Child(bar, u"Arc docs") != nullptr,
              "Arc's default profile's bookmark is in the default profile");
  run->Expect(Child(bar, u"Client wiki") == nullptr,
              "the Client profile's bookmark stays out of the default profile");
  Report(std::move(run), "Arc's spaces, tabs, profile and bookmarks are in place");
}

void RunArc(std::unique_ptr<Run> run, const base::FilePath& home) {
  FailDemoAfter(kArcMode, base::Seconds(40));
  const std::string current_profile = run->profile->GetBaseName().AsUTF8Unsafe();
  ImportArc(home.AppendASCII("Arc"), kImportSpaces | kImportBookmarks, "Arc Safe Storage", "Arc",
            current_profile, base::BindOnce(&ArcImported, std::move(run)));
}

// --- import-dia -------------------------------------------------------------------------------

// Sets when every file in `dir` was last changed.
bool TouchFilesIn(const base::FilePath& dir, base::Time when) {
  bool ok = true;
  base::FileEnumerator files(dir, /*recursive=*/false, base::FileEnumerator::FILES);
  for (base::FilePath f = files.Next(); !f.empty(); f = files.Next()) {
    ok = base::TouchFile(f, when, when) && ok;
  }
  return ok;
}

// Copies the made-up Dia profile into `dir` and writes its session the way Dia saves it: the
// plain files, and an encrypted copy of each saved some milliseconds later, here in the next
// second. The copies' contents stand in for encrypted ones; only their times matter.
bool PrepareDia(const base::FilePath& home, const base::FilePath& dir) {
  const base::FilePath fixture = home.AppendASCII("Dia/User Data/Profile 10");
  for (const char* name : {"Bookmarks", "EncryptedBookmarks2"}) {
    if (!base::CopyFile(fixture.AppendASCII(name), dir.AppendASCII(name))) {
      return false;
    }
  }
  WriteSession(dir);
  const base::FilePath sessions = dir.AppendASCII("Sessions");
  const base::FilePath encrypted = dir.AppendASCII("Sessions_Encrypted");
  if (!base::CreateDirectory(encrypted)) {
    return false;
  }
  base::FileEnumerator files(sessions, /*recursive=*/false, base::FileEnumerator::FILES);
  for (base::FilePath f = files.Next(); !f.empty(); f = files.Next()) {
    if (!base::WriteFile(encrypted.Append(f.BaseName()), "v10")) {
      return false;
    }
  }
  const base::Time saved = base::Time::FromTimeT((base::Time::Now() - base::Hours(1)).ToTimeT()) +
                           base::Milliseconds(990);
  const base::Time copied = saved + base::Milliseconds(30);
  return TouchFilesIn(sessions, saved) && TouchFilesIn(encrypted, copied) &&
         base::TouchFile(dir.AppendASCII("Bookmarks"), saved, saved) &&
         base::TouchFile(dir.AppendASCII("EncryptedBookmarks2"), copied, copied);
}

void DiaImported(std::unique_ptr<Run> run, ImportResult result) {
  Profile* profile = run->profile.get();
  BrowserWindowInterface* browser = run->browser.get();
  if (!profile || !browser) {
    return;
  }
  run->Expect(result.counts.bookmarks == 1 && result.counts.tabs == 3 && result.problems.empty(),
              "Dia import: " + Counts(result));
  bookmarks::BookmarkModel* model = BookmarkModelFactory::GetForBrowserContext(profile);
  run->Expect(Child(model->bookmark_bar_node(), u"Dia notes") != nullptr,
              "Dia notes is in the bar");
  const std::vector<std::string> tabs = TabsOf(browser);
  run->Expect(
      Contains(tabs, "P https://mail.example/") && Contains(tabs, "https://ci.example/ [Work]"),
      "Dia's open tabs arrived: " + base::JoinString(tabs, ", "));
  Report(std::move(run), "plain files saved with encrypted copies are read: bookmark and tabs");
}

void RunDia(std::unique_ptr<Run> run, const base::FilePath& home) {
  FailDemoAfter(kDiaMode, base::Seconds(40));
  const base::FilePath dir = run->dir.GetPath();
  base::ThreadPool::CreateSingleThreadTaskRunner({base::MayBlock()})
      ->PostTaskAndReplyWithResult(
          FROM_HERE, base::BindOnce(&PrepareDia, home, dir),
          base::BindOnce(
              [](std::unique_ptr<Run> run, bool ok) {
                Profile* profile = run->profile.get();
                if (!ok || !profile) {
                  ReportDemo(kDiaMode, false, "couldn't build the Dia profile");
                  return;
                }
                ChromiumImportRequest request;
                request.profile_dir = run->dir.GetPath();
                request.items = kImportBookmarks | kImportOpenTabs;
                request.space_id = GetWindowSpaceId(run->browser.get());
                ImportChromiumProfile(profile, request,
                                      base::BindOnce(&DiaImported, std::move(run)));
              },
              std::move(run)));
}

// --- import-dialog ----------------------------------------------------------------------------

void RunDialog(std::unique_ptr<Run> run) {
  FailDemoAfter(kDialogMode, base::Seconds(20));
  FindImportSources(base::BindOnce(
      [](std::unique_ptr<Run> run, std::vector<ImportSource> sources) {
        std::vector<std::string> labels;
        for (const ImportSource& s : sources) {
          labels.push_back(base::UTF16ToUTF8(s.label));
        }
        run->Expect(labels.size() == 3 && Contains(labels, "Arc") &&
                        Contains(labels, "Chrome — Personal") && Contains(labels, "Dia — Studio"),
                    "sources: " + base::JoinString(labels, ", "));
        if (!run->browser) {
          return;
        }
        // The app menu's command, which Settings' row and the command bar run too.
        chrome::ExecuteCommand(run->browser.get(), IDC_IMPORT_SETTINGS);
        PostDemoTask(base::BindOnce(
                         [](std::unique_ptr<Run> run) {
                           run->Expect(ImportDialogCanImportForTesting(),
                                       "Import can be pressed with the first source picked");
                           Report(std::move(run),
                                  "lists Arc, Chrome — Personal and Dia — Studio; Import works");
                         },
                         std::move(run)),
                     base::Seconds(2));
      },
      std::move(run)));
}

// --- import-files -----------------------------------------------------------------------------

// Safari's export: a .zip with Bookmarks.html and History.json (a visit a day ago).
bool MakeSafariExport(const base::FilePath& home, const base::FilePath& dir) {
  const base::FilePath contents = dir.AppendASCII("export");
  const int64_t yesterday =
      ((base::Time::Now() - base::Days(1)) - base::Time::UnixEpoch()).InMicroseconds();
  return base::CreateDirectory(contents) &&
         base::CopyFile(home.AppendASCII("bookmarks.html"),
                        contents.AppendASCII("Bookmarks.html")) &&
         base::WriteFile(contents.AppendASCII("History.json"),
                         R"({"metadata": {"browser_name": "Safari", "data_type": "history"},
                             "history": [{"url": "https://safari-page.example/",
                                          "title": "Safari page", "visit_count": 2,
                                          "time_usec": )" +
                             base::NumberToString(yesterday) + "}]}") &&
         zip::Zip(contents, dir.AppendASCII("Safari Export.zip"), /*include_hidden_files=*/false);
}

void SafariExportImported(std::unique_ptr<Run> run, ImportResult result) {
  Profile* profile = run->profile.get();
  if (!profile) {
    return;
  }
  // Its bookmarks are the same file's: already here, so none are added again.
  run->Expect(result.counts.bookmarks == 0 && result.counts.history == 1 && result.problems.empty(),
              "Safari export: " + Counts(result));
  static base::NoDestructor<base::CancelableTaskTracker> tracker;
  HistoryServiceFactory::GetForProfile(profile, ServiceAccessType::EXPLICIT_ACCESS)
      ->QueryURL(GURL("https://safari-page.example/"),
                 base::BindOnce(
                     [](std::unique_ptr<Run> run, history::QueryURLResult result) {
                       run->Expect(result.success && result.row.title() == u"Safari page",
                                   "Safari's history page is in history");
                       Report(std::move(run),
                              "a bookmarks file and Safari's export came in, merged");
                     },
                     std::move(run)),
                 tracker.get());
}

void BookmarksFileImported(std::unique_ptr<Run> run, ImportResult result) {
  Profile* profile = run->profile.get();
  if (!profile) {
    return;
  }
  run->Expect(result.counts.bookmarks == 4 && result.problems.empty(),
              "bookmarks file: " + Counts(result));
  bookmarks::BookmarkModel* model = BookmarkModelFactory::GetForBrowserContext(profile);
  const bookmarks::BookmarkNode* recipes = Child(model->bookmark_bar_node(), u"Recipes");
  run->Expect(recipes && recipes->children().size() == 2,
              "Recipes is a folder of two in the bookmarks bar, not under \"Imported\"");
  run->Expect(Child(model->bookmark_bar_node(), u"Maps") != nullptr, "Maps is in the bar");
  run->Expect(Child(model->other_node(), u"Read later") != nullptr,
              "Read later is in other bookmarks");
  const base::FilePath zip = run->dir.GetPath().AppendASCII("Safari Export.zip");
  ImportSafariExport(profile, zip, kImportBookmarks | kImportHistory,
                     base::BindOnce(&SafariExportImported, std::move(run)));
}

void RunFiles(std::unique_ptr<Run> run, const base::FilePath& home) {
  FailDemoAfter(kFilesMode, base::Seconds(40));
  const base::FilePath dir = run->dir.GetPath();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()}, base::BindOnce(&MakeSafariExport, home, dir),
      base::BindOnce(
          [](std::unique_ptr<Run> run, base::FilePath home, bool ok) {
            Profile* profile = run->profile.get();
            BrowserWindowInterface* browser = run->browser.get();
            if (!ok || !profile || !browser) {
              ReportDemo(kFilesMode, false, "couldn't make Safari's export file");
              return;
            }
            user_data_importer::SourceProfile source;
            source.importer_type = user_data_importer::TYPE_BOOKMARKS_FILE;
            source.source_path = home.AppendASCII("bookmarks.html");
            source.services_supported = user_data_importer::FAVORITES;
            ImportWithChromiumImporter(profile, source, kImportBookmarks,
                                       browser->GetWindow()->GetNativeWindow(),
                                       base::BindOnce(&BookmarksFileImported, std::move(run)));
          },
          std::move(run), home));
}

}  // namespace

bool MaybeRunImportDemo(BrowserWindowInterface* browser, const std::string& mode) {
  if (mode != kFixtureMode && mode != kArcMode && mode != kDialogMode && mode != kFilesMode &&
      mode != kDiaMode && mode != kImportMode) {
    return false;
  }
  auto run = std::make_unique<Run>();
  run->mode = mode;
  run->browser = browser->GetWeakPtr();
  run->profile = browser->GetProfile()->GetWeakPtr();
  if (mode == kImportMode) {
    const base::FilePath source =
        base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(kSourceSwitch);
    if (source.empty()) {
      ReportDemo(mode, false, std::string("needs --") + kSourceSwitch + "=<profile folder>");
      return true;
    }
    FailDemoAfter(kImportMode, base::Seconds(120));
    ChromiumImportRequest request;
    request.profile_dir = source;
    request.items = kImportBookmarks | kImportHistory | kImportSearchShortcuts | kImportAddresses |
                    kImportOpenTabs | kImportExtensions;
    request.space_id = GetWindowSpaceId(browser);
    ImportChromiumProfile(browser->GetProfile(), request, base::BindOnce([](ImportResult result) {
                            // Counts only: never what was imported.
                            ReportDemo(kImportMode, true, Counts(result));
                          }));
    return true;
  }
  const base::FilePath home = ImportSupportFolder();
  if (!base::PathExists(home.AppendASCII("Arc/StorableSidebar.json"))) {
    ReportDemo(mode, false, "needs --lb-import-home=desktop/lb/test/data/import/home");
    return true;
  }
  if (mode == kDialogMode) {
    RunDialog(std::move(run));
    return true;
  }
  if (mode == kArcMode) {
    RunArc(std::move(run), home);
    return true;
  }
  if (!run->dir.CreateUniqueTempDir()) {
    ReportDemo(mode, false, "no temporary folder");
    return true;
  }
  if (mode == kFilesMode) {
    RunFiles(std::move(run), home);
    return true;
  }
  if (mode == kDiaMode) {
    RunDia(std::move(run), home);
    return true;
  }
  RunFixture(std::move(run), home);
  return true;
}

}  // namespace lb
