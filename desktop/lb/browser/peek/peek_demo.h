// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_PEEK_PEEK_DEMO_H_
#define LB_BROWSER_PEEK_PEEK_DEMO_H_

class BrowserWindowInterface;

namespace lb {

// --lb-sidebar-demo=peek: checks Peek and the little window in a running browser and logs
// "lb demo: peek PASS ..." or "... FAIL ..." (desktop/lb/README.md). On local pages, with real
// mouse and key events sent to the page:
//   - Shift+click on a link opens it in Peek, growing from a smaller card to one as tall as the
//     window and a little narrower than the page, with no new tab, its site shown above it and
//     the ad blocker on its page;
//   - Reload acts on the Peek's page, not the page under it; View Source does nothing;
//   - Esc, the Close button, Cmd+W pressed in the page, the Close Tab command and a click on the
//     backdrop each close it, and no tab;
//   - Shift+click inside the Peek replaces its page (and the site shown);
//   - its page can't open a window without a click; its alert() shows; its requestFullscreen()
//     is refused;
//   - Cmd+click in it opens a tab behind it, and the Peek keeps the keyboard; a tab its page
//     opens in front keeps that page as a tab, so window.opener works;
//   - "Open as tab" makes the Peek's page a tab next to the page it came from, without
//     reloading it (a value the page set when it loaded is still there), and in history;
//   - Shift+click on a target=_blank link opens Peek too; "Open in split view" pairs its page with
//     the page it came from, and beside a split view the Peek offers no second split;
//   - showing another tab closes it; a page that asks before it is left ("Leave site?") asks,
//     and "Leave" closes the Peek;
//   - in a little window, Peek offers no split view and "Open as tab" moves its page here;
//   - Cmd+Option+N opens the command bar, and an address chosen there opens in a little window
//     (a page of 960 x 680 with "Open in <space>", following the space's name, and Copy URL),
//     whose button moves its page into a tab here;
//   - a link's context menu has "Open Link in Little Window", which opens one.
// It then closes the tabs and windows it opened and deletes its pages.
// With --lb-ui-snapshot-peek=<png> it stops at the first open Peek, and with
// --lb-ui-snapshot-little-window=<png> at the first little window, which is then captured.
void RunPeekDemo(BrowserWindowInterface* browser);

}  // namespace lb

#endif  // LB_BROWSER_PEEK_PEEK_DEMO_H_
