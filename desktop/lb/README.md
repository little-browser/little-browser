# desktop/lb — Little Browser's own Chromium-side code

Our features live here, not inside Chromium files. `fetch.sh` links this folder into the Chromium
tree as `src/lb`, and thin patches in `desktop/patches/lb/` hook it into Chromium's build and UI.

Contents (the sidebar, spaces and command bar; the design is in
[docs/desktop-ui.md](../../docs/desktop-ui.md)):

```
lb/BUILD.gn                         //lb:model (view-free logic) + lb_unittests
lb/browser/spaces/space_model.*     Space type, saved form, new-space defaults, which space a window opens on
lb/browser/spaces/swipe_tracker.*   two-finger swipe → one space switch per gesture
lb/browser/command_bar/command_matching.*  command bar scoring and "is this an address?"
lb/browser/tabs/pinned_close.*      what closing a tab does: close, sleep or only hide (unit tested)
lb/browser/tabs/tab_to_show.*       which tab to show when the shown pinned tab goes to sleep
lb/browser/tabs/restore_order.*     the order session restore adds a window's tabs in: the selected one first
lb/browser/password/password_manager_choice.*  first-run password managers, when to offer them, what follows an install
lb/browser/search/search_choice.*   when the first-run search choice opens, or waits for the open one
lb/browser/adblock/
  filter_lists.*                    the ad blocker's filter lists, when to update, list text → ruleset
                                    (uBlock Origin's !#if, !#include, option names and badfilter)
  site_ads.*                        per-site ads: the command bar's entry, the switch's label, what to store
  site_ads_row.*                    the Ads switch in the site-info panel's "Cookies and site data"
  ad_blocker.*                      downloads the lists and hands the rules to Chromium's filter;
                                    the per-site setting
  ad_blocker_demo.*                 --lb-sidebar-demo=ad-blocker (below)
  ad_blocking_unittest.cc           Chromium's filter is on for every site
lb/browser/demo/demo_report.*       PASS/FAIL logging and deadlines for the demo modes
lb/browser/extensions/
  web_store_unittest.cc             store addresses, update server, install API not substituted
  update_check_unittest.cc          update checks: extensions' go out bare, others stay blocked
  extensions_demo.*                 --lb-sidebar-demo=<extension modes> (below)
lb/browser/ui/
  lb_ui_snapshot.*                  --lb-ui-snapshot=<png>: save the window's UI and quit (dev tool)
  tabs/pinned_tab_sleep.*           closing a pinned tab puts it to sleep instead
  views/sidebar/sidebar_controller.*   spaces (Local State), space switching, saved tabs
  views/sidebar/sidebar_header_view.*  address pill, space name
  views/sidebar/sidebar_footer_view.*  space switcher (switch; right-click rename/icon/colour/delete) and the "+" menu
  views/sidebar/new_tab_row_view.*     "+ New Tab" between the pinned and today's tabs
  views/sidebar/new_space_dialog.*     new space: name, profile, colour, icon
  views/command_bar/command_bar.*      the command bar (Cmd+T / Cmd+L / address pill)
  views/search/search_choice_dialog.*  first-run search engine choice
  views/password/password_manager_choice_dialog.*  first-run password manager choice
  views/browser_modal_dialog.*      base for our browser-modal dialogs (deletes itself when closed)
  views/frame/contents_card.h       the page as an inset rounded card next to the sidebar
```

Hooked into Chromium by `patches/lb/sidebar-hooks.patch`, `new-tab-row.patch`,
`pinned-list.patch`, `pinned-tab-sleep.patch`, `tab-click-hook.patch`, `sidebar-prefs.patch`,
`command-hooks.patch`, `session-spaces.patch`, `restore-selected-first.patch`,
`tab-metrics-detached-guard.patch`, `contents-card.patch`, `ad-blocking.patch`,
`ad-blocking-settings.patch` and `ad-blocking-page-info.patch` (the ad blocker);
vertical tabs are the default via `patches/lb/vertical-tabs-default.patch`.

Not built yet: peek, the little window, element hiding, tracking-parameter stripping, the
Widevine consent prompt, and the link to `lb-core` (spaces are stored in C++ prefs).

Unit tests (`*_unittest.cc` next to the code):

```bash
cd desktop/build/src && ninja -C out/Dev lb_unittests && out/Dev/lb_unittests
```

Try it:

```bash
desktop/scripts/build.sh --dev && desktop/scripts/run.sh --dev --fresh
```

A second space, switched to automatically: add `--lb-sidebar-demo`
(`=command-bar` also opens the command bar through the Cmd+T path, `=collapsed` collapses the
strip, `=new-space` opens the new-space dialog, `=new-profile` creates a space in a new profile,
`=search-again` opens the search choice, closes it and opens it again,
`=restore-after-crash` does what the crash bubble's Restore button does, `=tab-group` groups a
tab in Work, leaves the space and comes back, `=group-colour` groups a tab without choosing a
colour (it takes the space's), `=group-close` then closes that group and logs the saved groups
before and after (none may be left), `=split` does the same as `=tab-group` with a split view,
`=reopen-window` closes a second window and reopens it through tab restore, as an extension can, `=pinned-sleep`
pins a tab, shows it and presses Cmd+W (it must stay, asleep and faded), `=pinned-remove` then
closes the sleeping tab (it must go), `=pinned-wake` then
shows it again (it must load), `=pinned-close-all` pins every tab, closes them all at once and then closes one twice in a row
(all must sleep, with a new tab shown), `=pinned-unloaded` pins a tab that hasn't loaded yet (it must look
asleep), `=pinned-restart`, run again on the profile `=pinned-close-all` left, checks that a
restart wakes none of the sleeping pinned tabs; `--lb-demo-url=<url>` makes the slept tab a new one showing <url>, such as a page playing
audio (add `--autoplay-policy=no-user-gesture-required`); all logged with
`--vmodule=sidebar_controller=1,pinned_tab_sleep=1`, with each pinned row's button;
`=drag-pins` drags tabs with synthetic mouse events across "+ New Tab" both ways, into and
out of a tab group, as single jumps and with Esc, logging the tabs after each drag;
`=web-store` opens an extension's Chrome Web Store page, presses "Add to …" and accepts the
prompt (it must install from the store, and the page must get an empty referrer chain),
`=extension-update`, run again on that profile, checks the store for an update to it as the
extension updater does and records the request (it must reach `update.googleapis.com`, be
offered the update and carry no pings and no hardware fields), `=update-manifest` fetches the
store's update manifest for it and parses it as `ExtensionDownloader` does, `=mv2-off` loads an
unpacked MV2 and MV3 extension (only MV3 may run), `=store-links`, on a profile with no
extensions, checks the app menu's "Visit Chrome Web Store" and follows it and the store links
on `chrome://extensions` (each must load a store page), `=password-manager`, on a fresh profile,
opens a second window while the search choice is up, answers the search choice with "Not now",
then picks Proton Pass in the password manager choice and accepts the install prompt (it must
come after the search choice, never open at the same time, and install from the store),
`=ad-blocker`, on a fresh profile (it counts the first-run questions as answered), waits for the
filter lists, then on example.com an ad script must be blocked and a CDN script load, no per-site
record may be kept and the console must show none of Chrome's abusive-ads messages; with a
third-party frame trying to set a cookie the address bar must show no eye icon; in the site-info
panel's "Cookies and site data", clicking the cookie switch and then the Ads switch must allow ads,
let the ad script load and leave no "reload this page" prompt; the command bar's "Block ads on
this site" must block it again, also with Settings set to "Sites can show ads"; and the site's
Settings page and the Ads page must use our wording (with
`--lb-ui-snapshot-dialog=<png> --lb-ui-snapshot-delay=0` it captures the site-info panel); the
extension is uBlock Origin Lite unless
`--lb-demo-extension=<id>`, and each logs
`lb demo: <mode> PASS` or `FAIL` once, without `--vmodule`;
`--lb-ui-snapshot-dialog=<png>` captures a dialog; `--lb-command-bar-text=<q>`
prefills it; `--lb-ui-snapshot-command-bar=<png>` captures it; `--lb-ui-snapshot-delay=<seconds>`
sets the wait before any capture, 4 by default). Add `--use-mock-keychain` to any
manual launch, or macOS asks for keychain access after every rebuild (`run.sh` does this).

Snapshot of the UI without touching the screen:

```bash
"desktop/build/src/out/Dev/Little Browser.app/Contents/MacOS/Little Browser" --user-data-dir=/tmp/lb-snap --no-first-run --lb-ui-snapshot=/tmp/lb-ui.png https://example.com
```
