// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/little_window/little_window_menu.h"

#import <AppKit/AppKit.h>

#include <optional>
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
#include "lb/browser/ui/views/search/search_choice_dialog.h"
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
constexpr char kEditedPage[] = "file:///lb-little-window-menu-edited.html";
constexpr int kTries = 100;  // every 100 ms

// The browser window whose NSWindow is `window`, or null.
BrowserWindowInterface* BrowserOf(NSWindow* window) {
  BrowserWindowInterface* found = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation([&](BrowserWindowInterface* b) {
    if (b->GetWindow() && b->GetWindow()->GetNativeWindow().GetNativeNSWindow() == window) {
      found = b;
    }
    return !found;
  });
  return found;
}

// The Window menu's item for `window` (AppKit's, whose target is the window), or nil.
NSMenuItem* WindowsMenuItemFor(NSWindow* window) {
  for (NSMenuItem* item in NSApp.windowsMenu.itemArray) {
    if (item.target == window) {
      return item;
    }
  }
  return nil;
}

// A little window's mark in the Window menu: the system's picture-in-picture symbol.
NSImage* LittleWindowMenuImage() {
  static NSImage* image = [NSImage imageWithSystemSymbolName:@"pip"
                                    accessibilityDescription:@"Little window"];
  return image;
}

// Gives `item`, if it is the Window menu's item for a window, the little window mark when that
// window is a little window, and none otherwise. Sets it only when it changes: a change notifies
// the menu again.
void MarkWindowsMenuItem(NSMenuItem* item) {
  NSWindow* window = [item.target isKindOfClass:[NSWindow class]] ? item.target : nil;
  if (!window || item.menu != NSApp.windowsMenu) {
    return;
  }
  BrowserWindowInterface* browser = BrowserOf(window);
  NSImage* image =
      browser && WindowKindOf(browser) == WindowKind::kLittle ? LittleWindowMenuImage() : nil;
  if (item.image != image) {
    item.image = image;
  }
}

void MarkWindowsMenu() {
  for (NSMenuItem* item in NSApp.windowsMenu.itemArray) {
    MarkWindowsMenuItem(item);
  }
}

// AppKit adds a window's item to the Window menu when the window is shown, and updates it when
// its title changes: each time, the item gets its mark.
void WatchWindowsMenu() {
  static bool watching = false;
  if (watching) {
    return;
  }
  watching = true;
  NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
  for (NSNotificationName name : {NSMenuDidAddItemNotification, NSMenuDidChangeItemNotification}) {
    [center addObserverForName:name
                        object:nil
                         queue:nil
                    usingBlock:^(NSNotification* note) {
                      NSMenu* menu = note.object;
                      if (menu != NSApp.windowsMenu) {
                        return;
                      }
                      const NSInteger index =
                          [note.userInfo[@"NSMenuItemIndex"] integerValue];
                      if (index >= 0 && index < menu.numberOfItems) {
                        MarkWindowsMenuItem([menu itemAtIndex:index]);
                      }
                    }];
  }
  [center addObserverForName:NSMenuDidBeginTrackingNotification
                      object:nil
                       queue:nil
                  usingBlock:^(NSNotification* note) {
                    MarkWindowsMenu();
                  }];
}

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

// The profile's one little window, or null (none, or more than one).
BrowserWindowInterface* OnlyLittleWindow() {
  BrowserWindowInterface* little = nullptr;
  int little_windows = 0;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation([&](BrowserWindowInterface* b) {
    if (WindowKindOf(b) == WindowKind::kLittle) {
      little = b;
      ++little_windows;
    }
    return true;
  });
  return little_windows == 1 ? little : nullptr;
}

// Whether `above`'s window is in front of `below`'s.
bool InFrontOf(BrowserWindowInterface* above, BrowserWindowInterface* below) {
  NSWindow* front = above->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  NSWindow* back = below->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  for (NSWindow* window in NSApp.orderedWindows) {  // front to back
    if (window == front) {
      return true;
    }
    if (window == back) {
      return false;
    }
  }
  return false;
}

// A search with no engine chosen yet, typed in the little window's address: the engine is
// chosen, and the search opens in that little window, not a new one.
void WaitForSearchInLittleWindow(base::WeakPtr<BrowserWindowInterface> browser,
                                 std::string detail,
                                 bool chose,
                                 int tries) {
  if (!chose && IsSearchChoiceOpenForTesting()) {
    chose = ChooseSearchEngineForTesting(0);
    detail += std::string(" engine_chosen=") + (chose ? "1" : "0");
  }
  BrowserWindowInterface* little = OnlyLittleWindow();
  content::WebContents* shown =
      little ? little->GetTabStripModel()->GetActiveWebContents() : nullptr;
  const bool searched = shown && shown->GetVisibleURL().SchemeIsHTTPOrHTTPS();
  if (searched || tries == 0) {
    ReportDemo(kMode, searched, detail + " search_in_place=" + (searched ? "1" : "0"));
    return;
  }
  PostDemoTask(base::BindOnce(&WaitForSearchInLittleWindow, browser, detail, chose, tries - 1),
               base::Milliseconds(100));
}

// Esc in the bar the little window's address opened: the little window comes back in front of
// the window the bar showed over.
void CheckLittleWindowBackInFront(base::WeakPtr<BrowserWindowInterface> browser,
                                  std::string detail) {
  BrowserWindowInterface* little = OnlyLittleWindow();
  const bool in_front = browser && little && InFrontOf(little, browser.get());
  detail += std::string(" back_in_front_after_esc=") + (in_front ? "1" : "0");
  if (!in_front) {
    ReportDemo(kMode, false, detail);
    return;
  }
  const bool clicked = ClickLittleWindowAddressForTesting(little);
  const bool ran = clicked && RunFirstCommandBarResultForTesting(u"lb little window search");
  detail += std::string(" search_typed=") + (ran ? "1" : "0");
  if (!ran) {
    ReportDemo(kMode, false, detail);
    return;
  }
  WaitForSearchInLittleWindow(browser, detail, /*chose=*/false, kTries);
}

// The one little window shows the edited address: no second little window opened.
void WaitForEditedLittleWindow(base::WeakPtr<BrowserWindowInterface> browser,
                               std::string detail,
                               int tries) {
  BrowserWindowInterface* little = OnlyLittleWindow();
  const bool edited = browser && little && HasLittleWindowAt(browser.get(), GURL(kEditedPage));
  if (edited || tries == 0) {
    detail += std::string(" edited_in_place=") + (edited ? "1" : "0");
    if (!edited) {
      ReportDemo(kMode, false, detail);
      return;
    }
    // Its address again, then Esc.
    const bool reopened =
        ClickLittleWindowAddressForTesting(little) && CloseCommandBarForTesting();
    detail += std::string(" bar_again=") + (reopened ? "1" : "0");
    if (!reopened) {
      ReportDemo(kMode, false, detail);
      return;
    }
    PostDemoTask(base::BindOnce(&CheckLittleWindowBackInFront, browser, detail),
                 base::Milliseconds(500));
    return;
  }
  PostDemoTask(base::BindOnce(&WaitForEditedLittleWindow, browser, detail, tries - 1),
               base::Milliseconds(100));
}

void WaitForLittleWindow(base::WeakPtr<BrowserWindowInterface> browser,
                         std::string detail,
                         int tries) {
  const bool opened = browser && HasLittleWindowAt(browser.get(), GURL(kPage));
  if (opened || tries == 0) {
    // In the Window menu the little window has its mark and the browser window none.
    NSWindow* little = nil;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation([&](BrowserWindowInterface* b) {
      if (WindowKindOf(b) == WindowKind::kLittle) {
        little = b->GetWindow()->GetNativeWindow().GetNativeNSWindow();
      }
      return !little;
    });
    NSMenuItem* little_item = little ? WindowsMenuItemFor(little) : nil;
    NSMenuItem* browser_item =
        browser ? WindowsMenuItemFor(
                      browser->GetWindow()->GetNativeWindow().GetNativeNSWindow())
                : nil;
    const bool marked = little_item && little_item.image == LittleWindowMenuImage() &&
                        browser_item && !browser_item.image;
    detail += std::string(" little_window=") + (opened ? "1" : "0") +
              " window_menu_marks=" + (marked ? "1" : "0");
    // A click on its address opens the command bar holding it; an address entered there opens
    // in the same little window.
    BrowserWindowInterface* little_browser = nullptr;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation([&](BrowserWindowInterface* b) {
      if (WindowKindOf(b) == WindowKind::kLittle) {
        little_browser = b;
      }
      return !little_browser;
    });
    const bool clicked = little_browser && ClickLittleWindowAddressForTesting(little_browser);
    const std::optional<std::u16string> text = CommandBarTextForTesting();
    const bool prefilled = text && *text == base::UTF8ToUTF16(std::string_view(kPage));
    const bool edited =
        prefilled && RunFirstCommandBarResultForTesting(base::UTF8ToUTF16(std::string_view(kEditedPage)));
    detail += std::string(" address_click=") + (clicked ? "1" : "0") +
              " bar_holds_address=" + (prefilled ? "1" : "0") +
              (prefilled ? "" : " bar_text=" + (text ? base::UTF16ToUTF8(*text) : "none"));
    if (!opened || !marked || !edited) {
      ReportDemo(kMode, false, detail);
      return;
    }
    WaitForEditedLittleWindow(browser, detail, kTries);
    return;
  }
  PostDemoTask(base::BindOnce(&WaitForLittleWindow, browser, detail, tries - 1),
               base::Milliseconds(100));
}

}  // namespace

void AddLittleWindowMenuItem(NSMenu* file_menu) {
  WatchWindowsMenu();
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
