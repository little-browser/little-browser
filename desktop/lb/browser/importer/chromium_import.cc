// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/chromium_import.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/barrier_callback.h"
#include "base/callback_list.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/numerics/safe_conversions.h"
#include "base/scoped_observation.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/importer/profile_writer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "components/autofill/core/browser/data_manager/addresses/address_data_manager.h"
#include "components/autofill/core/browser/data_manager/personal_data_manager.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_profile.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_structured_address_component.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/bookmarks/browser/base_bookmark_model_observer.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/history/core/browser/history_backend.h"
#include "components/history/core/browser/history_database.h"
#include "components/history/core/browser/history_db_task.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/url_row.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/storage_partition.h"
#include "extensions/browser/extension_registry.h"
#include "lb/browser/importer/chromium_profile_reader.h"
#include "lb/browser/spaces/space_model.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "lb/core_bridge/lib.rs.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_access_result.h"
#include "net/cookies/cookie_constants.h"
#include "net/cookies/cookie_options.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "url/gurl.h"

namespace lb {

namespace {

using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;

// ADDRESS_HOME_COUNTRY: the field holding an address's country code.
constexpr int64_t kCountryField = 36;

// The three places of the bookmark model, in lb::core::FlatBookmark's order.
std::vector<const BookmarkNode*> Places(BookmarkModel* model) {
  return {model->bookmark_bar_node(), model->other_node(), model->mobile_node()};
}

// The existing bookmarks as lb-core's flat list.
rust::Vec<core::FlatBookmark> FlattenModel(BookmarkModel* model) {
  rust::Vec<core::FlatBookmark> out;
  auto walk = [&out](auto& self, const BookmarkNode* node, uint8_t place, size_t depth) -> void {
    for (const auto& child : node->children()) {
      const bool folder = child->is_folder();
      out.push_back(core::FlatBookmark{place, depth, folder, base::UTF16ToUTF8(child->GetTitle()),
                                       folder ? std::string() : child->url().spec()});
      if (folder) {
        self(self, child.get(), place, depth + 1);
      }
    }
  };
  std::vector<const BookmarkNode*> places = Places(model);
  for (size_t place = 0; place < places.size(); ++place) {
    walk(walk, places[place], static_cast<uint8_t>(place), 0);
  }
  return out;
}

// The first folder called `title` directly in `parent`, or a new one at its end.
const BookmarkNode* FolderIn(BookmarkModel* model,
                             const BookmarkNode* parent,
                             const std::u16string& title) {
  for (const auto& child : parent->children()) {
    if (child->is_folder() && child->GetTitle() == title) {
      return child.get();
    }
  }
  return model->AddFolder(parent, parent->children().size(), title);
}

// Adds lb-core's merge result (folders to find or make, links to add at the end of their
// folder) to the model. Returns the number of links added.
size_t AddBookmarks(BookmarkModel* model, const rust::Vec<core::FlatBookmark>& added) {
  std::vector<const BookmarkNode*> places = Places(model);
  size_t links = 0;
  std::vector<const BookmarkNode*> open;  // open[d]: the folder items at depth d go into
  uint8_t place = 255;
  for (const core::FlatBookmark& b : added) {
    if (b.place != place || open.empty()) {
      place = b.place;
      open.assign(1, places[std::min<size_t>(place, places.size() - 1)]);
    }
    open.resize(std::min(open.size(), b.depth + 1));
    const BookmarkNode* parent = open.back();
    const std::u16string title = base::UTF8ToUTF16(std::string(b.title));
    if (b.is_folder) {
      open.push_back(FolderIn(model, parent, title));
      continue;
    }
    const GURL url(std::string(b.url));
    if (url.is_valid()) {
      model->AddURL(parent, parent->children().size(), title, url);
      ++links;
    }
  }
  return links;
}

// Problem lines (shown in the import summary).
constexpr char kProfileClosed[] = "The profile closed before the import finished";
constexpr char kAddressesOff[] = "Addresses: saving addresses is turned off in Settings";
constexpr char kAddressesNotLoaded[] = "Addresses: saved addresses didn't load, so none were added";
constexpr char kNoKeychainKey[] = "Cookies: the keychain didn't give the key, so they were skipped";

// How long to wait for saved addresses to load before going on without them.
constexpr base::TimeDelta kAddressLoadLimit = base::Seconds(20);

// Adds the pages history doesn't have yet, or has with an older visit, on history's own
// sequence, and reports how many that was. A page already there with this visit or a later one
// is left alone, so importing twice adds nothing.
class AddPagesTask : public history::HistoryDBTask {
 public:
  AddPagesTask(history::URLRows rows,
               history::VisitSource source,
               base::OnceCallback<void(size_t)> done)
      : rows_(std::move(rows)), source_(source), done_(std::move(done)) {}

  bool RunOnDBThread(history::HistoryBackend* backend, history::HistoryDatabase* db) override {
    history::URLRows add;
    for (const history::URLRow& row : rows_) {
      history::URLRow existing;
      if (db->GetRowForURL(row.url(), &existing) && existing.last_visit() >= row.last_visit()) {
        continue;
      }
      add.push_back(row);
    }
    added_ = add.size();
    if (!add.empty()) {
      backend->AddPagesWithDetails(add, source_);
    }
    return true;
  }

  void DoneRunOnMainThread() override { std::move(done_).Run(added_); }

 private:
  history::URLRows rows_;
  history::VisitSource source_;
  base::OnceCallback<void(size_t)> done_;
  size_t added_ = 0;
};

// Writes one profile's data, waiting for each service to load first; deletes itself when done.
// If the profile goes away first, it stops and reports that as a problem.
class Writer : public bookmarks::BaseBookmarkModelObserver,
               public autofill::AddressDataManager::Observer,
               public ProfileObserver {
 public:
  Writer(Profile* profile,
         ChromiumProfileData data,
         std::string space_id,
         base::OnceCallback<void(ImportResult)> done)
      : profile_(profile),
        data_(std::move(data)),
        space_id_(std::move(space_id)),
        done_(std::move(done)) {
    result_.problems = data_.problems;
    for (const core::StoreExtension& e : data_.extensions) {
      ExtensionToInstall extension;
      extension.id = std::string(e.id);
      extension.name = base::UTF8ToUTF16(std::string(e.name));
      extension.profile = profile->GetWeakPtr();
      extension.profile_name = profile->GetProfileUserName().empty()
                                   ? base::UTF8ToUTF16(profile->GetBaseName().AsUTF8Unsafe())
                                   : base::UTF8ToUTF16(profile->GetProfileUserName());
      result_.extensions.push_back(std::move(extension));
    }
    result_.extensions_not_from_store = data_.extensions_not_from_store;
    profile_observation_.Observe(profile);
  }

  void Start() { WriteBookmarks(); }

 private:
  ~Writer() override = default;

  void WriteBookmarks() {
    BookmarkModel* model = BookmarkModelFactory::GetForBrowserContext(profile_);
    if (data_.bookmarks.empty() || !model) {
      WriteHistory();
      return;
    }
    if (!model->loaded()) {
      bookmark_observation_.Observe(model);
      return;  // BookmarkModelLoaded continues.
    }
    bookmark_observation_.Reset();
    result_.counts.bookmarks = MergeIntoBookmarks(profile_, data_.bookmarks);
    WriteHistory();
  }

  // bookmarks::BaseBookmarkModelObserver:
  void BookmarkModelLoaded(bool ids_reassigned) override { WriteBookmarks(); }
  void BookmarkModelChanged() override {}

  void WriteHistory() {
    history::URLRows rows;
    for (const core::HistoryPage& page : data_.history) {
      history::URLRow row{GURL(std::string(page.url))};
      if (!row.url().is_valid()) {
        continue;
      }
      row.set_title(base::UTF8ToUTF16(std::string(page.title)));
      row.set_visit_count(base::saturated_cast<int>(page.visit_count));
      row.set_typed_count(base::saturated_cast<int>(page.typed_count));
      row.set_last_visit(base::Time::UnixEpoch() + base::Microseconds(page.last_visit_unix_micros));
      rows.push_back(std::move(row));
    }
    AddHistoryPages(profile_, std::move(rows), history::SOURCE_OS_MIGRATION_IMPORTED,
                    base::BindOnce(&Writer::HistoryWritten, weak_factory_.GetWeakPtr()));
  }

  void HistoryWritten(size_t pages) {
    result_.counts.history = pages;
    WriteSearchShortcuts();
  }

  void WriteSearchShortcuts() {
    TemplateURLService* service = TemplateURLServiceFactory::GetForProfile(profile_);
    if (data_.search_shortcuts.empty() || !service) {
      WriteAddresses();
      return;
    }
    if (!service->loaded()) {
      template_url_loaded_ = service->RegisterOnLoadedCallback(
          base::BindOnce(&Writer::WriteSearchShortcuts, weak_factory_.GetWeakPtr()));
      service->Load();
      return;
    }
    TemplateURLService::OwnedTemplateURLVector urls;
    for (const core::SearchShortcut& s : data_.search_shortcuts) {
      TemplateURLData d;
      d.SetShortName(base::UTF8ToUTF16(std::string(s.name)));
      d.SetKeyword(base::UTF8ToUTF16(std::string(s.keyword)));
      d.SetURL(std::string(s.url));
      d.safe_for_autoreplace = false;
      urls.push_back(std::make_unique<TemplateURL>(d));
    }
    const size_t before = service->GetTemplateURLs().size();
    writer()->AddKeywords(std::move(urls), /*unique_on_host_and_path=*/true);
    result_.counts.search_shortcuts = service->GetTemplateURLs().size() - before;
    WriteAddresses();
  }

  void WriteAddresses() {
    if (addresses_done_) {
      return;
    }
    autofill::PersonalDataManager* pdm =
        autofill::PersonalDataManagerFactory::GetForBrowserContext(profile_);
    if (data_.addresses.empty() || !pdm) {
      AddressesDone();
      return;
    }
    if (!autofill::prefs::IsAutofillProfileEnabled(profile_->GetPrefs())) {
      // Chromium would drop them without a word.
      result_.problems.push_back(kAddressesOff);
      AddressesDone();
      return;
    }
    autofill::AddressDataManager& adm = pdm->address_data_manager();
    if (!adm.has_initial_load_finished()) {
      if (!address_observation_.IsObserving()) {
        address_observation_.Observe(&adm);
        // A web database that fails to load never reports in: don't wait for ever.
        base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE, base::BindOnce(&Writer::AddressesTimedOut, weak_factory_.GetWeakPtr()),
            kAddressLoadLimit);
      }
      return;  // OnAddressDataChanged continues.
    }
    address_observation_.Reset();

    const rust::Vec<int64_t> types = core::address_field_types();
    rust::Vec<core::Address> existing;
    for (const autofill::AutofillProfile* saved : adm.GetProfiles()) {
      core::Address a;
      for (int64_t type : types) {
        std::u16string value = saved->GetRawInfo(static_cast<autofill::FieldType>(type));
        if (!value.empty()) {
          a.fields.push_back(core::AddressField{type, base::UTF16ToUTF8(value)});
        }
      }
      existing.push_back(std::move(a));
    }
    for (const core::Address& address : core::new_addresses(existing, std::move(data_.addresses))) {
      std::string country;
      for (const core::AddressField& f : address.fields) {
        if (f.field_type == kCountryField) {
          country = std::string(f.value);
        }
      }
      autofill::AutofillProfile profile{autofill::AddressCountryCode(country)};
      for (const core::AddressField& f : address.fields) {
        profile.SetRawInfoWithVerificationStatus(static_cast<autofill::FieldType>(f.field_type),
                                                 base::UTF8ToUTF16(std::string(f.value)),
                                                 autofill::VerificationStatus::kObserved);
      }
      profile.FinalizeAfterImport();
      adm.AddProfile(profile);
      ++result_.counts.addresses;
    }
    AddressesDone();
  }

  void AddressesTimedOut() {
    if (addresses_done_) {
      return;
    }
    address_observation_.Reset();
    result_.problems.push_back(kAddressesNotLoaded);
    AddressesDone();
  }

  void AddressesDone() {
    addresses_done_ = true;
    WriteOpenTabs();
  }

  void WriteOpenTabs() {
    if (!data_.open_tabs.empty() && !space_id_.empty()) {
      std::vector<SavedTab> tabs;
      for (const core::OpenTab& t : data_.open_tabs) {
        SavedTab tab;
        tab.url = GURL(std::string(t.url));
        if (!tab.url.is_valid()) {
          continue;
        }
        tab.title = base::UTF8ToUTF16(std::string(t.title));
        tab.pinned = t.pinned;
        tab.group_id = std::string(t.group);
        tab.group_title = base::UTF8ToUTF16(std::string(t.group_title));
        tab.group_color = t.group_color;
        tabs.push_back(std::move(tab));
      }
      result_.counts.tabs = tabs.size();
      SidebarController::AddImportedTabs(space_id_, tabs);
    }
    WriteCookies();
  }

  void WriteCookies() {
    if (data_.cookies.empty()) {
      Finish();
      return;
    }
    network::mojom::CookieManager* manager =
        profile_->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess();
    auto time = [](int64_t micros) { return base::Time::UnixEpoch() + base::Microseconds(micros); };
    auto set = base::BarrierCallback<bool>(
        data_.cookies.size(), base::BindOnce(&Writer::CookiesWritten, weak_factory_.GetWeakPtr()));
    for (const core::Cookie& c : data_.cookies) {
      // lb-core has already mapped the stored enum values into range.
      std::unique_ptr<net::CanonicalCookie> cookie = net::CanonicalCookie::FromStorage(
          std::string(c.name), std::string(c.value), std::string(c.host_key), std::string(c.path),
          time(c.creation_unix_micros),
          c.expires_unix_micros ? time(c.expires_unix_micros) : base::Time(),
          time(c.last_access_unix_micros), time(c.creation_unix_micros), c.is_secure, c.is_httponly,
          static_cast<net::CookieSameSite>(c.samesite),
          static_cast<net::CookiePriority>(c.priority), std::nullopt,
          static_cast<net::CookieSourceScheme>(c.source_scheme), static_cast<int>(c.source_port),
          net::CookieSourceType::kOther, net::CanonicalCookieFromStorageCallSite::kCookieManager);
      if (!cookie) {
        set.Run(false);
        continue;
      }
      std::string host(c.host_key);
      if (!host.empty() && host.front() == '.') {
        host.erase(0, 1);
      }
      const GURL source((c.is_secure ? "https://" : "http://") + host + std::string(c.path));
      // A reply that never comes (the network service went away) counts as not written.
      manager->SetCanonicalCookie(
          *cookie, source, net::CookieOptions::MakeAllInclusive(),
          base::BindOnce(
              [](base::OnceCallback<void(bool)> written, net::CookieAccessResult result) {
                std::move(written).Run(result.status.IsInclude());
              },
              mojo::WrapCallbackWithDefaultInvokeIfNotRun(base::OnceCallback<void(bool)>(set),
                                                          false)));
    }
  }

  void CookiesWritten(std::vector<bool> written) {
    result_.counts.cookies = std::ranges::count(written, true);
    Finish();
  }

  // autofill::AddressDataManager::Observer:
  void OnAddressDataChanged() override {
    if (address_observation_.IsObserving() &&
        address_observation_.GetSource()->has_initial_load_finished()) {
      WriteAddresses();
    }
  }

  // ProfileObserver:
  void OnProfileWillBeDestroyed(Profile* profile) override {
    result_.problems.push_back(kProfileClosed);
    Finish();
  }

  void Finish() {
    base::OnceCallback<void(ImportResult)> done = std::move(done_);
    ImportResult result = std::move(result_);
    delete this;
    std::move(done).Run(std::move(result));
  }

  ProfileWriter* writer() {
    if (!writer_) {
      writer_ = base::MakeRefCounted<ProfileWriter>(profile_);
    }
    return writer_.get();
  }

  raw_ptr<Profile> profile_;
  ChromiumProfileData data_;
  std::string space_id_;
  base::OnceCallback<void(ImportResult)> done_;
  ImportResult result_;
  bool addresses_done_ = false;
  scoped_refptr<ProfileWriter> writer_;
  base::CallbackListSubscription template_url_loaded_;
  base::ScopedObservation<BookmarkModel, bookmarks::BookmarkModelObserver> bookmark_observation_{
      this};
  base::ScopedObservation<autofill::AddressDataManager, autofill::AddressDataManager::Observer>
      address_observation_{this};
  base::ScopedObservation<Profile, ProfileObserver> profile_observation_{this};
  base::WeakPtrFactory<Writer> weak_factory_{this};
};

}  // namespace

ExtensionToInstall::ExtensionToInstall() = default;
ExtensionToInstall::ExtensionToInstall(const ExtensionToInstall&) = default;
ExtensionToInstall& ExtensionToInstall::operator=(const ExtensionToInstall&) = default;
ExtensionToInstall::~ExtensionToInstall() = default;

ImportResult::ImportResult() = default;
ImportResult::ImportResult(const ImportResult&) = default;
ImportResult& ImportResult::operator=(const ImportResult&) = default;
ImportResult::~ImportResult() = default;

ImportResult& ImportResult::operator+=(const ImportResult& other) {
  counts += other.counts;
  problems.insert(problems.end(), other.problems.begin(), other.problems.end());
  // An extension comes once per profile, even when two sources (two Arc profiles that map to
  // one profile here) both have it.
  for (const ExtensionToInstall& e : other.extensions) {
    if (std::ranges::none_of(extensions, [&](const ExtensionToInstall& known) {
          return known.id == e.id && known.profile.get() == e.profile.get();
        })) {
      extensions.push_back(e);
    }
  }
  extensions_not_from_store += other.extensions_not_from_store;
  return *this;
}

ChromiumImportRequest::ChromiumImportRequest() = default;
ChromiumImportRequest::ChromiumImportRequest(const ChromiumImportRequest&) = default;
ChromiumImportRequest& ChromiumImportRequest::operator=(const ChromiumImportRequest&) = default;
ChromiumImportRequest::~ChromiumImportRequest() = default;

void AddHistoryPages(Profile* profile,
                     history::URLRows rows,
                     history::VisitSource source,
                     base::OnceCallback<void(size_t)> done) {
  history::HistoryService* history =
      HistoryServiceFactory::GetForProfile(profile, ServiceAccessType::EXPLICIT_ACCESS);
  if (!history || rows.empty()) {
    std::move(done).Run(0);
    return;
  }
  // Never cancelled: the task always reports back.
  static base::NoDestructor<base::CancelableTaskTracker> tracker;
  history->ScheduleDBTask(FROM_HERE,
                          std::make_unique<AddPagesTask>(std::move(rows), source, std::move(done)),
                          tracker.get());
}

size_t MergeIntoBookmarks(Profile* profile, const rust::Vec<core::FlatBookmark>& bookmarks) {
  BookmarkModel* model = BookmarkModelFactory::GetForBrowserContext(profile);
  if (!model || !model->loaded() || bookmarks.empty()) {
    return 0;
  }
  return AddBookmarks(model, core::merge_bookmarks(FlattenModel(model), bookmarks));
}

void ImportChromiumProfile(Profile* profile,
                           const ChromiumImportRequest& request,
                           base::OnceCallback<void(ImportResult)> done) {
  ChromiumReadOptions options;
  options.now = base::Time::Now();
  options.oldest_visit = options.now - base::Days(history::HistoryBackend::kExpireDaysThreshold);
  if (request.items & kImportExtensions) {
    for (const auto& extension :
         extensions::ExtensionRegistry::Get(profile)->GenerateInstalledExtensionsSet()) {
      options.installed_extensions.push_back(extension->id());
    }
  }
  // A single-threaded runner: Chromium's session backend (open tabs) needs one.
  base::ThreadPool::CreateSingleThreadTaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE})
      ->PostTaskAndReplyWithResult(
          FROM_HERE,
          base::BindOnce(
              [](ChromiumImportRequest request, ChromiumReadOptions options) {
                if (request.items & kImportCookies) {
                  // The keychain asks the user first; declining leaves the cookies behind. A
                  // caller importing several profiles of one browser reads it once.
                  options.keychain_password =
                      request.keychain_password
                          ? request.keychain_password
                          : ReadImportKeychainPassword(request.keychain_service,
                                                       request.keychain_account);
                }
                ChromiumProfileData data =
                    ReadChromiumProfile(request.profile_dir, request.items, options);
                if ((request.items & kImportCookies) && !options.keychain_password) {
                  data.problems.push_back(kNoKeychainKey);
                }
                return data;
              },
              request, std::move(options)),
          base::BindOnce(
              [](base::WeakPtr<Profile> profile, std::string space_id,
                 base::OnceCallback<void(ImportResult)> done, ChromiumProfileData data) {
                if (!profile) {
                  ImportResult gone;
                  gone.problems.push_back(kProfileClosed);
                  std::move(done).Run(std::move(gone));
                  return;
                }
                (new Writer(profile.get(), std::move(data), std::move(space_id), std::move(done)))
                    ->Start();
              },
              profile->GetWeakPtr(), request.space_id, std::move(done)));
}

}  // namespace lb
