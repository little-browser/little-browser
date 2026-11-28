// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The native side of app.littlebrowser.lb.profiles.Profiles: the browser's profiles (docs/ux.md
// §5), as Chromium's ProfileManager keeps them, named in its ProfileAttributesStorage as on the
// desktop. A profile is known to Java by its directory's name ("Default", "Profile 1", ...);
// Profiles.isValidDir checks one before it gets here, and so does this file.

#include <jni.h>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/android/callback_android.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_init_params.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "lb/android/jni_headers/Profiles_jni.h"

namespace lb {

using base::android::JavaRef;
using base::android::ScopedJavaGlobalRef;

namespace {

// "Default" or "Profile <n>": the only directory names a profile may have.
bool IsValidDir(const std::string& dir) {
  if (dir == chrome::kInitialProfile) {
    return true;
  }
  const std::string prefix = "Profile ";
  unsigned number = 0;
  return dir.starts_with(prefix) && base::StringToUint(dir.substr(prefix.size()), &number) &&
         number > 0;
}

ProfileManager* Manager() {
  return g_browser_process ? g_browser_process->profile_manager() : nullptr;
}

std::vector<ProfileAttributesEntry*> Entries() {
  ProfileManager* manager = Manager();
  if (!manager) {
    return {};
  }
  return manager->GetProfileAttributesStorage().GetAllProfilesAttributes();
}

using ProfileCallback = base::OnceCallback<void(Profile*)>;

// The profiles Create has reserved (their storage entry is made) but not started creating yet,
// while what an earlier run may have left in their directory is deleted, each with the callbacks
// waiting for it: Create's own and those of Loads made meanwhile.
std::map<base::FilePath, std::vector<ProfileCallback>>& Reserved() {
  static base::NoDestructor<std::map<base::FilePath, std::vector<ProfileCallback>>> reserved;
  return *reserved;
}

// The reserved profile in `path` can be created now: creates it for everything waiting for it.
void CreateReserved(const base::FilePath& path) {
  auto node = Reserved().extract(path);
  if (node.empty()) {
    return;
  }
  for (ProfileCallback& callback : node.mapped()) {
    if (ProfileManager* manager = Manager()) {
      manager->CreateProfileAsync(path, std::move(callback));
    } else {
      std::move(callback).Run(nullptr);
    }
  }
}

base::OnceCallback<void(Profile*)> ToJava(const JavaRef<jobject>& callback) {
  return base::BindOnce(
      [](const ScopedJavaGlobalRef<jobject>& callback, Profile* profile) {
        base::android::RunObjectCallbackAndroid(callback,
                                                profile ? profile->GetJavaObject() : nullptr);
      },
      ScopedJavaGlobalRef<jobject>(callback));
}

}  // namespace

static std::vector<std::string> JNI_Profiles_Dirs(JNIEnv* env) {
  std::vector<std::string> dirs;
  for (ProfileAttributesEntry* entry : Entries()) {
    dirs.push_back(entry->GetPath().BaseName().AsUTF8Unsafe());
  }
  return dirs;
}

// Each profile's name, in Dirs' order; empty for one still named by Chromium's default (the
// caller names that one).
static std::vector<std::u16string> JNI_Profiles_Names(JNIEnv* env) {
  std::vector<std::u16string> names;
  for (ProfileAttributesEntry* entry : Entries()) {
    names.push_back(entry->IsUsingDefaultName() ? std::u16string()
                                                : entry->GetLocalProfileName());
  }
  return names;
}

// Creates a profile named `name` in a new "Profile <n>" directory and returns that directory's
// name; `callback` gets the profile once it is loaded and can be used (null if it failed). As the
// desktop's ProfileManager::CreateMultiProfileAsync does it: the directory comes from Local
// State's count of profiles created, so no two creations get the same one; the profile's storage
// entry is made at once, so it is listed and Load gives it while it is being created; and what a
// crash may have left in the directory (files written before Local State was) is deleted first.
static std::string JNI_Profiles_Create(JNIEnv* env,
                                       const std::u16string& name,
                                       const JavaRef<jobject>& callback) {
  ProfileManager* manager = Manager();
  PrefService* local_state = g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!manager || !local_state) {
    base::android::RunObjectCallbackAndroid(callback, nullptr);
    return std::string();
  }
  ProfileAttributesStorage& storage = manager->GetProfileAttributesStorage();
  std::string dir;
  base::FilePath path;
  do {
    const int n = std::max(1, local_state->GetInteger(prefs::kProfilesNumCreated));
    local_state->SetInteger(prefs::kProfilesNumCreated, n + 1);
    dir = chrome::kMultiProfileDirPrefix + base::NumberToString(n);
    path = manager->user_data_dir().AppendASCII(dir);
  } while (storage.GetProfileAttributesWithPath(path) || manager->GetProfileByPath(path) ||
           Reserved().contains(path));

  ProfileAttributesInitParams params;
  params.profile_path = path;
  params.profile_name = name;
  storage.AddProfile(std::move(params));
  if (ProfileAttributesEntry* entry = storage.GetProfileAttributesWithPath(path)) {
    entry->SetLocalProfileName(name, /*is_default_name=*/false);
  }

  Reserved()[path].push_back(base::BindOnce(
      [](const base::FilePath& path, base::OnceCallback<void(Profile*)> ready, Profile* profile) {
        ProfileManager* manager = Manager();
        if (!profile && manager && !manager->GetProfileByPath(path) &&
            manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(path)) {
          // Not made after all: not listed either.
          manager->GetProfileAttributesStorage().RemoveProfile(path);
        }
        std::move(ready).Run(profile);
      },
      path, ToJava(callback)));
  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&NukeProfileFromDisk, path, base::BindOnce(&CreateReserved, path)));
  return dir;
}

// Loads the profile in directory `dir` (it must exist) and gives it to `callback`, or null.
static void JNI_Profiles_Load(JNIEnv* env,
                              const std::string& dir,
                              const JavaRef<jobject>& callback) {
  ProfileManager* manager = Manager();
  if (!manager || !IsValidDir(dir)) {
    base::android::RunObjectCallbackAndroid(callback, nullptr);
    return;
  }
  const base::FilePath path = manager->user_data_dir().AppendASCII(dir);
  if (dir != chrome::kInitialProfile &&
      !manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(path)) {
    // Not a profile this browser made: don't make one by loading it.
    base::android::RunObjectCallbackAndroid(callback, nullptr);
    return;
  }
  if (auto reserved = Reserved().find(path); reserved != Reserved().end()) {
    // Being created by Create: loaded with it.
    reserved->second.push_back(ToJava(callback));
    return;
  }
  manager->CreateProfileAsync(path, ToJava(callback));
}

// The profile in directory `dir` if it is loaded already, else null.
static base::android::ScopedJavaLocalRef<jobject> JNI_Profiles_Loaded(JNIEnv* env,
                                                                     const std::string& dir) {
  ProfileManager* manager = Manager();
  if (!manager || !IsValidDir(dir)) {
    return nullptr;
  }
  Profile* profile = manager->GetProfileByPath(manager->user_data_dir().AppendASCII(dir));
  return profile ? profile->GetJavaObject() : nullptr;
}

// Records `dir` as the last used profile (Local State's profile.last_used), as the desktop does
// when one of a profile's windows comes to the front: what Chromium asks of "the" profile then
// follows the window in front. Only a loaded profile: ProfileManager::GetLastUsedProfile() loads
// the last used one synchronously, which one still loading can't be.
static void JNI_Profiles_SetLastUsed(JNIEnv* env, const std::string& dir) {
  ProfileManager* manager = Manager();
  if (!IsValidDir(dir) || !manager || !g_browser_process->local_state() ||
      !manager->GetProfileByPath(manager->user_data_dir().AppendASCII(dir))) {
    return;
  }
  g_browser_process->local_state()->SetFilePath(prefs::kProfileLastUsed,
                                                base::FilePath::FromASCII(dir));
}

}  // namespace lb

DEFINE_JNI(Profiles)
