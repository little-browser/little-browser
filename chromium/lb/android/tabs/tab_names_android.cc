// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The native side of app.littlebrowser.lb.tabs.TabNames: tab names and pinned links through the
// shared rules (lb/browser/tabs/tab_names.h), tested in lb_unittests and lb-core. This file only
// converts types: a TabCustom crosses as its four fields, and comes back through makeCustom.

#include <jni.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "lb/android/jni_headers/TabNames_jni.h"
#include "lb/browser/tabs/tab_names.h"
#include "url/gurl.h"

namespace lb {

using base::android::ScopedJavaLocalRef;

namespace {

TabCustom ToCustom(const std::u16string& name,
                   const std::string& pinned_url,
                   bool wakes_at_link,
                   const std::u16string& pinned_title) {
  TabCustom custom;
  custom.name = name;
  custom.pinned_url = GURL(pinned_url);
  custom.wakes_at_link = wakes_at_link;
  custom.pinned_title = pinned_title;
  return custom;
}

ScopedJavaLocalRef<jobject> ToJava(JNIEnv* env, const TabCustom& custom) {
  return Java_TabNames_makeCustom(
      env, custom.name, custom.pinned_url.is_valid() ? custom.pinned_url.spec() : std::string(),
      custom.wakes_at_link, custom.pinned_title);
}

}  // namespace

static std::u16string JNI_TabNames_TitleToShow(JNIEnv* env,
                                               const std::u16string& name,
                                               const std::string& pinned_url,
                                               bool wakes_at_link,
                                               const std::u16string& pinned_title,
                                               const std::u16string& page_title,
                                               bool asleep) {
  return TabTitleToShow(ToCustom(name, pinned_url, wakes_at_link, pinned_title), page_title,
                        asleep);
}

static bool JNI_TabNames_HasOwnTitle(JNIEnv* env,
                                     const std::u16string& name,
                                     const std::string& pinned_url,
                                     bool wakes_at_link,
                                     const std::u16string& pinned_title,
                                     bool asleep) {
  return HasOwnTitle(ToCustom(name, pinned_url, wakes_at_link, pinned_title), asleep);
}

static std::u16string JNI_TabNames_NameFromInput(JNIEnv* env,
                                                 const std::u16string& typed,
                                                 const std::u16string& page_title) {
  return NameFromInput(typed, page_title);
}

static bool JNI_TabNames_HasLeftPinnedUrl(JNIEnv* env,
                                          const std::string& pinned_url,
                                          const std::string& current_url) {
  return HasLeftPinnedUrl(GURL(pinned_url), GURL(current_url));
}

static ScopedJavaLocalRef<jobject> JNI_TabNames_Settle(JNIEnv* env,
                                                       const std::u16string& name,
                                                       const std::string& pinned_url,
                                                       bool wakes_at_link,
                                                       const std::u16string& pinned_title,
                                                       bool pinned,
                                                       const std::string& committed_url) {
  return ToJava(env, SettleTabLink(ToCustom(name, pinned_url, wakes_at_link, pinned_title), pinned,
                                   GURL(committed_url)));
}

static ScopedJavaLocalRef<jobject> JNI_TabNames_Restored(JNIEnv* env,
                                                         const std::u16string& name,
                                                         const std::string& pinned_url,
                                                         bool wakes_at_link,
                                                         const std::u16string& pinned_title,
                                                         bool pinned,
                                                         const std::string& committed_url,
                                                         bool shown) {
  return ToJava(env, RestoredTabCustom(ToCustom(name, pinned_url, wakes_at_link, pinned_title),
                                       pinned, GURL(committed_url), shown));
}

static ScopedJavaLocalRef<jobject> JNI_TabNames_TakePinnedTitle(
    JNIEnv* env,
    const std::u16string& name,
    const std::string& pinned_url,
    bool wakes_at_link,
    const std::u16string& pinned_title,
    const std::string& committed_url,
    const std::u16string& page_title) {
  return ToJava(env, TakePinnedTitle(ToCustom(name, pinned_url, wakes_at_link, pinned_title),
                                     GURL(committed_url), page_title));
}

static ScopedJavaLocalRef<jobject> JNI_TabNames_WithPinnedUrl(JNIEnv* env,
                                                              const std::u16string& name,
                                                              const std::string& pinned_url,
                                                              bool wakes_at_link,
                                                              const std::u16string& pinned_title,
                                                              const std::string& url) {
  return ToJava(env, WithPinnedUrl(ToCustom(name, pinned_url, wakes_at_link, pinned_title),
                                   GURL(url)));
}

static ScopedJavaLocalRef<jobject> JNI_TabNames_AfterPinnedSleep(
    JNIEnv* env,
    const std::u16string& name,
    const std::string& pinned_url,
    bool wakes_at_link,
    const std::u16string& pinned_title,
    const std::string& current_url) {
  return ToJava(env, AfterPinnedSleep(ToCustom(name, pinned_url, wakes_at_link, pinned_title),
                                      GURL(current_url)));
}

static bool JNI_TabNames_LoadsLinkWhenShown(JNIEnv* env,
                                            const std::u16string& name,
                                            const std::string& pinned_url,
                                            bool wakes_at_link,
                                            const std::u16string& pinned_title) {
  return OnTabShown(ToCustom(name, pinned_url, wakes_at_link, pinned_title)).load_link;
}

static ScopedJavaLocalRef<jobject> JNI_TabNames_Shown(JNIEnv* env,
                                                      const std::u16string& name,
                                                      const std::string& pinned_url,
                                                      bool wakes_at_link,
                                                      const std::u16string& pinned_title) {
  return ToJava(env, OnTabShown(ToCustom(name, pinned_url, wakes_at_link, pinned_title)).custom);
}

static std::string JNI_TabNames_PinnedUrlFromInput(JNIEnv* env, const std::u16string& typed) {
  const std::optional<GURL> url = PinnedUrlFromInput(typed);
  return url ? url->spec() : std::string();
}

static ScopedJavaLocalRef<jintArray> JNI_TabNames_MenuCommands(JNIEnv* env,
                                                               bool pinned,
                                                               bool left_pinned_url) {
  std::vector<int> commands;
  for (const TabNameMenuItem& item : TabNameMenuItems(1, pinned, left_pinned_url)) {
    // TabNames.Command's values, by name: the enum here may be reordered.
    switch (item.command) {
      case TabNameCommand::kRename:
        commands.push_back(0);
        break;
      case TabNameCommand::kEditPinnedUrl:
        commands.push_back(1);
        break;
      case TabNameCommand::kUsePageAsPinnedUrl:
        commands.push_back(2);
        break;
    }
  }
  return base::android::ToJavaIntArray(env, commands);
}

static std::vector<std::u16string> JNI_TabNames_MenuLabels(JNIEnv* env,
                                                           bool pinned,
                                                           bool left_pinned_url) {
  std::vector<std::u16string> labels;
  for (const TabNameMenuItem& item : TabNameMenuItems(1, pinned, left_pinned_url)) {
    labels.push_back(item.label);
  }
  return labels;
}

static ScopedJavaLocalRef<jbooleanArray> JNI_TabNames_MenuEnabled(JNIEnv* env,
                                                                  bool pinned,
                                                                  bool left_pinned_url) {
  std::vector<bool> enabled;
  for (const TabNameMenuItem& item : TabNameMenuItems(1, pinned, left_pinned_url)) {
    enabled.push_back(item.enabled);
  }
  return base::android::ToJavaBooleanArray(env, enabled);
}

}  // namespace lb

DEFINE_JNI(TabNames)
