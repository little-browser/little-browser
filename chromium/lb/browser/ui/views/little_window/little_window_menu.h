// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_LITTLE_WINDOW_LITTLE_WINDOW_MENU_H_
#define LB_BROWSER_UI_VIEWS_LITTLE_WINDOW_LITTLE_WINDOW_MENU_H_

class BrowserWindowInterface;

#ifdef __OBJC__
@class NSMenu;
#else
class NSMenu;
#endif

namespace lb {

// The File menu's "New little window" (⌥⌘N, IDC_NEW_LITTLE_WINDOW; docs/ux.md §8).
inline constexpr char16_t kNewLittleWindowMenuTitle[] = u"New little window";

// Adds "New little window" to the macOS File menu `file_menu`, right after "New Incognito window"
// (patches/lb/little-window-menu.patch). It runs the same command as ⌥⌘N, in every browser
// window (ShowLittleWindowCommandBar).
// From then on a little window's item in the Window menu has the system's picture-in-picture
// symbol beside its title, so it can be told from the browser windows, which have none.
void AddLittleWindowMenuItem(NSMenu* file_menu);

// --lb-sidebar-demo=little-window-menu: the File menu has "New little window" after "New
// Incognito window" with ⌥⌘N, the window enables it, and choosing it opens the command bar for a
// little window, where an address opens one, which the Window menu marks. Logs "lb demo: little-window-menu PASS ..." or
// "... FAIL ...".
void RunLittleWindowMenuDemo(BrowserWindowInterface* browser);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_LITTLE_WINDOW_LITTLE_WINDOW_MENU_H_
