// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_DEMO_DEMO_REPORT_H_
#define LB_BROWSER_DEMO_DEMO_REPORT_H_

#include <string>

#include "base/functional/callback_forward.h"
#include "base/time/time.h"

namespace lb {

// Helpers for --lb-sidebar-demo modes that check behaviour in a running browser
// (desktop/lb/README.md).

// Logs "lb demo: <mode> PASS <detail>" or "... FAIL ...", once per mode: whichever comes second
// of a result and its deadline is dropped. Logged without --vmodule.
void ReportDemo(const std::string& mode, bool pass, const std::string& detail);

// Reports `mode` as failed if it has no result after `limit`.
void FailDemoAfter(const std::string& mode, base::TimeDelta limit);

// Runs `task` on the UI thread after `delay`.
void PostDemoTask(base::OnceClosure task, base::TimeDelta delay);

}  // namespace lb

#endif  // LB_BROWSER_DEMO_DEMO_REPORT_H_
