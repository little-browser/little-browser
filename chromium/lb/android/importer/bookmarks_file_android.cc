// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The native side of app.littlebrowser.lb.importer.BookmarksFile: a bookmarks HTML file read by
// lb-core (core_bridge's bookmarks_html, tested there), its bookmarks merged into the profile's
// bookmarks as every import merges them (lb/browser/bookmarks/bookmark_merge.h), and its keyword
// searches added as search shortcuts unless their keyword is taken.

#include <jni.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/android/jni_array.h"
#include "base/android/scoped_java_ref.h"
#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "lb/android/jni_headers/BookmarksFile_jni.h"
#include "lb/browser/bookmarks/bookmark_merge.h"
#include "lb/core_bridge/lib.rs.h"

namespace lb {

using base::android::JavaRef;
using base::android::ScopedJavaLocalRef;

namespace {

// The file's search shortcuts as Chromium's search engines, normalised as TemplateURLService
// keys them (the keyword lowercased, without spaces), and only those whose address works.
std::vector<TemplateURLData> ShortcutsOf(const core::BookmarksFileResult& file,
                                         const SearchTermsData& search_terms) {
  std::vector<TemplateURLData> shortcuts;
  for (const core::SearchShortcut& s : file.search_shortcuts) {
    if (s.keyword.empty() || s.url.empty()) {
      continue;
    }
    TemplateURLData data;
    data.SetShortName(base::UTF8ToUTF16(std::string(s.name)));
    data.SetKeyword(base::UTF8ToUTF16(std::string(s.keyword)));
    data.SetURL(std::string(s.url));
    data.safe_for_autoreplace = false;
    // One the user added: Chromium's keyword search uses only active ones.
    data.is_active = TemplateURLData::ActiveStatus::kTrue;
    if (data.keyword().empty() || !TemplateURL(data).url_ref().IsValid(search_terms)) {
      continue;
    }
    shortcuts.push_back(std::move(data));
  }
  return shortcuts;
}

// Adds `shortcuts` whose keyword no search engine has yet; returns how many it added.
int AddShortcuts(TemplateURLService* service, const std::vector<TemplateURLData>& shortcuts) {
  int added = 0;
  for (const TemplateURLData& data : shortcuts) {
    if (service->GetTemplateURLForKeyword(data.keyword())) {
      continue;
    }
    if (service->Add(std::make_unique<TemplateURL>(data))) {
      ++added;
    }
  }
  return added;
}

// Adds search shortcuts once a TemplateURLService that hasn't loaded yet has, as the desktop's
// import does: it would drop them otherwise. Deletes itself then. The service runs the callback
// it holds, so a service destroyed first never does (and the few bytes here stay).
class AddWhenLoaded {
 public:
  static void Start(TemplateURLService* service, std::vector<TemplateURLData> shortcuts) {
    auto* self = new AddWhenLoaded(std::move(shortcuts));
    self->loaded_ = service->RegisterOnLoadedCallback(
        base::BindOnce(&AddWhenLoaded::Loaded, base::Unretained(self), base::Unretained(service)));
    service->Load();
  }

 private:
  explicit AddWhenLoaded(std::vector<TemplateURLData> shortcuts)
      : shortcuts_(std::move(shortcuts)) {}

  void Loaded(TemplateURLService* service) {
    AddShortcuts(service, shortcuts_);
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, this);
  }

  std::vector<TemplateURLData> shortcuts_;
  base::CallbackListSubscription loaded_;
};

}  // namespace

// {links added, search shortcuts added}, or {-1, 0} when the text isn't a bookmarks file. The
// bookmark model must be loaded, and so should the search engines be (BookmarksFile's caller
// waits for both); search shortcuts for search engines still loading are added once they have,
// uncounted.
static ScopedJavaLocalRef<jintArray> JNI_BookmarksFile_ImportFile(
    JNIEnv* env,
    const JavaRef<jobject>& java_profile,
    const std::string& html) {
  Profile* profile = Profile::FromJavaObject(java_profile);
  const core::BookmarksFileResult file = core::bookmarks_html(html);
  if (!profile || !std::string(file.error).empty()) {
    return base::android::ToJavaIntArray(env, std::vector<int>{-1, 0});
  }
  const int links = static_cast<int>(
      MergeIntoBookmarkModel(BookmarkModelFactory::GetForBrowserContext(profile), file.bookmarks));

  int searches = 0;
  TemplateURLService* service = TemplateURLServiceFactory::GetForProfile(profile);
  if (service && !file.search_shortcuts.empty()) {
    std::vector<TemplateURLData> shortcuts = ShortcutsOf(file, service->search_terms_data());
    if (service->loaded()) {
      searches = AddShortcuts(service, shortcuts);
    } else if (!shortcuts.empty()) {
      AddWhenLoaded::Start(service, std::move(shortcuts));
    }
  }
  return base::android::ToJavaIntArray(env, std::vector<int>{links, searches});
}

}  // namespace lb

DEFINE_JNI(BookmarksFile)
