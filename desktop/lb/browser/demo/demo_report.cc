// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/demo/demo_report.h"

#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "lb/browser/ui/views/importer/import_dialog.h"
#include "lb/browser/ui/views/password/password_manager_choice_dialog.h"
#include "lb/browser/ui/views/search/search_choice_dialog.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/views/view.h"

namespace lb {

void ReportDemo(const std::string& mode, bool pass, const std::string& detail) {
  static base::NoDestructor<std::set<std::string>> reported;
  if (!reported->insert(mode).second) {
    return;
  }
  // LOG, not VLOG: the result must be in the log without --vmodule.
  LOG(WARNING) << "lb demo: " << mode << (pass ? " PASS " : " FAIL ") << detail;
}

void FailDemoAfter(const std::string& mode, base::TimeDelta limit) {
  PostDemoTask(base::BindOnce(&ReportDemo, mode, false,
                              "no result after " + base::NumberToString(limit.InSeconds()) + " s"),
               limit);
}

void PostDemoTask(base::OnceClosure task, base::TimeDelta delay) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE, std::move(task),
                                                                     delay);
}

void SkipFirstRunQuestionsForDemo(Profile* profile) {
  g_browser_process->local_state()->SetBoolean(kImportOfferedPref, true);
  profile->GetPrefs()->SetBoolean(kSearchChoiceShownPref, true);
  profile->GetPrefs()->SetBoolean(kPasswordManagerChoiceShownPref, true);
}

void ClickViewForDemo(views::View* view, std::optional<gfx::Point> point) {
  const gfx::Point at = point.value_or(view->GetLocalBounds().CenterPoint());
  view->OnMousePressed(ui::MouseEvent(ui::EventType::kMousePressed, at, at, ui::EventTimeForNow(),
                                      ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON));
  view->OnMouseReleased(ui::MouseEvent(ui::EventType::kMouseReleased, at, at, ui::EventTimeForNow(),
                                       ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON));
}

}  // namespace lb
