// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/little_window/little_window_menu.h"

#import <AppKit/AppKit.h>

#include <string>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "lb/browser/demo/demo_report.h"
#include "lb/browser/peek/peek_rules.h"
#include "lb/browser/ui/lb_command_ids.h"
#include "lb/browser/ui/views/command_bar/command_bar.h"
#include "lb/browser/ui/views/little_window/little_window.h"
#include "ui/gfx/native_ui_types.h"
#include "url/gurl.h"

// Chromium's menu items send this to the window (chrome/browser/ui/cocoa/main_menu_builder.mm).
@protocol LBCommandDispatch
- (void)commandDispatch:(id)sender;
@end

namespace lb {

namespace {

constexpr char kMode[] = "little-window-menu";
// Never loaded: the little window only has to show it.
constexpr char kPage[] = "file:///lb-little-window-menu-demo.html";
constexpr int kTries = 100;  // every 100 ms

bool HasLittleWindowAt(BrowserWindowInterface* browser, const GURL& url) {
  bool found = false;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation([&](BrowserWindowInterface* b) {
    content::WebContents* shown = b->GetTabStripModel()->GetActiveWebContents();
    found = b->GetProfile() == browser->GetProfile() && WindowKindOf(b) == WindowKind::kLittle &&
            shown && shown->GetVisibleURL() == url;
    return !found;
  });
  return found;
}

void WaitForLittleWindow(base::WeakPtr<BrowserWindowInterface> browser,
                         std::string detail,
                         int tries) {
  const bool opened = browser && HasLittleWindowAt(browser.get(), GURL(kPage));
  if (opened || tries == 0) {
    ReportDemo(kMode, opened, detail + " little_window=" + (opened ? "1" : "0"));
    return;
  }
  PostDemoTask(base::BindOnce(&WaitForLittleWindow, browser, detail, tries - 1),
               base::Milliseconds(100));
}

}  // namespace

void AddLittleWindowMenuItem(NSMenu* file_menu) {
  NSMenuItem* item =
      [[NSMenuItem alloc] initWithTitle:base::SysUTF16ToNSString(kNewLittleWindowMenuTitle)
                                 action:@selector(commandDispatch:)
                          keyEquivalent:@"n"];
  item.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
  item.tag = IDC_NEW_LITTLE_WINDOW;
  const NSInteger incognito = [file_menu indexOfItemWithTag:IDC_NEW_INCOGNITO_WINDOW];
  [file_menu insertItem:item atIndex:incognito < 0 ? 0 : incognito + 1];
}

void RunLittleWindowMenuDemo(BrowserWindowInterface* browser) {
  NSMenu* file = [[NSApp.mainMenu itemWithTag:IDC_FILE_MENU] submenu];
  NSMenuItem* item = [file itemWithTag:IDC_NEW_LITTLE_WINDOW];
  if (!item) {
    ReportDemo(kMode, false, "the File menu has no New little window");
    return;
  }
  const bool placed =
      [file indexOfItem:item] == [file indexOfItemWithTag:IDC_NEW_INCOGNITO_WINDOW] + 1;
  const bool titled =
      base::SysNSStringToUTF16(item.title) == std::u16string(kNewLittleWindowMenuTitle);
  const bool shortcut =
      [item.keyEquivalent isEqualToString:@"n"] &&
      item.keyEquivalentModifierMask == (NSEventModifierFlagCommand | NSEventModifierFlagOption);
  NSWindow* window = browser->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  const bool enabled = [window validateUserInterfaceItem:item];
  // Choosing the item, as the menu does: its action goes to the window.
  [static_cast<id<LBCommandDispatch>>(window) commandDispatch:item];
  const bool ran = RunCommandBarForTesting(base::UTF8ToUTF16(std::string_view(kPage)));
  std::string detail = std::string("placed=") + (placed ? "1" : "0") +
                       " titled=" + (titled ? "1" : "0") + " shortcut=" + (shortcut ? "1" : "0") +
                       " enabled=" + (enabled ? "1" : "0") + " command_bar=" + (ran ? "1" : "0");
  if (!placed || !titled || !shortcut || !enabled || !ran) {
    ReportDemo(kMode, false, detail);
    return;
  }
  WaitForLittleWindow(browser->GetWeakPtr(), detail, kTries);
}

}  // namespace lb
