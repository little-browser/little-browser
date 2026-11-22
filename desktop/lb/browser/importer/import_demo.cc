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
#include "base/values.h"
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
#include "components/prefs/pref_service.h"
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
#include "lb/browser/importer/browser_import.h"
#include "lb/browser/importer/chromium_import.h"
#include "lb/browser/importer/cookie_decryptor.h"
#include "lb/browser/importer/import_sources.h"
#include "lb/browser/importer/other_import.h"
#include "lb/browser/spaces/space_model.h"
#include "lb/browser/ui/views/importer/import_dialog.h"
#include "lb/browser/ui/views/search/search_choice_dialog.h"
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
constexpr char kHomeSwitch[] = "lb-import-home";  // import_sources.cc reads it
constexpr char kFixtureMode[] = "import-fixture";
constexpr char kArcMode[] = "import-arc";
constexpr char kArcUnreadableMode[] = "import-arc-unreadable";
constexpr char kDialogMode[] = "import-dialog";
constexpr char kChoicesMode[] = "import-choices";
constexpr char kProgressMode[] = "import-progress";
constexpr char kFirstRunMode[] = "import-first-run";
constexpr char kImportMode[] = "import";
constexpr sql::Database::Tag kTag = "FirefoxImporter";
constexpr char kFilesMode[] = "import-files";
constexpr char kEdgeMode[] = "import-edge";
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

// The window's tabs as "url" (pinned ones "P url", grouped ones "url [group title]", ones in a
// pinned group "P url {group title}"), with ", collapsed" in the brackets when the group is.
std::vector<std::string> TabsOf(BrowserWindowInterface* browser) {
  std::vector<std::string> tabs;
  TabStripModel* model = browser->GetTabStripModel();
  SidebarController* controller = SidebarController::FromBrowser(browser);
  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    std::string entry = (model->IsTabPinned(i) ? "P " : "") + contents->GetVisibleURL().spec();
    if (std::optional<tab_groups::TabGroupId> group = model->GetTabGroupForTab(i)) {
      if (TabGroup* g = model->group_model()->GetTabGroup(*group)) {
        entry += " [" + base::UTF16ToUTF8(g->visual_data()->title()) +
                 (g->visual_data()->is_collapsed() ? ", collapsed" : "") + "]";
      }
    }
    if (const PinnedGroup* pinned_group =
            controller ? controller->pinned_groups().GroupOf(model->GetTabAtIndex(i)) : nullptr) {
      entry += " {" + base::UTF16ToUTF8(pinned_group->title) +
               (pinned_group->collapsed ? ", collapsed" : "") + "}";
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
                  first.extensions_not_from_store.size() == 1,
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
  run->Expect(Contains(tabs, "https://ci.example/ [Work, collapsed]") &&
                  Contains(tabs, "https://tracker.example/ [Work, collapsed]"),
              "CI and Tracker are in the Work group, collapsed: " + base::JoinString(tabs, ", "));

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

// The name of the profile in the folder `base`, or empty.
std::u16string ProfileNameOf(const std::string& base) {
  for (ProfileAttributesEntry* entry : g_browser_process->profile_manager()
                                           ->GetProfileAttributesStorage()
                                           .GetAllProfilesAttributes()) {
    if (entry->GetPath().BaseName().AsUTF8Unsafe() == base) {
      return entry->GetLocalProfileName();
    }
  }
  return std::u16string();
}

// Every profile as "folder=name", for a failure's log line.
std::string AllProfiles() {
  std::string profiles;
  for (ProfileAttributesEntry* entry : g_browser_process->profile_manager()
                                           ->GetProfileAttributesStorage()
                                           .GetAllProfilesAttributes()) {
    profiles += entry->GetPath().BaseName().AsUTF8Unsafe() + "=" +
                base::UTF16ToUTF8(entry->GetLocalProfileName()) + " ";
  }
  return profiles;
}

void ArcImported(std::unique_ptr<Run> run, ImportResult result) {
  Profile* profile = run->profile.get();
  BrowserWindowInterface* browser = run->browser.get();
  if (!profile || !browser) {
    return;
  }
  run->Expect(result.counts.spaces == 2 && result.counts.tabs == 6 && result.problems.empty(),
              "import: " + Counts(result));

  // Personal was empty, so it was filled, in this window: Favourites, the pinned tabs (the
  // "Reading" folder a pinned group in place, collapsed), then the today tab.
  std::vector<std::string> tabs = TabsOf(browser);
  std::erase_if(tabs,
                [](const std::string& t) { return t.find(".example/") == std::string::npos; });
  run->Expect(
      tabs == std::vector<std::string>{"P https://calendar.example/", "P https://mail.example/",
                                       "P https://blog.example/ {Reading, collapsed}",
                                       "P https://news.example/ {Reading, collapsed}",
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
  run->Expect(work && ProfileNameOf(work->profile) == u"Client",
              "Work is in a new profile called Client (Work's profile: " +
                  (work ? work->profile : std::string("none")) + "; profiles: " + AllProfiles() +
                  ")");
  const bookmarks::BookmarkNode* bar =
      BookmarkModelFactory::GetForBrowserContext(profile)->bookmark_bar_node();
  run->Expect(Child(bar, u"Arc docs") != nullptr,
              "Arc's default profile's bookmark is in the default profile");
  run->Expect(Child(bar, u"Client wiki") == nullptr,
              "the Client profile's bookmark stays out of the default profile");
  Report(std::move(run), "Arc's spaces, tabs, profile and bookmarks are in place");
}

// Arc whose sidebar file can't be read (an Arc update changed it, or it is cut short): its spaces
// can't be listed, so its profiles are, as for a Chrome-family browser, with the problem named.
void RunArcUnreadable(std::unique_ptr<Run> run, const base::FilePath& home) {
  FailDemoAfter(kArcUnreadableMode, base::Seconds(20));
  const base::FilePath copy = run->dir.GetPath().AppendASCII("home");
  if (!base::CopyDirectory(home, copy, /*recursive=*/true) ||
      !base::WriteFile(copy.AppendASCII("Arc/StorableSidebar.json"), "{ not json")) {
    ReportDemo(kArcUnreadableMode, false, "couldn't make the copy");
    return;
  }
  // Where FindImportSources looks (ImportSupportFolder); the switch's new value replaces the old.
  base::CommandLine::ForCurrentProcess()->AppendSwitchPath(kHomeSwitch, copy);
  FindImportSources(base::BindOnce(
      [](std::unique_ptr<Run> run, std::vector<ImportSource> sources) {
        const auto arc = std::ranges::find(sources, u"Arc", &ImportSource::browser);
        run->Expect(arc != sources.end(), "Arc is listed");
        if (arc != sources.end()) {
          std::vector<std::string> labels;
          for (const ImportChoice& choice : arc->choices) {
            labels.push_back(base::UTF16ToUTF8(choice.label));
          }
          run->Expect(arc->kind == ImportSourceKind::kChromium &&
                          arc->path.BaseName().value() == "User Data",
                      "Arc is imported as a Chrome-family browser");
          run->Expect(arc->choices.size() == 2,
                      "Arc's two profiles are listed: " + base::JoinString(labels, ", "));
          run->Expect(arc->problem.starts_with("Arc's spaces couldn't be read"),
                      "the problem is named: " + arc->problem);
        }
        Report(std::move(run), "Arc with an unreadable sidebar lists its profiles instead");
      },
      std::move(run)));
}

void RunArc(std::unique_ptr<Run> run, const base::FilePath& home) {
  FailDemoAfter(kArcMode, base::Seconds(40));
  BrowserImportRequest request;
  request.kind = ImportSourceKind::kArc;
  request.dir = home.AppendASCII("Arc");
  request.chosen = {"space-personal", "space-work"};
  request.items = kImportSpaces | kImportBookmarks;
  request.current_profile = run->profile->GetBaseName().AsUTF8Unsafe();
  request.browser = "Arc";
  ImportBrowser(request, base::BindOnce(&ArcImported, std::move(run)));
}

// --- import-edge -------------------------------------------------------------------------------

// Sets when every file in `dir` was last changed.
bool TouchFilesIn(const base::FilePath& dir, base::Time when) {
  bool ok = true;
  base::FileEnumerator files(dir, /*recursive=*/false, base::FileEnumerator::FILES);
  for (base::FilePath f = files.Next(); !f.empty(); f = files.Next()) {
    ok = base::TouchFile(f, when, when) && ok;
  }
  return ok;
}

// Copies the made-up Edge into `user_data` and writes the session of its "Profile 10" the way a
// browser moving to encrypted copies saves it: the plain files, and an encrypted copy of each
// saved some milliseconds later, here in the next second. The copies' contents stand in for
// encrypted ones; only their times matter.
bool PrepareEdge(const base::FilePath& home, const base::FilePath& user_data) {
  const base::FilePath fixture = home.AppendASCII("Microsoft Edge");
  if (!base::CopyDirectory(fixture, user_data.DirName(), /*recursive=*/true)) {
    return false;
  }
  const base::FilePath dir = user_data.AppendASCII("Profile 10");
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

// Where the demo's copy of Edge's user data folder goes.
base::FilePath EdgeUserData(const Run& run) {
  return run.dir.GetPath().AppendASCII("Microsoft Edge");
}

void EdgeImported(std::unique_ptr<Run> run, ImportResult result) {
  Profile* profile = run->profile.get();
  if (!profile) {
    return;
  }
  run->Expect(result.counts.bookmarks == 1 && result.counts.tabs == 3 &&
                  result.counts.spaces == 1 && result.problems.empty(),
              "Edge import: " + Counts(result));
  // "Studio" became a space of its own, with its open tabs, in a new profile of the same name.
  std::optional<Space> studio;
  bool has_tabs = false;
  for (const auto& [space, empty] : SidebarController::GetSpacesForImport()) {
    if (space.name == "Studio") {
      studio = space;
      has_tabs = !empty;
    }
  }
  run->Expect(
      studio && has_tabs && ProfileNameOf(studio->profile) == u"Studio",
      "Studio is a space with tabs in a profile called Studio (profiles: " + AllProfiles() + ")");
  // Its open tabs keep their kind in the new space, which no window shows yet: the pinned tab
  // pinned, the group a group, collapsed.
  const base::ListValue* saved =
      studio ? g_browser_process->local_state()->GetDict("lb.space_tabs").FindList(studio->id)
             : nullptr;
  std::vector<std::string> tabs;
  for (const SavedTab& t : saved ? SavedTabsFromList(*saved) : std::vector<SavedTab>()) {
    std::string entry = (t.pinned ? "P " : "") + t.url.spec();
    if (!t.group_id.empty()) {
      entry +=
          " [" + base::UTF16ToUTF8(t.group_title) + (t.group_collapsed ? ", collapsed" : "") + "]";
    }
    tabs.push_back(entry);
  }
  run->Expect(
      Contains(tabs, "P https://mail.example/") &&
          Contains(tabs, "https://ci.example/ [Work, collapsed]") &&
          Contains(tabs, "https://tracker.example/ [Work, collapsed]"),
      "Studio's tabs keep their kind, the group collapsed: " + base::JoinString(tabs, ", "));
  ProfileManager* manager = g_browser_process->profile_manager();
  Profile* studio_profile =
      studio ? manager->GetProfileByPath(manager->user_data_dir().AppendASCII(studio->profile))
             : nullptr;
  const bookmarks::BookmarkNode* studio_bar =
      studio_profile
          ? BookmarkModelFactory::GetForBrowserContext(studio_profile)->bookmark_bar_node()
          : nullptr;
  run->Expect(studio_bar && Child(studio_bar, u"Edge notes"), "Edge notes is in Studio's bar");
  // "Personal" wasn't picked, and nothing went into the current profile.
  const bookmarks::BookmarkNode* bar =
      BookmarkModelFactory::GetForBrowserContext(profile)->bookmark_bar_node();
  run->Expect(!Child(bar, u"Edge notes") && !Child(bar, u"Personal notes") &&
                  !(studio_bar && Child(studio_bar, u"Personal notes")),
              "only the picked profile came along");
  Report(std::move(run),
         "the picked Edge profile became a space and a profile; its plain files saved with "
         "encrypted copies were read");
}

void RunEdge(std::unique_ptr<Run> run, const base::FilePath& home) {
  FailDemoAfter(kEdgeMode, base::Seconds(40));
  base::ThreadPool::CreateSingleThreadTaskRunner({base::MayBlock()})
      ->PostTaskAndReplyWithResult(
          FROM_HERE, base::BindOnce(&PrepareEdge, home, EdgeUserData(*run)),
          base::BindOnce(
              [](std::unique_ptr<Run> run, bool ok) {
                Profile* profile = run->profile.get();
                if (!ok || !profile) {
                  ReportDemo(kEdgeMode, false, "couldn't build the Edge profile");
                  return;
                }
                BrowserImportRequest request;
                request.kind = ImportSourceKind::kChromium;
                request.dir = EdgeUserData(*run);
                request.chosen = {"Profile 10"};
                request.items = kImportSpaces | kImportBookmarks;
                request.current_profile = profile->GetBaseName().AsUTF8Unsafe();
                request.browser = "Edge";
                ImportBrowser(request, base::BindOnce(&EdgeImported, std::move(run)));
              },
              std::move(run)));
}

// --- import-dialog ----------------------------------------------------------------------------

// The open dialog's rows, for a log line.
std::string Rows(const std::optional<ImportDialogState>& state) {
  std::vector<std::string> rows;
  for (const std::u16string& row : state ? state->rows : std::vector<std::u16string>()) {
    rows.push_back(base::UTF16ToUTF8(row));
  }
  return base::JoinString(rows, ", ");
}

// Goes through the dialog's two steps: the browsers, then each one's profiles or spaces.
void CheckDialogSteps(Run& run) {
  std::optional<ImportDialogState> state = GetImportDialogStateForTesting();
  run.Expect(state && state->step == 1 && state->can_accept && state->rows.size() == 5 &&
                 std::ranges::count(state->rows, u"Edge") == 1 &&
                 state->rows[3] == u"A Safari export file…" &&
                 state->rows[4] == u"A bookmarks file…",
             "step 1 lists three browsers, each once, and the two files: " + Rows(state));

  // Chrome has one profile: none is listed, and Import can be pressed.
  run.Expect(ImportDialogNextForTesting(u"Chrome"), "Chrome can be picked");
  state = GetImportDialogStateForTesting();
  run.Expect(state && state->step == 2 && state->can_accept && state->rows.empty(),
             "Chrome's single profile isn't listed: " + Rows(state));

  // Back, then Arc: its spaces, Work with its profile's name.
  run.Expect(ImportDialogBackForTesting(), "Back goes to the browsers");
  state = GetImportDialogStateForTesting();
  run.Expect(state && state->step == 1, "Back shows the browsers");
  run.Expect(ImportDialogNextForTesting(u"Arc"), "Arc can be picked");
  state = GetImportDialogStateForTesting();
  run.Expect(state && state->step == 2 && state->can_accept &&
                 state->rows == std::vector<std::u16string>({u"Personal", u"Work — Client"}),
             "Arc's spaces: " + Rows(state));

  // Edge last: both its profiles, ticked; Import can be pressed.
  ImportDialogBackForTesting();
  run.Expect(ImportDialogNextForTesting(u"Edge"), "Edge can be picked");
  state = GetImportDialogStateForTesting();
  run.Expect(state && state->step == 2 && state->can_accept &&
                 state->rows == std::vector<std::u16string>({u"Studio", u"Personal"}),
             "Edge's profiles: " + Rows(state));
}

// After Import: waits for the summary, then checks the extensions list, grouped by the profile
// each goes into (the snapshot shows it).
// The import dialog's tallest: its body (at most 480) with the title, buttons and margins (about
// 605 here; 740 without the cap).
constexpr int kMaxDialogHeight = 640;

void CheckExtensionsStep(std::unique_ptr<Run> run, int tries) {
  std::optional<ImportDialogState> state = GetImportDialogStateForTesting();
  if ((!state || state->step != 3) && tries > 0) {
    PostDemoTask(base::BindOnce(&CheckExtensionsStep, std::move(run), tries - 1),
                 base::Milliseconds(500));
    return;
  }
  std::vector<std::string> list;
  for (const std::u16string& line : state ? state->extensions : std::vector<std::u16string>()) {
    list.push_back(base::UTF16ToUTF8(line));
  }
  // Studio's "Made-up Paused" was turned off in Edge: it starts unticked.
  std::vector<std::u16string> expected = {u"Studio:", u"Made-up Notes",
                                          u"Made-up Paused (unticked)", u"Made-up Reader"};
  for (int i = 1; i <= 6; ++i) {
    expected.push_back(u"Made-up Tool " + base::NumberToString16(i));
  }
  expected.insert(expected.end(), {u"Personal:", u"Made-up Reader"});
  for (int i = 1; i <= 5; ++i) {
    expected.push_back(u"Made-up Tool " + base::NumberToString16(i));
  }
  run->Expect(state && state->step == 3 && state->extensions == expected,
              "extensions grouped by profile: " + base::JoinString(list, ", "));
  // Fifteen extensions don't fit: the body scrolls and the dialog keeps a bounded height.
  run->Expect(state && state->scrolls && state->dialog_height <= kMaxDialogHeight,
              "the long list scrolls inside a dialog " +
                  base::NumberToString(state ? state->dialog_height : 0) + " high (at most " +
                  base::NumberToString(kMaxDialogHeight) + ")");
  Report(std::move(run),
         "lists Arc, Chrome and Edge, then each one's profiles or spaces; Import works; the "
         "extensions are grouped by profile and the long list scrolls (dialog " +
             base::NumberToString(state ? state->dialog_height : 0) + " high)");
}

// Runs `then` once the dialog shows its browsers (it looks for them first), or gives up.
void WhenBrowsersShown(std::unique_ptr<Run> run,
                       base::OnceCallback<void(std::unique_ptr<Run>)> then,
                       int tries = 100) {
  std::optional<ImportDialogState> state = GetImportDialogStateForTesting();
  if ((!state || state->step != 1) && tries > 0) {
    PostDemoTask(base::BindOnce(&WhenBrowsersShown, std::move(run), std::move(then), tries - 1),
                 base::Milliseconds(100));
    return;
  }
  std::move(then).Run(std::move(run));
}

// Opens the dialog and steps to Chrome's "what comes along" step, then holds it, so
// --lb-ui-snapshot-dialog can capture the item rows (each with its icon).
void RunChoices(std::unique_ptr<Run> run) {
  FailDemoAfter(kChoicesMode, base::Seconds(20));
  FindImportSources(base::BindOnce(
      [](std::unique_ptr<Run> run, std::vector<ImportSource>) {
        if (!run->browser) {
          return;
        }
        chrome::ExecuteCommand(run->browser.get(), IDC_IMPORT_SETTINGS);
        WhenBrowsersShown(std::move(run), base::BindOnce([](std::unique_ptr<Run> run) {
                            const bool stepped = ImportDialogNextForTesting(u"Chrome");
                            std::optional<ImportDialogState> state =
                                GetImportDialogStateForTesting();
                            Report(std::move(run), stepped && state && state->step == 2
                                                       ? "the choices step is open"
                                                       : "couldn't open the choices step");
                          }));
      },
      std::move(run)));
}

// The items the importing page must report as it brings Edge's data over (cookies are off by
// default, open tabs aren't a row).
constexpr uint32_t kProgressItems = kImportSpaces | kImportBookmarks | kImportHistory |
                                    kImportSearchShortcuts | kImportAddresses | kImportExtensions;

// Whether the bookmarks bar of a loaded profile shown as `profile` holds `title`.
bool BarHas(const std::u16string& profile, const std::u16string& title) {
  for (Profile* p : g_browser_process->profile_manager()->GetLoadedProfiles()) {
    const bookmarks::BookmarkModel* model = BookmarkModelFactory::GetForBrowserContext(p);
    if (ProfileDisplayName(p->GetBaseName().AsUTF8Unsafe()) == profile && model &&
        model->loaded() && Child(model->bookmark_bar_node(), title)) {
      return true;
    }
  }
  return false;
}

// Waits until the importing page has reported every expected item (each spinner became a check),
// then reports. A check means the item came over: from the moment Bookmarks is reported, both
// Edge profiles' bookmarks must be in their profiles (`bookmarks_there` keeps what was seen).
void CheckProgress(std::unique_ptr<Run> run, int tries, bool bookmarks_there) {
  std::optional<ImportDialogState> state = GetImportDialogStateForTesting();
  const uint32_t seen = state ? state->imported_seen : 0;
  if (seen & kImportBookmarks) {
    bookmarks_there = bookmarks_there && BarHas(u"Studio", u"Edge notes") &&
                      BarHas(u"Personal", u"Personal notes");
  }
  const bool all_reported = (seen & kProgressItems) == kProgressItems;
  if (!all_reported && tries > 0) {
    PostDemoTask(base::BindOnce(&CheckProgress, std::move(run), tries - 1, bookmarks_there),
                 base::Milliseconds(20));
    return;
  }
  run->Expect(all_reported,
              "every item's spinner became a check (reported=" + base::NumberToString(seen) + ")");
  run->Expect(
      bookmarks_there,
      std::string("Bookmarks was checked only once both profiles' bookmarks were in (now: ") +
          "Studio " + (BarHas(u"Studio", u"Edge notes") ? "has" : "lacks") +
          " its bookmark, Personal " + (BarHas(u"Personal", u"Personal notes") ? "has" : "lacks") +
          " its bookmark; profiles: " + AllProfiles() + ")");
  Report(std::move(run), "the importing page reported each item once it had come over");
}

void StartProgress(std::unique_ptr<Run> run);

// A copy of the made-up home where Edge's "Studio" bookmarks file and its encrypted copy were saved
// together (as a browser saves them), so the plain one is read; a checkout gives them any times.
bool CopyHomeWithEdgeBookmarksFresh(const base::FilePath& home, const base::FilePath& copy) {
  if (!base::CopyDirectory(home, copy, /*recursive=*/true)) {
    return false;
  }
  const base::FilePath studio = copy.AppendASCII("Microsoft Edge/Profile 10");
  const base::Time now = base::Time::Now();
  return base::TouchFile(studio.AppendASCII("Bookmarks"), now, now) &&
         base::TouchFile(studio.AppendASCII("EncryptedBookmarks2"), now, now);
}

// Opens the dialog and imports Edge's two profiles, checking each item's row is reported as it is
// brought over (--lb-ui-snapshot-dialog captures the per-item rows while it runs).
void RunProgress(std::unique_ptr<Run> run, const base::FilePath& home) {
  FailDemoAfter(kProgressMode, base::Seconds(30));
  if (!run->dir.CreateUniqueTempDir()) {
    ReportDemo(kProgressMode, false, "no temporary folder");
    return;
  }
  const base::FilePath copy = run->dir.GetPath().AppendASCII("home");
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()}, base::BindOnce(&CopyHomeWithEdgeBookmarksFresh, home, copy),
      base::BindOnce(
          [](std::unique_ptr<Run> run, base::FilePath copy, bool ok) {
            if (!ok) {
              ReportDemo(kProgressMode, false, "couldn't make the copy");
              return;
            }
            // Where FindImportSources looks; the switch's new value replaces the old.
            base::CommandLine::ForCurrentProcess()->AppendSwitchPath(kHomeSwitch, copy);
            StartProgress(std::move(run));
          },
          std::move(run), copy));
}

void StartProgress(std::unique_ptr<Run> run) {
  FindImportSources(base::BindOnce(
      [](std::unique_ptr<Run> run, std::vector<ImportSource>) {
        if (!run->browser) {
          return;
        }
        chrome::ExecuteCommand(run->browser.get(), IDC_IMPORT_SETTINGS);
        WhenBrowsersShown(std::move(run), base::BindOnce([](std::unique_ptr<Run> run) {
                            run->Expect(ImportDialogNextForTesting(u"Edge"), "Edge can be picked");
                            run->Expect(ImportDialogImportForTesting(), "Import can be pressed");
                            CheckProgress(std::move(run), 600, /*bookmarks_there=*/true);
                          }));
      },
      std::move(run)));
}

// On the first run (run without --no-first-run) the dialog opens by itself on its first step,
// with the introduction and "Not now", no question before it.
void CheckFirstRun(std::unique_ptr<Run> run, int tries) {
  std::optional<ImportDialogState> state = GetImportDialogStateForTesting();
  if ((!state || state->step != 1) && tries > 0) {
    PostDemoTask(base::BindOnce(&CheckFirstRun, std::move(run), tries - 1),
                 base::Milliseconds(250));
    return;
  }
  run->Expect(state && state->step == 1, "the import dialog opened by itself on its first step");
  run->Expect(state && state->title == u"Bring your tabs and bookmarks from another browser" &&
                  state->cancel_label == u"Not now",
              "it has the first-run title and Not now (" +
                  base::UTF16ToUTF8(state ? state->title + u" / " + state->cancel_label : u"") +
                  ")");
  run->Expect(state && std::ranges::count(state->rows, u"Arc") == 1 &&
                  std::ranges::count(state->rows, u"Chrome") == 1 &&
                  std::ranges::count(state->rows, u"Edge") == 1,
              "it lists the browsers: " + Rows(state));
  run->Expect(!IsSearchChoiceOpenForTesting(), "the next question waits while it is open");
  // Import asked for again (the command bar) while it is open waits for it to close too.
  auto import_closed = std::make_shared<bool>(false);
  if (run->browser) {
    ShowImportDialog(
        run->browser.get(),
        base::BindOnce([](std::shared_ptr<bool> closed) { *closed = true; }, import_closed));
  }
  run->Expect(!*import_closed, "a second Import waits for the open dialog");
  run->Expect(ImportDialogCancelForTesting(), "Not now closes it");
  PostDemoTask(base::BindOnce(
                   [](std::unique_ptr<Run> run, std::shared_ptr<bool> import_closed) {
                     run->Expect(*import_closed, "closing it lets the second Import go on");
                     run->Expect(IsSearchChoiceOpenForTesting(),
                                 "then the search engine question comes");
                     Report(std::move(run),
                            "first run opens the import dialog itself, with its introduction; "
                            "what waits for it goes on once it closes");
                   },
                   std::move(run), import_closed),
               base::Seconds(2));
}

void RunDialog(std::unique_ptr<Run> run) {
  FailDemoAfter(kDialogMode, base::Seconds(40));
  FindImportSources(base::BindOnce(
      [](std::unique_ptr<Run> run, std::vector<ImportSource> sources) {
        std::vector<std::string> labels;
        for (const ImportSource& s : sources) {
          labels.push_back(base::UTF16ToUTF8(s.browser));
        }
        run->Expect(labels.size() == 3 && Contains(labels, "Arc") && Contains(labels, "Chrome") &&
                        Contains(labels, "Edge"),
                    "browsers: " + base::JoinString(labels, ", "));
        if (!run->browser) {
          return;
        }
        // The app menu's command, which Settings' row and the command bar run too.
        chrome::ExecuteCommand(run->browser.get(), IDC_IMPORT_SETTINGS);
        WhenBrowsersShown(std::move(run), base::BindOnce([](std::unique_ptr<Run> run) {
                            CheckDialogSteps(*run);
                            run->Expect(ImportDialogImportForTesting(), "Import can be pressed");
                            CheckExtensionsStep(std::move(run), 40);
                          }));
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

// A bookmarks file of one bookmark in the bar.
std::string OneBookmarkFile(const std::string& url, const std::string& title) {
  return "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n<DL><p>\n"
         "<DT><H3 PERSONAL_TOOLBAR_FOLDER=\"true\">Bookmarks bar</H3>\n<DL><p>\n"
         "<DT><A HREF=\"" +
         url + "\">" + title + "</A>\n</DL><p>\n</DL><p>\n";
}

// Several sources in one import (as Firefox's chosen profiles come), one after another: both of
// two bookmarks files come in, and the summary counts both.
void ImportTwoFiles(std::unique_ptr<Run> run) {
  Profile* profile = run->profile.get();
  BrowserWindowInterface* browser = run->browser.get();
  const base::FilePath dir = run->dir.GetPath();
  if (!profile || !browser ||
      !base::WriteFile(dir.AppendASCII("one.html"),
                       OneBookmarkFile("https://first-file.example/", "First file")) ||
      !base::WriteFile(dir.AppendASCII("two.html"),
                       OneBookmarkFile("https://second-file.example/", "Second file"))) {
    ReportDemo(kFilesMode, false, "couldn't write the two bookmarks files");
    return;
  }
  std::vector<user_data_importer::SourceProfile> sources(2);
  for (size_t i = 0; i < sources.size(); ++i) {
    sources[i].importer_type = user_data_importer::TYPE_BOOKMARKS_FILE;
    sources[i].source_path = dir.AppendASCII(i == 0 ? "one.html" : "two.html");
    sources[i].services_supported = user_data_importer::FAVORITES;
  }
  ImportWithChromiumImporter(
      profile, std::move(sources), kImportBookmarks, browser->GetWindow()->GetNativeWindow(),
      base::BindOnce(
          [](std::unique_ptr<Run> run, ImportResult result) {
            Profile* profile = run->profile.get();
            if (!profile) {
              return;
            }
            run->Expect(result.counts.bookmarks == 2 && result.problems.empty(),
                        "two files at once: " + Counts(result));
            const bookmarks::BookmarkNode* bar =
                BookmarkModelFactory::GetForBrowserContext(profile)->bookmark_bar_node();
            run->Expect(Child(bar, u"First file") && Child(bar, u"Second file"),
                        "both files' bookmarks are in the bar");
            Report(std::move(run),
                   "a bookmarks file and Safari's export came in, merged; two files at once "
                   "came in one after another");
          },
          std::move(run)));
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
                       ImportTwoFiles(std::move(run));
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
  if (mode != kFixtureMode && mode != kArcMode && mode != kArcUnreadableMode &&
      mode != kDialogMode && mode != kFilesMode && mode != kEdgeMode && mode != kChoicesMode &&
      mode != kProgressMode && mode != kFirstRunMode && mode != kImportMode) {
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
  if (mode == kChoicesMode) {
    RunChoices(std::move(run));
    return true;
  }
  if (mode == kProgressMode) {
    RunProgress(std::move(run), home);
    return true;
  }
  if (mode == kFirstRunMode) {
    FailDemoAfter(kFirstRunMode, base::Seconds(30));
    CheckFirstRun(std::move(run), 80);
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
  if (mode == kArcUnreadableMode) {
    RunArcUnreadable(std::move(run), home);
    return true;
  }
  if (mode == kEdgeMode) {
    RunEdge(std::move(run), home);
    return true;
  }
  RunFixture(std::move(run), home);
  return true;
}

}  // namespace lb
