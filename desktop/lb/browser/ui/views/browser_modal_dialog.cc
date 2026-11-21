// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/browser_modal_dialog.h"

#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/constrained_window/constrained_window_views.h"
#include "lb/browser/ui/lb_ui_snapshot.h"
#include "ui/base/base_window.h"

namespace lb {

BrowserModalDialog::BrowserModalDialog() = default;

BrowserModalDialog::~BrowserModalDialog() = default;

// static
views::Widget* BrowserModalDialog::Show(std::unique_ptr<BrowserModalDialog> dialog,
                                        BrowserWindowInterface* browser) {
  // Deleted in OnWidgetDestroyed.
  BrowserModalDialog* owned = dialog.release();
  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      owned, browser->GetWindow()->GetNativeWindow());
  owned->observation_.Observe(widget);
  widget->Show();
  MaybeScheduleUiSnapshot(widget, "lb-ui-snapshot-dialog");
  return widget;
}

void BrowserModalDialog::OnWidgetDestroyed(views::Widget* widget) {
  observation_.Reset();
  OnClosed();
  base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, this);
}

}  // namespace lb
