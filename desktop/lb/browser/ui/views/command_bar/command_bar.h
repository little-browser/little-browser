// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_COMMAND_BAR_COMMAND_BAR_H_
#define LB_BROWSER_UI_VIEWS_COMMAND_BAR_COMMAND_BAR_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace lb {

class CommandBarView;

// The command bar (docs/ux.md §4): a palette floating over the page. Cmd+T opens it empty
// (results open in a new tab), Cmd+L / the address pill open it with the current address
// (results replace the page). Lists open tabs across spaces, history, commands
// and a web address or search. It also hosts "Rename space".
class CommandBar : public views::WidgetObserver {
 public:
  CommandBar(SidebarController* controller, views::Widget* browser_widget);
  CommandBar(const CommandBar&) = delete;
  CommandBar& operator=(const CommandBar&) = delete;
  ~CommandBar() override;

  void Show(CommandBarMode mode);
  void Close();
  bool IsShowing() const { return !!widget_; }

  // RunCommandBarForTesting() for this bar.
  bool RunForTesting(const std::u16string& text);

  // Called by the view when its preferred height changes.
  void UpdateBounds();

  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  raw_ptr<SidebarController> controller_;
  raw_ptr<views::Widget> browser_widget_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<CommandBarView> view_ = nullptr;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{this};
  base::WeakPtrFactory<CommandBar> weak_factory_{this};
};

// Types `text` into the open command bar and runs the shown result titled `text`, as choosing it
// does; false if no bar is open or no such result is shown (the --lb-sidebar-demo=ad-blocker
// check).
bool RunCommandBarForTesting(const std::u16string& text);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_COMMAND_BAR_COMMAND_BAR_H_
