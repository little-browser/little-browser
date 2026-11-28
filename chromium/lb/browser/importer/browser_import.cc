// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/browser_import.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "lb/browser/importer/import_text.h"
#include "lb/browser/importer/imported_tabs.h"
#include "lb/browser/spaces/space_model.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "lb/core_bridge/lib.rs.h"

namespace lb {

namespace {

// Problem lines (shown in the import summary).
constexpr char kProfileNotOpened[] = "A profile couldn't be opened, so its data wasn't imported";
constexpr char kSidebarUnreadable[] = "Spaces: Arc's sidebar file couldn't be read";
constexpr char kNoKeychainKey[] = "Cookies: the keychain didn't give the key, so they were skipped";
constexpr char kSpacesPrefix[] = "Spaces: ";  // then why Arc's spaces couldn't be read
// "Profiles: "Work" couldn't be made", around the profile's name.
constexpr char kProfileNotMadeBefore[] = "Profiles: \"";
constexpr char kProfileNotMadeAfter[] = "\" couldn't be made";
// A space whose Arc name is empty is called this, as lb::core::place_spaces numbers it.
constexpr char kArcSpaceName[] = "Arc";

// A profile of the source browser that the import reads.
struct SourceProfile {
  // As lb-core's profile mapping names it: empty for the browser's default profile.
  std::string key;
  base::FilePath dir;  // its folder
  std::string name;    // as the browser shows it
  bool exists = false;
};

// What was read from the browser's files, off the UI thread.
struct BrowserRead {
  core::ArcResult sidebar;  // Arc: the chosen spaces
  bool has_sidebar = false;
  std::vector<SourceProfile> profiles;  // in the order they are imported
  std::optional<std::string> keychain_password;
  std::vector<std::string> problems;
};

// The key of a profile folder: the default profile is "Default", or the User Data folder itself
// for browsers without profile folders.
std::string ProfileKey(const std::string& folder) {
  return folder == chrome::kInitialProfile ? std::string() : folder;
}

BrowserRead ReadBrowser(const BrowserImportRequest& request) {
  BrowserRead read;
  const bool arc = request.kind == ImportSourceKind::kArc;
  const base::FilePath user_data = arc ? request.dir.AppendASCII("User Data") : request.dir;
  std::map<std::string, std::string> names;  // profile key → name
  std::string text;
  if (base::ReadFileToString(user_data.AppendASCII("Local State"), &text)) {
    for (const core::SourceProfile& p : core::chromium_profiles(ToCoreText(text)).profiles) {
      names[ProfileKey(std::string(p.dir))] = std::string(p.name);
    }
  }
  std::vector<std::string> folders;  // the profiles to read, as folders in User Data
  if (arc) {
    if (base::ReadFileToString(request.dir.AppendASCII("StorableSidebar.json"), &text)) {
      rust::Vec<rust::String> chosen;
      for (const std::string& id : request.chosen) {
        chosen.push_back(id);
      }
      read.sidebar = core::chosen_arc_spaces(ToCoreText(text), chosen);
      read.has_sidebar = read.sidebar.error.empty();
      if (!read.has_sidebar) {
        read.problems.push_back(kSpacesPrefix + std::string(read.sidebar.error));
      }
    } else {
      read.problems.push_back(kSidebarUnreadable);
    }
    // The profiles of the chosen spaces, in the spaces' order.
    for (const core::ArcSpace& space : read.sidebar.spaces) {
      const std::string dir(space.profile_dir);
      const std::string folder = dir.empty() ? std::string(chrome::kInitialProfile) : dir;
      if (std::ranges::find(folders, folder) == folders.end()) {
        folders.push_back(folder);
      }
    }
  } else {
    folders = request.chosen;
  }
  for (const std::string& folder : folders) {
    SourceProfile profile;
    profile.key = ProfileKey(folder);
    profile.dir = folder.empty() ? user_data : user_data.Append(folder);
    profile.name = names[profile.key];
    profile.exists = base::DirectoryExists(profile.dir);
    read.profiles.push_back(std::move(profile));
  }
  if (request.items & kImportCookies) {
    // Once for all the browser's profiles: one keychain prompt, not one per profile.
    read.keychain_password =
        ReadImportKeychainPassword(request.keychain_service, request.keychain_account);
    if (!read.keychain_password) {
      read.problems.push_back(kNoKeychainKey);
    }
  }
  return read;
}

class BrowserImport {
 public:
  BrowserImport(BrowserImportRequest request, base::OnceCallback<void(ImportResult)> done)
      : request_(std::move(request)), done_(std::move(done)) {}

  void Start() {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&ReadBrowser, request_),
        base::BindOnce(&BrowserImport::OnRead, base::Unretained(this)));
  }

 private:
  bool arc() const { return request_.kind == ImportSourceKind::kArc; }
  bool spaces() const { return request_.items & kImportSpaces; }

  void OnRead(BrowserRead read) {
    read_ = std::move(read);
    result_.problems = read_.problems;
    if (!read_.keychain_password) {
      Report(kImportCookies);  // nothing of them comes: a problem line says why
    }
    if (!spaces()) {
      // Without "Spaces, profiles and tabs" no profile is made: everything goes into the
      // current profile, as from any other browser.
      for (const SourceProfile& p : read_.profiles) {
        profile_of_[p.key] = request_.current_profile;
      }
      ImportNextProfile();
      return;
    }

    ProfileManager* manager = g_browser_process->profile_manager();
    rust::Vec<core::ExistingProfile> existing;
    bool has_initial_profile = false;
    for (ProfileAttributesEntry* entry :
         manager->GetProfileAttributesStorage().GetAllProfilesAttributes()) {
      const std::string base = entry->GetPath().BaseName().AsUTF8Unsafe();
      has_initial_profile = has_initial_profile || base == chrome::kInitialProfile;
      existing.push_back(
          core::ExistingProfile{base, ToCoreText(base::UTF16ToUTF8(entry->GetLocalProfileName()))});
    }
    rust::Vec<core::ProfileName> source;
    for (const SourceProfile& p : read_.profiles) {
      source.push_back(core::ProfileName{p.key, ToCoreText(p.name)});
    }
    // The browser's default profile goes to our default profile, or to the current one if the
    // default profile was deleted.
    const std::string default_profile =
        has_initial_profile ? std::string(chrome::kInitialProfile) : request_.current_profile;
    rust::Vec<core::ProfileTarget> targets = core::map_profiles(existing, default_profile, source);
    for (size_t i = 0; i < read_.profiles.size(); ++i) {
      const std::string& key = read_.profiles[i].key;
      if (!std::string(targets[i].existing_id).empty()) {
        profile_of_[key] = std::string(targets[i].existing_id);
      } else {
        to_create_.emplace_back(key, std::string(targets[i].new_name));
      }
    }
    CreateNextProfile();
  }

  // Makes the profiles the browser has and we don't, one at a time; source profiles with the
  // same name share one.
  void CreateNextProfile() {
    while (!to_create_.empty()) {
      auto [key, name] = to_create_.front();
      to_create_.erase(to_create_.begin());
      if (auto made = made_by_name_.find(name); made != made_by_name_.end()) {
        profile_of_[key] = made->second;
        continue;
      }
      ProfileManager* manager = g_browser_process->profile_manager();
      ProfileManager::CreateMultiProfileAsync(
          base::UTF8ToUTF16(name),
          manager->GetProfileAttributesStorage().ChooseAvatarIconIndexForNewProfile(),
          /*is_hidden=*/false,
          base::BindOnce(&BrowserImport::OnProfileCreated, base::Unretained(this), key, name));
      return;
    }
    PlaceSpaces();
  }

  void OnProfileCreated(std::string key, std::string name, Profile* profile) {
    if (profile) {
      const std::string base = profile->GetBaseName().AsUTF8Unsafe();
      profile_of_[key] = base;
      made_by_name_[name] = base;
    } else {
      result_.problems.push_back(kProfileNotMadeBefore + name + kProfileNotMadeAfter);
    }
    CreateNextProfile();
  }

  void PlaceSpaces() {
    std::vector<std::pair<Space, bool>> spaces = SidebarController::GetSpacesForImport();
    std::vector<Space> all;
    rust::Vec<core::ExistingSpace> existing;
    for (const auto& [space, empty] : spaces) {
      all.push_back(space);
      existing.push_back(core::ExistingSpace{ToCoreText(space.name), space.profile, empty});
    }
    if (arc()) {
      PlaceArcSpaces(spaces, all, existing);
    } else {
      PlaceProfileSpaces(spaces, all, existing);
    }
    if (arc()) {
      Report(kImportSpaces);  // Arc's tabs came with its spaces
    }
    ImportNextProfile();
  }

  void PlaceArcSpaces(const std::vector<std::pair<Space, bool>>& spaces,
                      std::vector<Space>& all,
                      const rust::Vec<core::ExistingSpace>& existing) {
    std::vector<const core::ArcSpace*> placed;
    rust::Vec<core::SpaceToPlace> to_place;
    for (const core::ArcSpace& space : read_.sidebar.spaces) {
      auto profile = profile_of_.find(std::string(space.profile_dir));
      if (profile == profile_of_.end()) {
        continue;  // its profile couldn't be made
      }
      placed.push_back(&space);
      to_place.push_back(core::SpaceToPlace{std::string(space.name), profile->second});
    }
    rust::Vec<core::SpaceTarget> targets = core::place_spaces(existing, to_place, kArcSpaceName);
    for (size_t i = 0; i < placed.size(); ++i) {
      const core::ArcSpace& arc = *placed[i];
      ++result_.counts.spaces;
      if (targets[i].fill_index >= 0) {
        const Space& filled = spaces[targets[i].fill_index].first;
        const std::vector<SavedTab> tabs =
            ArcSpaceTabs(arc, static_cast<int>(TabGroupColorForHue(filled.hue)));
        result_.counts.tabs += tabs.size();
        SidebarController::AddImportedTabs(filled.id, tabs);
        continue;
      }
      // Arc's colour becomes the nearest of ours (docs/ux.md §13); its folders' groups start in
      // the group colour nearest to it, as a new group does.
      const int hue =
          arc.hue >= 0 ? SpaceColours()[NearestSpaceColour(arc.hue)].hue : NextSpaceHue(all);
      const std::vector<SavedTab> tabs =
          ArcSpaceTabs(arc, static_cast<int>(TabGroupColorForHue(hue)));
      result_.counts.tabs += tabs.size();
      Space space{base::Uuid::GenerateRandomV4().AsLowercaseString(),
                  std::string(targets[i].new_name),
                  arc.emoji.empty() ? std::string(NextSpaceIcon(all)) : std::string(arc.emoji),
                  hue,
                  std::string(to_place[i].profile),
                  u""};
      all.push_back(space);
      SidebarController::AddImportedSpace(space, tabs);
    }
  }

  // A Chrome-family profile becomes a space named after it; its open tabs join it once read.
  void PlaceProfileSpaces(const std::vector<std::pair<Space, bool>>& spaces,
                          std::vector<Space>& all,
                          const rust::Vec<core::ExistingSpace>& existing) {
    std::vector<const SourceProfile*> placed;
    rust::Vec<core::SpaceToPlace> to_place;
    for (const SourceProfile& p : read_.profiles) {
      auto profile = profile_of_.find(p.key);
      if (!p.exists || profile == profile_of_.end()) {
        continue;
      }
      placed.push_back(&p);
      to_place.push_back(core::SpaceToPlace{ToCoreText(p.name), profile->second});
    }
    rust::Vec<core::SpaceTarget> targets =
        core::place_spaces(existing, to_place, ToCoreText(request_.browser));
    for (size_t i = 0; i < placed.size(); ++i) {
      ++result_.counts.spaces;
      if (targets[i].fill_index >= 0) {
        space_of_[placed[i]->key] = spaces[targets[i].fill_index].first.id;
        continue;
      }
      Space space{base::Uuid::GenerateRandomV4().AsLowercaseString(),
                  std::string(targets[i].new_name),
                  NextSpaceIcon(all),
                  NextSpaceHue(all),
                  std::string(to_place[i].profile),
                  u""};
      all.push_back(space);
      SidebarController::AddImportedSpace(space, {});
      space_of_[placed[i]->key] = space.id;
    }
  }

  // Each source profile's bookmarks, history and the rest (and a Chrome-family profile's open
  // tabs) into its profile, one after another: two of them may go into the same profile.
  // What `source` imports into its profile (none when it can't).
  uint32_t ItemsOf(const SourceProfile& source) const {
    if (!source.exists || !profile_of_.contains(source.key)) {
      return 0;
    }
    uint32_t items = request_.items & ~static_cast<uint32_t>(kImportSpaces | kImportOpenTabs);
    if (!read_.keychain_password) {
      items &= ~static_cast<uint32_t>(kImportCookies);  // the problem is already named
    }
    if (space_of_.contains(source.key)) {
      items |= kImportOpenTabs;
    }
    return items;
  }

  void ImportNextProfile() {
    while (next_ < read_.profiles.size()) {
      const SourceProfile& source = read_.profiles[next_++];
      const uint32_t items = ItemsOf(source);
      if (!items) {
        continue;
      }
      ChromiumImportRequest request;
      request.profile_dir = source.dir;
      request.items = items;
      if (auto space = space_of_.find(source.key); space != space_of_.end()) {
        request.space_id = space->second;
      }
      request.keychain_service = request_.keychain_service;
      request.keychain_account = request_.keychain_account;
      request.keychain_password = read_.keychain_password;
      // An item has come over once the last profile that imports it has written it: the earlier
      // ones are done by then, as they go one after another.
      if (std::ranges::none_of(read_.profiles.begin() + next_, read_.profiles.end(),
                               [&](const SourceProfile& later) { return ItemsOf(later) != 0; })) {
        request.on_item_written =
            base::BindRepeating(&BrowserImport::LastProfileWrote, base::Unretained(this));
      }
      const auto target = profile_of_.find(source.key);
      ProfileManager* manager = g_browser_process->profile_manager();
      // LoadProfileByPath runs the callback in every case (with null when the profile can't be
      // opened), so the next profile always follows.
      manager->LoadProfileByPath(manager->user_data_dir().AppendASCII(target->second),
                                 /*incognito=*/false,
                                 base::BindOnce(&BrowserImport::OnProfileLoaded,
                                                base::Unretained(this), request, source.name));
      return;
    }
    Finish();
  }

  void OnProfileLoaded(ChromiumImportRequest request, std::string name, Profile* profile) {
    if (!profile) {
      ImportResult failed;
      failed.problems.push_back(kProfileNotOpened);
      OnProfileImported(std::move(name), std::move(failed));
      return;
    }
    ImportChromiumProfile(
        profile, request,
        base::BindOnce(&BrowserImport::OnProfileImported, base::Unretained(this), std::move(name)));
  }

  void OnProfileImported(std::string name, ImportResult imported) {
    for (std::string& problem : imported.problems) {
      problem = ProfileProblem(problem, name, read_.profiles.size() > 1);
    }
    result_ += imported;
    ImportNextProfile();
  }

  // The last profile wrote `item`. A Chrome-family profile's open tabs fill its space last.
  void LastProfileWrote(uint32_t item) {
    Report(item == kImportOpenTabs ? (arc() ? 0u : static_cast<uint32_t>(kImportSpaces)) : item);
  }

  // `item` has come over: tells the progress display, once.
  void Report(uint32_t item) {
    if (!(request_.items & item) || (reported_ & item)) {
      return;
    }
    reported_ |= item;
    if (request_.on_item) {
      request_.on_item.Run(item);
    }
  }

  void Finish() {
    // Every item is over now, also those with nothing to write or whose profile couldn't open.
    for (ImportItem item : ImportItemsInOrder()) {
      Report(item);
    }
    base::OnceCallback<void(ImportResult)> done = std::move(done_);
    ImportResult result = std::move(result_);
    delete this;
    std::move(done).Run(std::move(result));
  }

  const BrowserImportRequest request_;
  base::OnceCallback<void(ImportResult)> done_;
  BrowserRead read_;
  ImportResult result_;
  std::map<std::string, std::string> profile_of_;  // source profile key → ours
  std::map<std::string, std::string> space_of_;    // Chrome-family profile key → its space
  std::vector<std::pair<std::string, std::string>> to_create_;
  std::map<std::string, std::string> made_by_name_;
  size_t next_ = 0;        // the next of read_.profiles to import
  uint32_t reported_ = 0;  // the items reported to request_.on_item
};

}  // namespace

BrowserImportRequest::BrowserImportRequest() = default;
BrowserImportRequest::BrowserImportRequest(const BrowserImportRequest&) = default;
BrowserImportRequest& BrowserImportRequest::operator=(const BrowserImportRequest&) = default;
BrowserImportRequest::~BrowserImportRequest() = default;

void ImportBrowser(const BrowserImportRequest& request,
                   base::OnceCallback<void(ImportResult)> done) {
  (new BrowserImport(request, std::move(done)))->Start();
}

}  // namespace lb
