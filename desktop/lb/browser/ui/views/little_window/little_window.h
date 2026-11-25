// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_LITTLE_WINDOW_LITTLE_WINDOW_H_
#define LB_BROWSER_UI_VIEWS_LITTLE_WINDOW_LITTLE_WINDOW_H_

#include <memory>
#include <string>

#include "chrome/browser/ui/startup/startup_tab.h"
#include "lb/browser/peek/peek_rules.h"

class BrowserWindowInterface;
class GURL;
class Profile;

namespace base {
class CommandLine;
class FilePath;
}  // namespace base

namespace content {
class WebContents;
struct OpenURLParams;
}  // namespace content

namespace views {
class View;
}  // namespace views

namespace lb {

// The little window (docs/ux.md section 8): Chromium's popup window, a page with no tabs and one
// row above it with the address, "Copy URL" and "Open in <space>". Opened by Cmd+Option+N (the
// command bar, then a new little window) and a link's "Open Link in Little Window". A
// page's own popups (window.open with a size) are little windows too.

// The context menu item (patches/lb/little-window-link-menu.patch).
inline constexpr char16_t kOpenLinkInLittleWindow[] = u"Open Link in Little Window";

// Which kind of window `browser` is, for Peek and the little window.
WindowKind WindowKindOf(const BrowserWindowInterface* browser);

// Opens `url` in a new little window of `profile`, as if typed (the command bar).
void OpenUrlInLittleWindow(Profile* profile, const GURL& url);
// Opens a link in a new little window of `profile`, with the referrer, initiator and source the
// page gave it (a link's "Open Link in Little Window").
void OpenUrlInLittleWindow(Profile* profile, const content::OpenURLParams& open);

// Links from other apps (docs/ux.md section 8), each in a new little window of `profile`:
//   - Opened through macOS (a mail link, `open <url>`;
//   patches/lb/little-window-other-apps-mac.patch):
//     opens those of `tabs` that OpensFromOtherAppInLittleWindow takes, and returns the rest for
//     Chromium to open as tabs.
StartupTabs OpenLinksFromOtherAppsInLittleWindows(Profile* profile, const StartupTabs& tabs);
//   - On the command line of a second launch (patches/lb/little-window-other-apps.patch), in the
//     profile Chromium would open them in (guest or private when asked for): true if they opened in
//     little windows (lb::OpensCommandLineInLittleWindows), so Chromium opens nothing. False leaves
//     the command line to Chromium: no links, a link that opens as a tab, --app, or no window can
//     open in that profile.
bool OpenCommandLineLinksInLittleWindows(Profile* profile,
                                         const base::CommandLine& command_line,
                                         const base::FilePath& cur_dir);

// Makes `contents` the active tab of the profile's most recently used window with tabs, in the
// space that window shows, and brings that window to the front; opens a window if there is none.
// The page isn't reloaded.
BrowserWindowInterface* ShowInSpaceWindow(Profile* profile,
                                          std::unique_ptr<content::WebContents> contents);

// "Open in <space>": moves the page in little window `browser` into a tab (ShowInSpaceWindow).
// The little window closes. False if `browser` isn't a little window with a page.
bool MoveLittleWindowToSpace(BrowserWindowInterface* browser);

// Adds "Copy URL" and "Open in <space>" to the toolbar of `browser` if it is a little window
// (Chromium's toolbar in its location-only mode; patches/lb/little-window-toolbar.patch).
void AddLittleWindowControls(views::View* toolbar, BrowserWindowInterface* browser);

// Cmd+Option+N: the command bar, over `browser`, where what is chosen opens in a new little
// window. In a window without our sidebar (a little window), the bar comes from the profile's
// window with tabs, and with none, from a new one. Always handles the command.
bool ShowLittleWindowCommandBar(BrowserWindowInterface* browser);

// The "Open in <space>" button's label in little window `browser`, or empty if it has none, and
// a click on it, as the mouse does (false if there is none). For --lb-sidebar-demo=peek.
std::u16string GetOpenInSpaceLabelForTesting(BrowserWindowInterface* browser);
bool ClickOpenInSpaceForTesting(BrowserWindowInterface* browser);
// Clicks "Copy URL", as the mouse does. False if the window has none.
bool ClickCopyUrlForTesting(BrowserWindowInterface* browser);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_LITTLE_WINDOW_LITTLE_WINDOW_H_
