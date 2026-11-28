// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The native side of app.littlebrowser.lb.spaces.SpaceStore: the spaces and their saved tabs in
// Local State, through the shared store (lb/browser/spaces/space_store.h), which is tested in
// lb_unittests. This file only converts types.

#include <jni.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "base/android/jni_array.h"
#include "base/android/scoped_java_ref.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/chrome_constants.h"
#include "lb/android/jni_headers/SpaceStore_jni.h"
#include "lb/browser/spaces/space_model.h"
#include "lb/browser/spaces/space_store.h"
#include "url/gurl.h"

namespace lb {

using base::android::JavaRef;
using base::android::ScopedJavaLocalRef;

namespace {

PrefService& LocalState() {
  return *g_browser_process->local_state();
}

std::vector<Space> Spaces() {
  return ReadSpaces(LocalState(), chrome::kInitialProfile);
}

template <typename T, typename F>
std::vector<T> Map(const std::vector<SavedTab>& tabs, F field) {
  std::vector<T> values;
  values.reserve(tabs.size());
  for (const SavedTab& tab : tabs) {
    values.push_back(field(tab));
  }
  return values;
}

std::vector<std::string> SpaceField(std::string Space::* field) {
  std::vector<std::string> values;
  for (const Space& space : Spaces()) {
    values.push_back(space.*field);
  }
  return values;
}

ScopedJavaLocalRef<jbooleanArray> ToJava(JNIEnv* env, const std::vector<bool>& values) {
  return base::android::ToJavaBooleanArray(env, values);
}

ScopedJavaLocalRef<jintArray> ToJava(JNIEnv* env, const std::vector<int>& values) {
  return base::android::ToJavaIntArray(env, values);
}

std::vector<SavedTab> Tabs(const std::string& space_id) {
  return ReadSpaceTabs(LocalState(), space_id);
}

}  // namespace

static std::vector<std::string> JNI_SpaceStore_Ids(JNIEnv* env) {
  return SpaceField(&Space::id);
}

static std::vector<std::string> JNI_SpaceStore_Names(JNIEnv* env) {
  return SpaceField(&Space::name);
}

static std::vector<std::string> JNI_SpaceStore_SpaceIcons(JNIEnv* env) {
  return SpaceField(&Space::icon);
}

static ScopedJavaLocalRef<jintArray> JNI_SpaceStore_Hues(JNIEnv* env) {
  std::vector<int> hues;
  for (const Space& space : Spaces()) {
    hues.push_back(space.hue);
  }
  return ToJava(env, hues);
}

static std::vector<std::string> JNI_SpaceStore_Profiles(JNIEnv* env) {
  return SpaceField(&Space::profile);
}

static std::string JNI_SpaceStore_Create(JNIEnv* env,
                                         const std::string& name,
                                         const std::string& icon,
                                         int32_t hue,
                                         const std::string& profile) {
  Space space{base::Uuid::GenerateRandomV4().AsLowercaseString(), name, icon, NormalizeHue(hue),
              profile, u""};
  AppendSpace(LocalState(), space);
  return space.id;
}

static void JNI_SpaceStore_SetName(JNIEnv* env, const std::string& id, const std::string& name) {
  if (!name.empty()) {
    SetSpaceField(LocalState(), id, "name", base::Value(name));
  }
}

static void JNI_SpaceStore_SetIcon(JNIEnv* env, const std::string& id, const std::string& icon) {
  SetSpaceField(LocalState(), id, "icon", base::Value(icon));
}

static void JNI_SpaceStore_SetHue(JNIEnv* env, const std::string& id, int32_t hue) {
  SetSpaceField(LocalState(), id, "hue", base::Value(NormalizeHue(hue)));
}

static void JNI_SpaceStore_SetProfile(JNIEnv* env,
                                      const std::string& id,
                                      const std::string& profile) {
  SetSpaceField(LocalState(), id, "profile", base::Value(profile));
}

static void JNI_SpaceStore_Remove(JNIEnv* env, const std::string& id) {
  RemoveSpace(LocalState(), id);
}

static std::vector<std::string> JNI_SpaceStore_TabUrls(JNIEnv* env, const std::string& id) {
  return Map<std::string>(Tabs(id), [](const SavedTab& t) { return t.url.spec(); });
}

static std::vector<std::string> JNI_SpaceStore_TabTitles(JNIEnv* env, const std::string& id) {
  return Map<std::string>(Tabs(id), [](const SavedTab& t) { return base::UTF16ToUTF8(t.title); });
}

static ScopedJavaLocalRef<jbooleanArray> JNI_SpaceStore_TabPinned(JNIEnv* env,
                                                                  const std::string& id) {
  return ToJava(env, Map<bool>(Tabs(id), [](const SavedTab& t) { return t.pinned; }));
}

static ScopedJavaLocalRef<jbooleanArray> JNI_SpaceStore_TabActive(JNIEnv* env,
                                                                  const std::string& id) {
  return ToJava(env, Map<bool>(Tabs(id), [](const SavedTab& t) { return t.active; }));
}

static std::vector<std::string> JNI_SpaceStore_TabGroupIds(JNIEnv* env, const std::string& id) {
  return Map<std::string>(Tabs(id), [](const SavedTab& t) { return t.group_id; });
}

static std::vector<std::string> JNI_SpaceStore_TabGroupTitles(JNIEnv* env,
                                                              const std::string& id) {
  return Map<std::string>(Tabs(id),
                          [](const SavedTab& t) { return base::UTF16ToUTF8(t.group_title); });
}

static ScopedJavaLocalRef<jintArray> JNI_SpaceStore_TabGroupColors(JNIEnv* env,
                                                                   const std::string& id) {
  return ToJava(env, Map<int>(Tabs(id), [](const SavedTab& t) { return t.group_color; }));
}

static ScopedJavaLocalRef<jbooleanArray> JNI_SpaceStore_TabGroupCollapsed(
    JNIEnv* env,
    const std::string& id) {
  return ToJava(env, Map<bool>(Tabs(id), [](const SavedTab& t) { return t.group_collapsed; }));
}

static std::vector<std::string> JNI_SpaceStore_TabNames(JNIEnv* env, const std::string& id) {
  return Map<std::string>(Tabs(id),
                          [](const SavedTab& t) { return base::UTF16ToUTF8(t.custom.name); });
}

static std::vector<std::string> JNI_SpaceStore_TabPinnedUrls(JNIEnv* env,
                                                             const std::string& id) {
  return Map<std::string>(Tabs(id), [](const SavedTab& t) {
    return t.custom.pinned_url.is_valid() ? t.custom.pinned_url.spec() : std::string();
  });
}

static ScopedJavaLocalRef<jbooleanArray> JNI_SpaceStore_TabWakesAtLink(JNIEnv* env,
                                                                       const std::string& id) {
  return ToJava(env, Map<bool>(Tabs(id), [](const SavedTab& t) { return t.custom.wakes_at_link; }));
}

static std::vector<std::string> JNI_SpaceStore_TabPinnedTitles(JNIEnv* env,
                                                               const std::string& id) {
  return Map<std::string>(
      Tabs(id), [](const SavedTab& t) { return base::UTF16ToUTF8(t.custom.pinned_title); });
}

static void JNI_SpaceStore_WriteTabs(JNIEnv* env,
                                     const std::string& id,
                                     const std::vector<std::string>& urls,
                                     const std::vector<std::string>& titles,
                                     const JavaRef<jbooleanArray>& java_pinned,
                                     const JavaRef<jbooleanArray>& java_active,
                                     const std::vector<std::string>& group_ids,
                                     const std::vector<std::string>& group_titles,
                                     const JavaRef<jintArray>& java_group_colors,
                                     const JavaRef<jbooleanArray>& java_group_collapsed,
                                     const std::vector<std::string>& names,
                                     const std::vector<std::string>& pinned_urls,
                                     const JavaRef<jbooleanArray>& java_wakes_at_link,
                                     const std::vector<std::string>& pinned_titles) {
  std::vector<bool> pinned;
  std::vector<bool> active;
  std::vector<int> group_colors;
  std::vector<bool> group_collapsed;
  std::vector<bool> wakes_at_link;
  base::android::JavaBooleanArrayToBoolVector(env, java_pinned, &pinned);
  base::android::JavaBooleanArrayToBoolVector(env, java_active, &active);
  base::android::JavaIntArrayToIntVector(env, java_group_colors, &group_colors);
  base::android::JavaBooleanArrayToBoolVector(env, java_group_collapsed, &group_collapsed);
  base::android::JavaBooleanArrayToBoolVector(env, java_wakes_at_link, &wakes_at_link);
  std::vector<SavedTab> tabs;
  for (size_t i = 0; i < urls.size(); ++i) {
    SavedTab tab{GURL(urls[i]), base::UTF8ToUTF16(titles[i]), pinned[i], active[i]};
    tab.group_id = group_ids[i];
    tab.group_title = base::UTF8ToUTF16(group_titles[i]);
    tab.group_color = group_colors[i];
    tab.group_collapsed = group_collapsed[i];
    tab.custom.name = base::UTF8ToUTF16(names[i]);
    tab.custom.pinned_url = GURL(pinned_urls[i]);
    tab.custom.wakes_at_link = wakes_at_link[i];
    tab.custom.pinned_title = base::UTF8ToUTF16(pinned_titles[i]);
    if (tab.url.is_valid()) {
      tabs.push_back(std::move(tab));
    }
  }
  WriteSpaceTabs(LocalState(), id, tabs);
}

static bool JNI_SpaceStore_IsBlankUrl(JNIEnv* env, const std::string& url) {
  return IsBlankTabUrl(GURL(url));
}

static std::string JNI_SpaceStore_NextName(JNIEnv* env) {
  return NextSpaceName(Spaces());
}

static std::string JNI_SpaceStore_NextIcon(JNIEnv* env) {
  return NextSpaceIcon(Spaces());
}

static int32_t JNI_SpaceStore_NextHue(JNIEnv* env) {
  return NextSpaceHue(Spaces());
}

static std::vector<std::string> JNI_SpaceStore_OfferedIcons(JNIEnv* env) {
  return SpaceIcons();
}

static std::vector<std::string> JNI_SpaceStore_ColourNames(JNIEnv* env) {
  std::vector<std::string> names;
  for (const SpaceColour& colour : SpaceColours()) {
    names.push_back(base::UTF16ToUTF8(colour.name));
  }
  return names;
}

static ScopedJavaLocalRef<jintArray> JNI_SpaceStore_ColourHues(JNIEnv* env) {
  std::vector<int> hues;
  for (const SpaceColour& colour : SpaceColours()) {
    hues.push_back(colour.hue);
  }
  return ToJava(env, hues);
}

static int32_t JNI_SpaceStore_Tint(JNIEnv* env, int32_t hue) {
  return static_cast<int32_t>(SpaceTintArgb(hue));
}

static int32_t JNI_SpaceStore_NearestColour(JNIEnv* env, int32_t hue) {
  return static_cast<int32_t>(NearestSpaceColour(hue));
}

}  // namespace lb

DEFINE_JNI(SpaceStore)
