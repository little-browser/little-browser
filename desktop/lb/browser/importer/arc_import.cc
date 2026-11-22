// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/arc_import.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/barrier_callback.h"
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
#include "lb/browser/importer/arc_tabs.h"
#include "lb/browser/importer/import_text.h"
#include "lb/browser/spaces/space_model.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "lb/core_bridge/lib.rs.h"

namespace lb {

namespace {

// Problem lines (shown in the import summary).
constexpr char kProfileNotOpened[] = "A profile couldn't be opened, so its data wasn't imported";
constexpr char kSidebarUnreadable[] = "Spaces: Arc's sidebar file couldn't be read";
constexpr char kNoKeychainKey[] = "Cookies: the keychain didn't give the key, so they were skipped";

// What was read from Arc's files.
struct ArcRead {
  core::ArcResult sidebar;
  bool has_sidebar = false;
  // Arc's profiles: folder ("Default" is its default profile) and name.
  std::vector<std::pair<std::string, std::string>> profiles;
  // "Arc Safe Storage", read once for every profile (kImportCookies).
  std::optional<std::string> keychain_password;
  std::vector<std::string> problems;
};

ArcRead ReadArc(const base::FilePath& arc_dir,
                bool spaces,
                bool cookies,
                const std::string& keychain_service,
                const std::string& keychain_account) {
  ArcRead read;
  std::string text;
  if (spaces) {
    if (base::ReadFileToString(arc_dir.AppendASCII("StorableSidebar.json"), &text)) {
      read.sidebar = core::arc_sidebar(ToCoreText(text));
      read.has_sidebar = read.sidebar.error.empty();
      if (!read.has_sidebar) {
        read.problems.push_back("Spaces: " + std::string(read.sidebar.error));
      }
    } else {
      read.problems.push_back(kSidebarUnreadable);
    }
  }
  if (base::ReadFileToString(arc_dir.AppendASCII("User Data").AppendASCII("Local State"), &text)) {
    core::ProfilesResult profiles = core::chromium_profiles(ToCoreText(text));
    for (const core::SourceProfile& p : profiles.profiles) {
      if (!std::string(p.dir).empty()) {
        read.profiles.emplace_back(std::string(p.dir), std::string(p.name));
      }
    }
  }
  if (cookies) {
    // Once for all of Arc's profiles: one keychain prompt, not one per profile.
    read.keychain_password = ReadImportKeychainPassword(keychain_service, keychain_account);
    if (!read.keychain_password) {
      read.problems.push_back(kNoKeychainKey);
    }
  }
  return read;
}

// Arc's profile folder as lb-core's sidebar names it: empty for the default profile.
std::string SidebarDir(const std::string& dir) {
  return dir == chrome::kInitialProfile ? std::string() : dir;
}

class ArcImport {
 public:
  ArcImport(base::FilePath arc_dir,
            uint32_t items,
            std::string keychain_service,
            std::string keychain_account,
            std::string current_profile,
            base::OnceCallback<void(ImportResult)> done)
      : arc_dir_(std::move(arc_dir)),
        items_(items),
        keychain_service_(std::move(keychain_service)),
        keychain_account_(std::move(keychain_account)),
        current_profile_(std::move(current_profile)),
        done_(std::move(done)) {}

  void Start() {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&ReadArc, arc_dir_, (items_ & kImportSpaces) != 0,
                       (items_ & kImportCookies) != 0, keychain_service_, keychain_account_),
        base::BindOnce(&ArcImport::OnRead, base::Unretained(this)));
  }

 private:
  void OnRead(ArcRead read) {
    read_ = std::move(read);
    result_.problems = read_.problems;
    // Every Arc profile that has spaces or is listed, the default one first.
    std::vector<std::string> dirs = {std::string()};
    auto add = [&dirs](const std::string& dir) {
      if (std::ranges::find(dirs, dir) == dirs.end()) {
        dirs.push_back(dir);
      }
    };
    for (const auto& [dir, name] : read_.profiles) {
      add(SidebarDir(dir));
    }
    for (const core::ArcSpace& space : read_.sidebar.spaces) {
      add(std::string(space.profile_dir));
    }

    if (!(items_ & kImportSpaces)) {
      // Without "Spaces, profiles and tabs" no profile is made: everything goes into the
      // current profile, as from any other browser.
      for (const std::string& dir : dirs) {
        profile_of_[dir] = current_profile_;
      }
      ImportProfilesData();
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
    rust::Vec<core::ArcProfileName> arc;
    for (const std::string& dir : dirs) {
      std::string name;
      for (const auto& [d, n] : read_.profiles) {
        if (SidebarDir(d) == dir) {
          name = n;
        }
      }
      arc.push_back(core::ArcProfileName{dir, name});
    }
    // Arc's default profile goes to our default profile, or to the current one if the default
    // profile was deleted.
    const std::string default_profile =
        has_initial_profile ? std::string(chrome::kInitialProfile) : current_profile_;
    rust::Vec<core::ProfileTarget> targets = core::map_arc_profiles(existing, default_profile, arc);
    for (size_t i = 0; i < dirs.size(); ++i) {
      if (!std::string(targets[i].existing_id).empty()) {
        profile_of_[dirs[i]] = std::string(targets[i].existing_id);
      } else {
        to_create_.emplace_back(dirs[i], std::string(targets[i].new_name));
      }
    }
    CreateNextProfile();
  }

  // Makes the profiles Arc has and we don't, one at a time; Arc profiles with the same name
  // share one.
  void CreateNextProfile() {
    while (!to_create_.empty()) {
      auto [dir, name] = to_create_.front();
      to_create_.erase(to_create_.begin());
      if (auto made = made_by_name_.find(name); made != made_by_name_.end()) {
        profile_of_[dir] = made->second;
        continue;
      }
      ProfileManager* manager = g_browser_process->profile_manager();
      ProfileManager::CreateMultiProfileAsync(
          base::UTF8ToUTF16(name),
          manager->GetProfileAttributesStorage().ChooseAvatarIconIndexForNewProfile(),
          /*is_hidden=*/false,
          base::BindOnce(&ArcImport::OnProfileCreated, base::Unretained(this), dir, name));
      return;
    }
    PlaceSpaces();
  }

  void OnProfileCreated(std::string dir, std::string name, Profile* profile) {
    if (profile) {
      const std::string base = profile->GetBaseName().AsUTF8Unsafe();
      profile_of_[dir] = base;
      made_by_name_[name] = base;
    } else {
      result_.problems.push_back("Profiles: \"" + name + "\" couldn't be made");
    }
    CreateNextProfile();
  }

  void PlaceSpaces() {
    if (read_.has_sidebar) {
      std::vector<std::pair<Space, bool>> spaces = SidebarController::GetSpacesForImport();
      std::vector<Space> all;
      rust::Vec<core::ExistingSpace> existing;
      for (const auto& [space, empty] : spaces) {
        all.push_back(space);
        existing.push_back(core::ExistingSpace{ToCoreText(space.name), space.profile, empty});
      }
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
      rust::Vec<core::SpaceTarget> targets = core::place_spaces(existing, to_place, "Arc");
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
            arc.hue >= 0 ? kSpaceColours[NearestSpaceColour(arc.hue)].hue : NextSpaceHue(all);
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
    ImportProfilesData();
  }

  // Each Arc profile's bookmarks, history and the rest, into its matching profile.
  void ImportProfilesData() {
    uint32_t data = items_ & ~static_cast<uint32_t>(kImportSpaces | kImportOpenTabs);
    if (!read_.keychain_password) {
      data &= ~static_cast<uint32_t>(kImportCookies);  // the problem is already named
    }
    std::vector<std::pair<std::string, std::string>> jobs;  // Arc folder, our profile
    for (const auto& [dir, base] : profile_of_) {
      const std::string arc_dir = dir.empty() ? std::string(chrome::kInitialProfile) : dir;
      if (base::DirectoryExists(arc_dir_.AppendASCII("User Data").AppendASCII(arc_dir))) {
        jobs.emplace_back(arc_dir, base);
      }
    }
    if (!data || jobs.empty()) {
      Finish();
      return;
    }
    // Copied out of `this`: the barrier can finish the import (and delete it) mid-loop.
    const base::FilePath user_data = arc_dir_.AppendASCII("User Data");
    const std::string keychain_service = keychain_service_;
    const std::string keychain_account = keychain_account_;
    const std::optional<std::string> keychain_password = read_.keychain_password;
    auto collected = base::BarrierCallback<ImportResult>(
        jobs.size(), base::BindOnce(&ArcImport::OnDataImported, base::Unretained(this)));
    ProfileManager* manager = g_browser_process->profile_manager();
    for (const auto& [arc_dir, base] : jobs) {
      ChromiumImportRequest request;
      request.profile_dir = user_data.AppendASCII(arc_dir);
      request.items = data;
      request.keychain_service = keychain_service;
      request.keychain_account = keychain_account;
      request.keychain_password = keychain_password;
      // LoadProfileByPath runs the callback in every case (with null when the profile can't be
      // opened), so each job reports exactly once.
      manager->LoadProfileByPath(
          manager->user_data_dir().AppendASCII(base), /*incognito=*/false,
          base::BindOnce(
              [](ChromiumImportRequest request,
                 base::RepeatingCallback<void(ImportResult)> collected, Profile* profile) {
                if (!profile) {
                  ImportResult failed;
                  failed.problems.push_back(kProfileNotOpened);
                  collected.Run(std::move(failed));
                  return;
                }
                ImportChromiumProfile(profile, request, collected);
              },
              request, collected));
    }
  }

  void OnDataImported(std::vector<ImportResult> results) {
    for (const ImportResult& r : results) {
      result_ += r;
    }
    Finish();
  }

  void Finish() {
    base::OnceCallback<void(ImportResult)> done = std::move(done_);
    ImportResult result = std::move(result_);
    delete this;
    std::move(done).Run(std::move(result));
  }

  const base::FilePath arc_dir_;
  const uint32_t items_;
  const std::string keychain_service_;
  const std::string keychain_account_;
  const std::string current_profile_;
  base::OnceCallback<void(ImportResult)> done_;
  ArcRead read_;
  ImportResult result_;
  std::map<std::string, std::string> profile_of_;  // Arc folder (empty: default) → ours
  std::vector<std::pair<std::string, std::string>> to_create_;
  std::map<std::string, std::string> made_by_name_;
};

}  // namespace

void ImportArc(const base::FilePath& arc_dir,
               uint32_t items,
               const std::string& keychain_service,
               const std::string& keychain_account,
               const std::string& current_profile,
               base::OnceCallback<void(ImportResult)> done) {
  (new ArcImport(arc_dir, items, keychain_service, keychain_account, current_profile,
                 std::move(done)))
      ->Start();
}

}  // namespace lb
