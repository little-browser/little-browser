// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_PEEK_PEEK_H_
#define LB_BROWSER_UI_VIEWS_PEEK_PEEK_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback_forward.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/rect.h"

class BrowserWindowInterface;

namespace content {
class NavigationHandle;
class WebContents;
struct OpenURLParams;
}  // namespace content

namespace lb {

// Peek (docs/ux.md section 7): Shift+click a link and it opens in a card as tall as the window
// (less a margin, with the page's site above the card) and a little narrower than the page, which
// grows from the click point while the window dims. Round buttons beside it: "Close", "Open as
// tab" (the page becomes a tab and isn't reloaded) and "Open in split view" (the same, paired
// with the page under it); Esc, Cmd+W or a click on the backdrop also close it, after the page's
// own "Leave site?" if it asks. One per window: another Shift+click replaces it. It closes when
// the window shows another tab. The page in it has the helpers a tab's page has from its first
// load (the ad blocker, the popup blocker, passwords, ...), but it can't ask for permissions or go
// fullscreen until it is a tab.

// Hooks in Chromium's BrowserWebContentsDelegate (patches/lb/peek-hooks.patch).
//
// A link opened with OpenURLFromTab: if it opens in Peek (OpensInPeek), loads it there and
// returns the Peek's page; otherwise returns null and leaves `navigation_handle_callback` alone,
// and a Cmd+click shows the "Opened in a new tab" notice (lb::AnnouncesBackgroundTab).
content::WebContents* MaybeOpenLinkInPeek(
    BrowserWindowInterface* browser,
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>& navigation_handle_callback);
// A page the renderer created for a link (target=_blank) or a script: whether it goes into Peek.
bool ShouldShowNewContentsInPeek(BrowserWindowInterface* browser,
                                 content::WebContents* source,
                                 WindowOpenDisposition disposition,
                                 bool user_gesture);
// Shows that page in the window's Peek, replacing the Peek's page if one is open.
content::WebContents* ShowNewContentsInPeek(BrowserWindowInterface* browser,
                                            std::unique_ptr<content::WebContents> contents);

// A page the renderer created for a link: Cmd+click on a target=_blank link shows the "Opened in a
// new tab" notice (a click on it brings that tab up), as one opened with OpenURLFromTab does (MaybeOpenLinkInPeek; lb::
// AnnouncesBackgroundTab).
void NoticeNewContents(BrowserWindowInterface* browser,
                       WindowOpenDisposition disposition,
                       bool user_gesture);
// The notice's text while it shows in `browser`'s window, the page of the tab it announces, and a
// click on it, as the mouse does (false if it isn't showing). For --lb-sidebar-demo=peek.
std::optional<std::u16string> BackgroundTabNoticeForTesting(BrowserWindowInterface* browser);
content::WebContents* BackgroundTabNoticeTabForTesting(BrowserWindowInterface* browser);
bool ClickBackgroundTabNoticeForTesting(BrowserWindowInterface* browser);
// The pointer entering or leaving the notice, as the mouse does (false if it isn't showing).
bool HoverBackgroundTabNoticeForTesting(BrowserWindowInterface* browser, bool hovered);
// Whether the notice shows above the window's Peek (false if either isn't showing), and the Peek
// put above everything over the window, as activating it does.
bool IsBackgroundTabNoticeAbovePeekForTesting(BrowserWindowInterface* browser);
void RaisePeekForTesting(BrowserWindowInterface* browser);
// How many windows have a notice, shown or not yet gone.
size_t BackgroundTabNoticeCountForTesting();

// Closes the window's Peek, as Esc does, if it has one. True if one was open.
bool ClosePeek(BrowserWindowInterface* browser);

// A browser command (keyboard, menu) while the window shows a Peek: closes the Peek, runs on the
// Peek's page, or does nothing, instead of acting on the page under it (PeekCommandActionFor).
// False if the window has no Peek or the command is the window's.
bool HandlePeekCommand(BrowserWindowInterface* browser, int command_id);

// For --lb-sidebar-demo=peek.
enum class PeekControl { kClose, kOpenAsTab, kOpenInSplitView };
content::WebContents* GetPeekContentsForTesting(BrowserWindowInterface* browser);
// Clicks a button beside the card, as the mouse does. False if no Peek is open.
bool ClickPeekControlForTesting(BrowserWindowInterface* browser, PeekControl control);
// Clicks the backdrop next to the card. False if no Peek is open.
bool ClickPeekBackdropForTesting(BrowserWindowInterface* browser);
// The card, the page area under it and the window it dims, in screen coordinates; empty if no
// Peek is open.
gfx::Rect GetPeekCardBoundsForTesting(BrowserWindowInterface* browser);
gfx::Rect GetPeekPageBoundsForTesting(BrowserWindowInterface* browser);
gfx::Rect GetPeekWindowBoundsForTesting(BrowserWindowInterface* browser);
bool IsPeekAnimatingForTesting(BrowserWindowInterface* browser);
bool IsPeekControlShownForTesting(BrowserWindowInterface* browser, PeekControl control);
// Whether the Peek has the keyboard (its window is the key window).
bool IsPeekActiveForTesting(BrowserWindowInterface* browser);
// The site label shown above the card: its text, its tooltip and whether it has an icon.
struct PeekSiteShown {
  std::u16string text;
  std::u16string tooltip;
  bool icon = false;
};
PeekSiteShown GetPeekSiteForTesting(BrowserWindowInterface* browser);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_PEEK_PEEK_H_
