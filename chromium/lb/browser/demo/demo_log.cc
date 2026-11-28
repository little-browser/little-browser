// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/demo/demo_log.h"

#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"

namespace lb {

bool ReportDemo(const std::string& mode, bool pass, const std::string& detail) {
  static base::NoDestructor<std::set<std::string>> reported;
  if (!reported->insert(mode).second) {
    return false;
  }
  // LOG, not VLOG: the result must be in the log without --vmodule.
  LOG(WARNING) << "lb demo: " << mode << (pass ? " PASS " : " FAIL ") << detail;
  return true;
}

void FailDemoAfter(const std::string& mode, base::TimeDelta limit) {
  PostDemoTask(base::BindOnce(base::IgnoreResult(&ReportDemo), mode, false,
                              "no result after " + base::NumberToString(limit.InSeconds()) + " s"),
               limit);
}

void PostDemoTask(base::OnceClosure task, base::TimeDelta delay) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE, std::move(task),
                                                                     delay);
}

}  // namespace lb
