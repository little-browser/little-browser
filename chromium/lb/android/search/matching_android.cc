// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The native side of app.littlebrowser.lb.search.Matching: the command bar's matching through the
// shared C++ (lb/browser/command_bar/command_matching.h, over lb-core), tested in lb_unittests and
// lb-core. This file only converts types.

#include <jni.h>

#include <string>

#include "base/android/jni_string.h"
#include "lb/android/jni_headers/Matching_jni.h"
#include "lb/browser/command_bar/command_matching.h"

namespace lb {

static double JNI_Matching_Score(JNIEnv* env,
                                 const std::u16string& query,
                                 const std::u16string& text) {
  return MatchScore(query, text);
}

static double JNI_Matching_OpenTabBoost(JNIEnv* env) {
  return ResultBoost(CommandBarResult::kOpenTab);
}

}  // namespace lb

DEFINE_JNI(Matching)
