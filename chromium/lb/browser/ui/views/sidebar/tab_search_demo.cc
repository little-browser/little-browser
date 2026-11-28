// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/sidebar/tab_search_demo.h"

#include <string>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "lb/browser/demo/demo_report.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/models/menu_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"

namespace lb {

namespace {

constexpr char kMode[] = "tab-search";

bool IsPinned(Profile* profile) {
  return profile->GetPrefs()->GetBoolean(prefs::kTabSearchPinnedToTabstrip);
}

// The tab search button in `browser`'s window, or null.
views::View* TabSearchButton(BrowserWindowInterface* browser) {
  for (views::View* view : ViewTreeForDemo(WindowViewForDemo(browser))) {
    if (view->GetProperty(views::kElementIdentifierKey) == kTabSearchButtonElementId) {
      return view;
    }
  }
  return nullptr;
}

// After the right-click: no menu may be open.
void CheckNoMenu(base::WeakPtr<Profile> profile, std::string detail) {
  const bool menu_open = views::MenuController::GetActiveInstance() != nullptr;
  if (menu_open) {
    views::MenuController::GetActiveInstance()->Cancel(views::MenuController::ExitType::kAll);
  }
  ReportDemo(kMode, !menu_open && !detail.empty() && detail.find("FAIL") == std::string::npos,
             detail + " right_click_menu=" + (menu_open ? "1" : "0"));
}

// Whether `browser`'s window menu (a right-click on the title area) has the tab search toggle.
bool WindowMenuHasTabSearchToggle(BrowserWindowInterface* browser) {
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser);
  ui::MenuModel* menu = view ? view->browser_widget()->GetSystemMenuModel() : nullptr;
  for (size_t i = 0; menu && i < menu->GetItemCount(); ++i) {
    if (menu->GetCommandIdAt(i) == IDC_TAB_SEARCH_TOGGLE_PIN) {
      return true;
    }
  }
  return false;
}

// In the second window: the saved "unpinned" was cleared, the button is shown, the toggle
// command does nothing, the window menu has no toggle, and a right-click opens no menu.
void CheckSecondWindow(base::WeakPtr<Profile> profile,
                       base::WeakPtr<BrowserWindowInterface> first) {
  if (!profile) {
    ReportDemo(kMode, false, "the profile went away");
    return;
  }
  BrowserWindowInterface* browser = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation([&](BrowserWindowInterface* b) {
    if (b->GetProfile() == profile.get() && b != first.get()) {
      browser = b;
      return false;
    }
    return true;
  });
  views::View* button = browser ? TabSearchButton(browser) : nullptr;
  const bool cleared = IsPinned(profile.get());
  const bool shown = button && button->IsDrawn();
  if (browser) {
    chrome::ExecuteCommand(browser, IDC_TAB_SEARCH_TOGGLE_PIN);
  }
  const bool still_pinned = IsPinned(profile.get());
  const bool window_menu_toggle = browser && WindowMenuHasTabSearchToggle(browser);
  std::string detail = std::string("cleared=") + (cleared ? "1" : "0") +
                       " shown=" + (shown ? "1" : "0") +
                       " pinned_after_toggle=" + (still_pinned ? "1" : "0") +
                       " window_menu_toggle=" + (window_menu_toggle ? "1" : "0");
  if (!cleared || !shown || !still_pinned || window_menu_toggle) {
    detail += " FAIL";
  }
  if (button) {
    button->ShowContextMenu(button->GetBoundsInScreen().CenterPoint(),
                            ui::mojom::MenuSourceType::kMouse);
  }
  PostDemoTask(base::BindOnce(&CheckNoMenu, profile, detail), base::Seconds(1));
}

}  // namespace

void RunTabSearchDemo(BrowserWindowInterface* browser) {
  FailDemoAfter(kMode, base::Seconds(20));
  Profile* profile = browser->GetProfile();
  // "Unpinned", as Chromium saved it before; a window opening must clear it.
  profile->GetPrefs()->SetBoolean(prefs::kTabSearchPinnedToTabstrip, false);
  chrome::NewEmptyWindow(profile);
  PostDemoTask(base::BindOnce(&CheckSecondWindow, profile->GetWeakPtr(), browser->GetWeakPtr()),
               base::Seconds(3));
}

}  // namespace lb
