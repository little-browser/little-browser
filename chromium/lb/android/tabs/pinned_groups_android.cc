// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The native side of app.littlebrowser.lb.tabs.PinnedGroups: the pinned-group rules through the
// shared C++ (lb/browser/tabs/pinned_groups.h, over lb-core), tested in lb_unittests and lb-core.
// This file only converts types.

#include <jni.h>

#include <cstdint>
#include <string>
#include <vector>

#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "lb/android/jni_headers/PinnedGroups_jni.h"
#include "lb/browser/tabs/pinned_groups.h"

namespace lb {

using base::android::JavaRef;
using base::android::ScopedJavaLocalRef;

namespace {

std::vector<bool> ToBools(JNIEnv* env, const JavaRef<jbooleanArray>& array) {
  std::vector<bool> values;
  base::android::JavaBooleanArrayToBoolVector(env, array, &values);
  return values;
}

}  // namespace

static std::vector<std::string> JNI_PinnedGroups_Normalize(JNIEnv* env,
                                                           const std::vector<std::string>& groups) {
  return NormalizePinnedGroups(groups);
}

static ScopedJavaLocalRef<jintArray> JNI_PinnedGroups_Rows(
    JNIEnv* env,
    const std::vector<std::string>& item_groups,
    const JavaRef<jbooleanArray>& java_item_active,
    const std::vector<std::string>& group_ids,
    const JavaRef<jbooleanArray>& java_collapsed) {
  const std::vector<bool> active = ToBools(env, java_item_active);
  const std::vector<bool> collapsed = ToBools(env, java_collapsed);
  std::vector<PinnedItem> items;
  for (size_t i = 0; i < item_groups.size() && i < active.size(); ++i) {
    items.push_back({item_groups[i], active[i]});
  }
  std::vector<PinnedGroup> groups;
  for (size_t i = 0; i < group_ids.size() && i < collapsed.size(); ++i) {
    PinnedGroup group;
    group.id = group_ids[i];
    group.collapsed = collapsed[i];
    groups.push_back(group);
  }
  std::vector<int> flat;
  for (const PinnedRow& row : PinnedRows(items, groups)) {
    flat.push_back(row.header ? static_cast<int>(*row.header) : -1);
    flat.push_back(static_cast<int>(row.item));
    flat.push_back(row.indented ? 1 : 0);
    flat.push_back(row.shown ? 1 : 0);
  }
  return base::android::ToJavaIntArray(env, flat);
}

static ScopedJavaLocalRef<jintArray> JNI_PinnedGroups_Menu(JNIEnv* env, bool all_tabs_asleep) {
  std::vector<int> commands;
  for (PinnedGroupCommand command : PinnedGroupMenu(all_tabs_asleep)) {
    // PinnedGroups.Command's values, by name.
    switch (command) {
      case PinnedGroupCommand::kRename:
        commands.push_back(0);
        break;
      case PinnedGroupCommand::kColor:
        commands.push_back(1);
        break;
      case PinnedGroupCommand::kUnpin:
        commands.push_back(2);
        break;
      case PinnedGroupCommand::kUngroup:
        commands.push_back(3);
        break;
      case PinnedGroupCommand::kClose:
        commands.push_back(4);
        break;
      case PinnedGroupCommand::kRemove:
        commands.push_back(5);
        break;
    }
  }
  return base::android::ToJavaIntArray(env, commands);
}

static ScopedJavaLocalRef<jintArray> JNI_PinnedGroups_Close(
    JNIEnv* env,
    const JavaRef<jbooleanArray>& java_asleep,
    const JavaRef<jbooleanArray>& java_hidden_after_close) {
  const std::vector<bool> asleep = ToBools(env, java_asleep);
  const std::vector<bool> hidden = ToBools(env, java_hidden_after_close);
  std::vector<PinnedGroupTab> tabs;
  for (size_t i = 0; i < asleep.size() && i < hidden.size(); ++i) {
    tabs.push_back({asleep[i], hidden[i]});
  }
  const PinnedGroupClose close = DecidePinnedGroupClose(tabs);
  std::vector<int> result = {close.remove ? 1 : 0};
  for (size_t tab : close.tabs) {
    result.push_back(static_cast<int>(tab));
  }
  return base::android::ToJavaIntArray(env, result);
}

static std::string JNI_PinnedGroups_GroupAfterMove(JNIEnv* env,
                                                   const std::string& own,
                                                   const std::string& before,
                                                   const std::string& after,
                                                   bool own_has_other_items) {
  return GroupAfterMove(own, before, after, own_has_other_items);
}

static int32_t JNI_PinnedGroups_NewGroupPosition(JNIEnv* env,
                                                 const std::vector<std::string>& groups,
                                                 const JavaRef<jbooleanArray>& java_selected) {
  return static_cast<int32_t>(NewPinnedGroupPosition(groups, ToBools(env, java_selected)));
}

}  // namespace lb

DEFINE_JNI(PinnedGroups)
