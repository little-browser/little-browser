// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_BROWSER_MODAL_DIALOG_H_
#define LB_BROWSER_UI_VIEWS_BROWSER_MODAL_DIALOG_H_

#include <memory>

#include "base/scoped_observation.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/views/window/dialog_delegate.h"

class BrowserWindowInterface;

namespace lb {

// A dialog modal to a browser window. Browser-modal dialogs don't own their delegate (only
// allow-listed classes may ask the widget to), so this one deletes itself once its widget is
// gone.
class BrowserModalDialog : public views::DialogDelegate, public views::WidgetObserver {
 public:
  // Shows `dialog` over `browser`'s window; from then on it deletes itself when it closes.
  // Returns its widget. A --lb-ui-snapshot-dialog capture is scheduled for it.
  static views::Widget* Show(std::unique_ptr<BrowserModalDialog> dialog,
                             BrowserWindowInterface* browser);

  BrowserModalDialog(const BrowserModalDialog&) = delete;
  BrowserModalDialog& operator=(const BrowserModalDialog&) = delete;
  ~BrowserModalDialog() override;

 protected:
  BrowserModalDialog();

  // Runs once the widget is gone, just before the dialog is deleted.
  virtual void OnClosed() {}

 private:
  // views::WidgetObserver:
  void OnWidgetDestroyed(views::Widget* widget) override;

  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{this};
};

// Whether a first-run question may open over `browser` now: not while the window closes or the app
// quits (CanAskFirstRunQuestion in lb/browser/first_run/first_run_questions.h).
bool CanAskFirstRunQuestionIn(BrowserWindowInterface* browser);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_BROWSER_MODAL_DIALOG_H_
