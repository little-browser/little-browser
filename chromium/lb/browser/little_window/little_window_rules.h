// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_RULES_H_
#define LB_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_RULES_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

class GURL;

namespace lb {

// The little window (docs/ux.md section 8): a small window with no tabs, for one page.

// How much of its parent window (the window with tabs it opens over) a little window takes, each
// way, as in Arc.
inline constexpr float kLittleWindowShare = 0.9f;

// Where a new little window goes on a screen whose usable area is `work_area`: the window's
// origin and its page's size (as for Chromium's popup windows, the window is its page plus
// `chrome_height`, its one row above the page). The window is kLittleWindowShare of
// `parent`'s size (the window with tabs it opens over; without one, the screen's), centred on it,
// or, if another little window opened before it (`newest_little`, that window's bounds), a step
// down and to the right of that one; always within the screen, with a margin.
gfx::Rect LittleWindowBounds(const gfx::Rect& work_area,
                             const std::optional<gfx::Rect>& parent,
                             const std::optional<gfx::Rect>& newest_little,
                             int chrome_height);

// A little window has one row above its page, as in Arc: the window buttons, then the toolbar
// (the address, "Copy URL", "Open in <space>"), with no title bar above them. In the top of
// `area` (the window, in its own coordinates), with the window buttons taking `leading` at its
// leading edge and `trailing` at the other (each with its padding; 0 for none, as in fullscreen)
// and `buttons_height` high: the toolbar, between them, as high as the row. The row is as high as
// the taller of the window buttons and the toolbar (`toolbar_height`, what it asks for), so the
// buttons sit centred in it.
int LittleWindowRowHeight(int buttons_height, int toolbar_height);
gfx::Rect LittleWindowToolbarBounds(const gfx::Rect& area,
                                    int leading,
                                    int trailing,
                                    int buttons_height,
                                    int toolbar_height);

// The button that moves the page into a window with tabs: "Open in Work", after the space that
// window shows. Without a space name (no window with tabs yet): "Open in a tab".
std::u16string OpenInSpaceLabel(std::string_view space_name);

// Whether a link another app hands the browser (a mail link, `open <url>`, a second launch with a
// URL on its command line) opens in a little window of its own instead of a tab: web pages and
// local files. Browser pages (chrome://, extensions) and links Chromium marks as untrusted (from
// a stripped custom scheme) open as Chromium opens them, in a tab.
bool OpensFromOtherAppInLittleWindow(const GURL& url, bool untrusted_launch);

// The links on a second launch's command line (another app, or a script running the browser while
// it runs), as Chromium reads them, in the profile Chromium would open them in (guest or private
// when asked for or required by policy).
struct CommandLineLinks {
  size_t links = 0;                // links on it
  size_t little_window_links = 0;  // of those, the ones OpensFromOtherAppInLittleWindow takes
  bool app_launch = false;         // --app or --app-id: Chromium opens an app window
  bool can_open_window = false;    // that profile can have a window now
};
// Whether they open in little windows, each in its own (true), or the command line goes to
// Chromium (false): only when there are links, every one opens in a little window (a command line
// isn't split between the two), it isn't an app launch, and a window can open.
bool OpensCommandLineInLittleWindows(const CommandLineLinks& links);

}  // namespace lb

#endif  // LB_BROWSER_LITTLE_WINDOW_LITTLE_WINDOW_RULES_H_
