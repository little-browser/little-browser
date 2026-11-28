// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The native side of app.littlebrowser.lb.tabs.TabRules: lb-core's tab rules through their C++
// binding (lb/core_bridge/tabs.rs), which only converts types. The rules are tested in lb-core;
// PageInput reads the page state they take.

#include <jni.h>

#include <cstdint>
#include <vector>

#include "base/android/jni_array.h"
#include "base/android/scoped_java_ref.h"
#include "base/memory/weak_ptr.h"
#include "components/performance_manager/public/graph/page_node.h"
#include "components/performance_manager/public/performance_manager.h"
#include "content/public/browser/web_contents.h"
#include "lb/android/jni_headers/TabRules_jni.h"
#include "lb/core_bridge/tabs.rs.h"

namespace lb {

using base::android::JavaRef;

static int32_t JNI_TabRules_DecideClose(JNIEnv* env,
                                        bool user_gesture,
                                        bool closing_all,
                                        bool pinned,
                                        bool split,
                                        bool asleep,
                                        bool shown,
                                        bool hidden_by_earlier_close,
                                        bool sleep_pending,
                                        bool asks_before_leaving,
                                        bool had_user_edits) {
  const core::PinnedCloseState state{
      .user_gesture = user_gesture,
      .closing_all = closing_all,
      .pinned = pinned,
      .split = split,
      .asleep = asleep,
      .shown = shown,
      .hidden_by_earlier_close = hidden_by_earlier_close,
      .sleep_pending = sleep_pending,
      .asks_before_leaving = asks_before_leaving,
      .had_user_edits = had_user_edits,
  };
  // TabRules.CloseOutcome's values, by name: the bridge's enum may be reordered.
  switch (core::decide_pinned_close(state)) {
    case core::PinnedCloseOutcome::Close:
      return 0;
    case core::PinnedCloseOutcome::CloseAskingFirst:
      return 1;
    case core::PinnedCloseOutcome::Sleep:
      return 2;
    case core::PinnedCloseOutcome::Hide:
      return 3;
    case core::PinnedCloseOutcome::AlreadySleeping:
      return 4;
  }
  return 0;
}

static int32_t JNI_TabRules_ChooseTabToShow(JNIEnv* env,
                                            const JavaRef<jbooleanArray>& java_asleep,
                                            const JavaRef<jlongArray>& java_last_focused,
                                            const JavaRef<jbooleanArray>& java_in_collapsed_group) {
  std::vector<bool> asleep;
  std::vector<int64_t> last_focused;
  std::vector<bool> in_collapsed_group;
  base::android::JavaBooleanArrayToBoolVector(env, java_asleep, &asleep);
  base::android::JavaLongArrayToInt64Vector(env, java_last_focused, &last_focused);
  base::android::JavaBooleanArrayToBoolVector(env, java_in_collapsed_group, &in_collapsed_group);
  std::vector<core::TabToShowCandidate> others;
  for (size_t i = 0; i < asleep.size(); ++i) {
    others.push_back({.asleep = asleep[i],
                      .last_focused = last_focused[i],
                      .in_collapsed_group = in_collapsed_group[i]});
  }
  return static_cast<int32_t>(core::choose_tab_to_show(
      rust::Slice<const core::TabToShowCandidate>(others.data(), others.size())));
}

// Whether the page may hold unsaved input, read as the desktop's pinned close does
// (lb/browser/ui/tabs/pinned_tab_sleep.cc): TabRules.ASKS_BEFORE_LEAVING | HAD_USER_EDITS.
static int32_t JNI_TabRules_PageInput(JNIEnv* env, const JavaRef<jobject>& java_contents) {
  content::WebContents* contents = content::WebContents::FromJavaWebContents(java_contents);
  if (!contents) {
    return 0;
  }
  int32_t input = contents->NeedToFireBeforeUnload() ? 1 : 0;
  const base::WeakPtr<performance_manager::PageNode> page =
      performance_manager::PerformanceManager::GetPrimaryPageNodeForWebContents(contents);
  if (page && (page->HadUserEdits() || page->HadFormInteraction())) {
    input |= 2;
  }
  return input;
}

}  // namespace lb

DEFINE_JNI(TabRules)
