// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_PEEK_PEEK_RULES_H_
#define LB_BROWSER_PEEK_PEEK_RULES_H_

#include <string>

#include "base/time/time.h"
#include "components/security_state/core/security_state.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

class GURL;

namespace lb {

// Peek (docs/ux.md section 7): Shift+click a link to open it in a card over the page.

// The kind of browser window a link is clicked in.
enum class WindowKind {
  kNormal,  // a window with tabs (and our sidebar)
  kLittle,  // a little window (docs/ux.md section 8): Chromium's popup window
  kOther,   // apps, DevTools, Picture-in-Picture
};

// A request to open a link somewhere other than the page it was clicked on.
struct LinkOpen {
  WindowOpenDisposition disposition = WindowOpenDisposition::CURRENT_TAB;
  // The page asked for it (a click or a script), not the browser (a menu, the command bar).
  bool renderer_initiated = false;
  bool user_gesture = false;
  WindowKind window = WindowKind::kNormal;
  // The click was dispatched by a script (`a.dispatchEvent(new MouseEvent(..., {shiftKey}))`),
  // not made by the user.
  bool untrusted_event = false;
};

// Whether the link opens in Peek instead: the user's Shift+click (which Chromium opens in a new
// window) on a page in a normal or little window. Chromium's "Open Link in New Window" menu item
// still opens a window, and a script can't open a Peek, with or without a click.
bool OpensInPeek(const LinkOpen& open);

// What a link opened from the page in the Peek does.
enum class PeekLinkAction {
  kLoadInPeek,    // the same page: the Peek navigates
  kReplacePeek,   // the user's Shift+click: the Peek shows the new page instead
  kOpenInWindow,  // a new tab or window, a download, the link menu: the window handles it
};
PeekLinkAction ActionForLinkInPeek(const LinkOpen& open);

// Whether a link from the Peek's page that the window handles first turns the Peek's page into a
// tab: when it opens a tab in front in this window, which would otherwise close the Peek and
// destroy the page that opened it (`window.opener`). The page then stays, as a tab next to it.
bool PromotesPeekBeforeOpening(WindowOpenDisposition disposition, WindowKind window);

// Where "Open as tab" puts the page: the window under the Peek if it has tabs, otherwise the
// profile's window with tabs (as a little window's "Open in <space>" does).
enum class OpenAsTabTarget { kThisWindow, kSpaceWindow };
OpenAsTabTarget TargetForOpenAsTab(WindowKind window);

// Whether the Peek offers "Open in split view" (its page as a tab beside the one under it): only
// in a window with tabs, and only if the page under it isn't in a split view already.
bool CanOpenPeekInSplitView(WindowKind window, bool page_in_split_view);

// What a browser command (chrome/app/chrome_command_ids.h, our lb_command_ids.h) does while the
// window shows a Peek: the window's commands still act on the window, but those for "the page"
// must not reach the dimmed page under the Peek.
enum class PeekCommandAction {
  kPass,         // not about the page (new tab, switch tab, settings, ...): as usual
  kClosePeek,    // Cmd+W, Esc: the Peek goes, the tab stays
  kForPeekPage,  // reload, stop, back, forward, zoom, copy URL: on the Peek's page
  kIgnore,       // find, print, view source, DevTools, the address, ...: nothing, until it is a tab
};
PeekCommandAction PeekCommandActionFor(int command_id);

// A key pressed while the Peek's page has the keyboard, as the window sees it before the page
// does. Keys an input method takes (Esc that cancels a composition) never get here.
struct PeekKeyPress {
  bool escape = false;    // the Esc key
  bool modified = false;  // with Shift, Control, Option or Command
  bool key_down = false;  // going down, not coming up
};
// Whether the key closes the Peek before its page sees it: Esc alone, as it goes down. The page
// doesn't get it, so a page that handles Esc itself (to close its own menu or dialog) can't keep
// the Peek open.
bool ClosesPeekBeforePage(const PeekKeyPress& key);

// How long the card takes to grow open, and to expand into a tab (docs/ux.md section 11).
inline constexpr base::TimeDelta kPeekOpenDuration = base::Milliseconds(200);
inline constexpr base::TimeDelta kPeekExpandDuration = base::Milliseconds(240);

// The card's buttons: round, in a column to the right of the card, from its top.
inline constexpr int kPeekControlSize = 32;
inline constexpr int kPeekControlSpacing = 8;

// The site label above the card: the page's host, so a page can't pass for the one under it.
inline constexpr int kPeekLabelHeight = 20;

// The card, in the same coordinates as `page` (the area the page is shown in) and `window` (the
// browser window): from a margin at the top, room for the site label, down to the window's bottom
// edge, and a little narrower than the page, centred on it, leaving room on the right for the
// buttons.
gfx::Rect PeekCardBounds(const gfx::Rect& page, const gfx::Rect& window);

// The site label, in the margin above `card`, as wide as the card.
gfx::Rect PeekLabelBounds(const gfx::Rect& card);

// The `index`th button (from the top) beside `card`, centred in the gap between the card and the
// page's right edge.
gfx::Rect PeekControlBounds(const gfx::Rect& page, const gfx::Rect& card, int index);

// Where "Open in split view" puts the page while the card moves there: the right half of the page.
gfx::Rect PeekSplitBounds(const gfx::Rect& page);

// Where the card grows from: a small rectangle with the same proportions as `card`, centred on
// `pointer` (the mouse pointer, where the link was clicked) when it is over `page`, otherwise on
// the page's centre, and kept inside `page`.
gfx::Rect PeekStartBounds(const gfx::Rect& page, const gfx::Rect& card, const gfx::Point& pointer);

// What the site label above the card warns about. A secure page gets no icon: the label can't be
// clicked, and a page that is secure needs nothing pointed out.
enum class PeekSiteWarning { kNone, kNotSecure, kDangerous };

struct PeekSiteLabel {
  std::u16string text;     // "kp.ru", or "Not secure · example.com"
  std::u16string tooltip;  // the full URL
  PeekSiteWarning warning = PeekSiteWarning::kNone;
};

// The label for a page at `url` whose address bar would show `level`: the site without its
// scheme, path or "www." (as Chromium shows a site for a permission prompt, with a safe IDN), led
// by "Not secure" or "Dangerous" when the address bar would warn.
PeekSiteLabel PeekSiteLabelFor(const GURL& url, security_state::SecurityLevel level);

}  // namespace lb

#endif  // LB_BROWSER_PEEK_PEEK_RULES_H_
