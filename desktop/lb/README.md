# desktop/lb — Little Browser's own Chromium-side code

Our features live here, not inside Chromium files. `fetch.sh` links this folder into the Chromium
tree as `src/lb`, and thin patches in `desktop/patches/lb/` hook it into Chromium's build and UI.

Contents (the sidebar, spaces and command bar; the design is in
[docs/desktop-ui.md](../../docs/desktop-ui.md)):

```
lb/BUILD.gn                         //lb:model (view-free logic) + lb_unittests; lb_core (core/lb-core)
                                    + core_bridge, and lb_core_unittests
lb/core_bridge/lib.rs               lb-core's C++ binding (cxx, namespace lb::core)
lb/core_bridge/core_bridge_unittest.cc  the bridge carries lb-core's values across
lb/browser/importer/                import from other browsers (docs/desktop-ui.md "Import")
  import_choices.*                  what each source offers, labels, the summary line (unit tested)
  chromium_profile_reader.*         reads a Chrome-family profile (copies of its files) through
                                    lb-core's rules (unit tested)
  cookie_decryptor.*                opens Chrome-family cookie values with the keychain key (tested)
  imported_bookmarks.*              Chromium importers' bookmarks as lb-core's list (unit tested)
  chromium_import.*                 imports a Chrome-family profile into a profile and space
  browser_import.*                  imports a Chrome-family browser's profiles or Arc's spaces:
                                    profiles, spaces, tabs, then each profile's data
  imported_tabs.*                   the saved tabs an import brings: an Arc space's (its pinned
                                    folders as pinned groups) and a Chrome-family profile's open
                                    tabs, groups collapsed (unit tested)
  other_import.*                    Firefox, Safari, bookmarks files, Safari's export, through
                                    Chromium's importers with our bookmark merge
  import_sources.*                  finds the browsers on the Mac, the default one first
  extension_install.*               installs the ticked extensions from the store, no prompt
  import_app_icon.mm                a browser's macOS app icon, from its bundle id
  import_keychain.mm                reads a "<browser> Safe Storage" keychain item
  import_text.*                     makes other browsers' bytes valid UTF-8 for lb-core (unit tested)
  import_demo.*                     --lb-sidebar-demo=import-* (below)
lb/browser/prefs/pref_defaults.*    Chromium prefs whose default differs here (address autofill on)
lb/browser/privacy/tracking_params.*  a page navigation and its redirects lose their tracking
                                    parameters (lb-core's list), through a loader throttle
lb/browser/privacy/tracking_params_demo.*  --lb-sidebar-demo=tracking-params (below)
lb/test/data/import/home/           a made-up Application Support (Chrome, Arc, Edge, a bookmarks
                                    file)
lb/browser/spaces/space_model.*     Space type, saved form, new-space defaults, which space a window opens on
lb/browser/spaces/swipe_tracker.*   two-finger swipe → one space switch per gesture
lb/browser/command_bar/command_matching.*  command bar scoring and "is this an address?"
lb/browser/tabs/pinned_close.*      what closing a tab does: close, sleep or only hide (unit tested)
lb/browser/tabs/tab_to_show.*       which tab to show when the shown pinned tab goes to sleep
lb/browser/tabs/pinned_groups.*     pinned groups' rules: unbroken runs, a moved tab's group, rows,
                                    the group menu, session data (unit tested)
lb/browser/tabs/restore_order.*     the order session restore adds a window's tabs in: the selected one first
lb/browser/tabs/tab_names.*         tab names and pinned links: saved form, cleaning, when a link is
                                    taken or dropped, "left its link", the tab menu's items (unit tested)
lb/browser/password/password_manager_choice.*  first-run password managers, when to offer them, what follows an install
lb/browser/search/search_choice.*   when the first-run search choice opens, or waits for the open one
lb/browser/peek/peek_rules.*        what opens in Peek, what links in it do, which commands act on it,
                                    the card's size, growth, site label and buttons, when it offers a
                                    split view
lb/browser/peek/peek_demo.*         --lb-sidebar-demo=peek (below; also covers the little window)
lb/browser/little_window/little_window_rules.*  a new little window's bounds, "Open in <space>"
lb/browser/adblock/
  filter_lists.*                    the ad blocker's filter lists, when to update, list text → ruleset
                                    (uBlock Origin's !#if, !#include, option names and badfilter)
  site_ads.*                        per-site ads: the command bar's entry, the switch's label, what to store
  site_ads_row.*                    the Ads switch in the site-info panel's "Cookies and site data"
  ad_blocker.*                      downloads the lists and hands the rules to Chromium's filter;
                                    the per-site setting
  element_hiding_host.*             element hiding's browser side: loads the lists' hiding rules
                                    (lb-core indexes them) and answers each frame
  element_hiding_demo.*             --lb-sidebar-demo=element-hiding (below)
  ad_blocker_demo.*                 --lb-sidebar-demo=ad-blocker (below)
  ad_blocking_unittest.cc           Chromium's filter is on for every site
lb/common/element_hiding.mojom      what a frame asks the browser for element hiding
lb/common/page_names.*              the class and id names a page sends for element hiding, each
                                    once, and what the browser accepts (unit tested)
lb/renderer/element_hiding_agent.*  in each frame: inserts the hiding rules as a user stylesheet
lb/browser/demo/demo_report.*       PASS/FAIL logging, deadlines and clicks for the demo modes
lb/browser/keychain/
  keychain_key.*                    macOS: our own Safe Storage item's name, and when an existing
                                    install keeps the password from Chromium's item (unit tested)
  keychain_password_unittest.mm     Chromium's KeychainPassword as keychain-name.patch changes it
lb/browser/update/
  update_reminder.*                 when a found update is offered again after "Later"
  updater_mac.*                     macOS: loads Sparkle when the app is packaged with it, applies
                                    "Later", lets the restart into an update skip the quit question
  sparkle_api.h                     the part of Sparkle's API it calls (Sparkle isn't linked)
lb/browser/extensions/
  web_store_unittest.cc             store addresses, update server, install API not substituted
  update_check_unittest.cc          update checks: extensions' go out bare, others stay blocked
  extensions_demo.*                 --lb-sidebar-demo=<extension modes> (below)
lb/browser/ui/
  lb_ui_snapshot.*                  --lb-ui-snapshot=<png>: save the window's UI and quit (dev tool)
  tabs/pinned_tab_sleep.*           closing a pinned tab puts it to sleep instead
  tabs/pinned_group_tracker.*       a window's pinned groups: members, commands, session data
  tabs/tab_custom_data.*            a tab's name and pinned link, kept by the tab; the tab menu's
                                    items, the row's title, session data
  views/sidebar/sidebar_controller.*   spaces (Local State), space switching, saved tabs
  views/sidebar/sidebar_header_view.*  the space name; --lb-sidebar-demo=sidebar-header (below)
  views/sidebar/sidebar_footer_view.*  space switcher (switch; right-click rename/icon/colour/delete) and the "+" menu
  views/sidebar/new_tab_row_view.*     "+ New Tab" between the pinned and today's tabs
  views/sidebar/pinned_group_layout.*  pinned groups' header rows, indents and colour lines
  views/sidebar/pinned_groups_demo.*   --lb-sidebar-demo=pinned-groups(-restart, -drops) (below)
  views/sidebar/tab_search_demo.*      --lb-sidebar-demo=tab-search (below)
  views/sidebar/sidebar_edits_demo.*   --lb-sidebar-demo=tab-names (below)
  views/sidebar/new_space_dialog.*     new space: name, profile, colour, icon
  views/command_bar/command_bar.*      the command bar (Cmd+T / Cmd+L / Cmd+Option+N)
  views/importer/import_dialog.*       the import dialog, also opened by itself on first run
  views/peek/peek.*                    Peek: the card over the dimmed window, its page, the buttons beside it
  views/little_window/little_window.*  the little window: opening one, its toolbar buttons, moving
                                       its page into a tab
  views/little_window/little_window_menu.*  the File menu's New little window;
                                       --lb-sidebar-demo=little-window-menu (below)
  lb_command_ids.h                     our command ids (Cmd+Option+N, the link menu's little window)
  views/search/search_choice_dialog.*  first-run search engine choice
  views/password/password_manager_choice_dialog.*  first-run password manager choice
  views/profiles/space_avatar.*        the profile button's and profile menu's avatar: the space's emoji
  views/profiles/space_avatar_demo.*   --lb-sidebar-demo=space-avatar (below)
  views/browser_modal_dialog.*      base for our browser-modal dialogs (deletes itself when closed)
  views/frame/contents_card.h       the page as an inset rounded card next to the sidebar
```

Hooked into Chromium by `patches/lb/sidebar-hooks.patch`, `new-tab-row.patch`,
`pinned-list.patch`, `pinned-tab-sleep.patch`, `tab-click-hook.patch`, `sidebar-prefs.patch`,
`command-hooks.patch`, `session-spaces.patch`, `restore-selected-first.patch`, `tab-names.patch`,
`tab-names-ui.patch`,
`tab-metrics-detached-guard.patch`, `contents-card.patch`, `ad-blocking.patch`,
`ad-blocking-settings.patch` and `ad-blocking-page-info.patch` (the ad blocker),
`pinned-groups.patch` (pinned groups), `peek-hooks.patch`
(Peek), `little-window-toolbar.patch`, `little-window-shortcut.patch`,
`little-window-link-menu.patch` and `little-window-menu.patch` (the little window),
`space-avatar.patch` (the space's emoji as the avatar), `tracking-params.patch` (tracking
parameters), `element-hiding.patch` (element hiding's agent in each frame and its browser side);
vertical tabs are the default via `patches/lb/vertical-tabs-default.patch`.

Not built yet: the Widevine consent prompt, and the link to `lb-core` (spaces are
stored in C++ prefs).

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
`=inactive-space-load` opens a page, switches spaces so its tab is parked, and loads another page
in it (Chromium's tab helpers must cope with a tab outside every window),
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
restart wakes none of the sleeping pinned tabs, `=pinned-groups`, on a fresh profile, makes a
pinned group with the tab menu, renames, collapses, moves tabs in and out of it, closes and removes
it, unpins it and pins it back with the group menu's Pin Group, checks it is saved and survives a
space switch, then drags with the mouse (a tab onto its header and out again, once cancelled with
Esc, its header below "+ New Tab" with an image of the group following the pointer and a line
where it lands, the today group's header back above with a line where it would be pinned), checks
a collapsed pinned group's tabs count as collapsed, and quits (`--lb-ui-snapshot-pinned-drag=<png>`
stops it mid-drag and captures that image); `=pinned-groups-restart`, run again on the profile
`=pinned-groups` left, checks the restored window has it back, its header filled with the group's
colour; `=pinned-groups-drops` drags a pinned group holding a split view between today tabs below
a collapsed today group and back, and closes it (the split must stay whole, no group may be split,
every tab must sleep); the three log `lb demo: <mode> PASS` or `FAIL`; `--lb-demo-url=<url>` makes the slept tab a new one showing <url>, such as a page playing
audio (add `--autoplay-policy=no-user-gesture-required`); all logged with
`--vmodule=sidebar_controller=1,pinned_tab_sleep=1`, with each pinned row's button;
`=tab-search` saves Tab Search as unpinned, opens a second window (it must clear that and show the
button), runs the pin toggle (it must stay pinned), checks the window menu has no toggle and
right-clicks the button (no menu may open);
`=tab-names` renames a tab (its row must show the name), checks the tab menu's items, renames it
again through the command bar, pins it (its link must be its page), loads another page (Use
Current Page as Link and Back to Pinned Page must be enabled), goes back, makes another page the
link, drags the tab below "+ New Tab" and back (its link must stay), puts it to sleep (its name
and link must stay with the new page it gets) and checks the space's saved tabs; with
`--lb-ui-snapshot-sidebar=<png>` it captures the named rows at the end;
`=sidebar-header` checks the sidebar header holds the space name alone (no address field) and that
⌘L opens the command bar with the page's full address;
`=tracking-params` (needs the network) loads a page whose address has tracking parameters (it
must commit without them, other parameters and the fragment kept), a redirect to a tracked
address (it must land on the clean one) and an iframe with one (it must keep it);
`=space-avatar` checks the toolbar's profile button shows the first space's emoji, the second's
after switching to it and the new one after that space's icon changes, then opens the profile menu
(`--lb-ui-snapshot-dialog=<png>` captures it, the emoji in its header);
`=little-window-menu` checks the File menu has "New little window" after "New Incognito window"
with ⌥⌘N, that the window enables it, and that choosing it opens the command bar for a little
window, where an address opens one;
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
`=import-extensions`, on a profile without the extension, installs it as an import does, with any install prompt cancelled,
after one with no profile and one the store doesn't have (it must install from the store all the
same, as the import shows no prompt),
`=element-hiding` (needs the network) puts its own hiding rules in place of the downloaded ones,
which it sets aside before example.com loads so the page waits for its rules, loads example.com
and adds elements as a script would after the load: the site's rule and generic
rules (with no class or id, by class, by id) must hide theirs, an exception must keep one, the
page's paragraph and an element no rule names must show, and an example.org iframe must get
example.org's rule, not example.com's; after allowing ads on the site and adding them again,
nothing may be hidden;
`=ad-blocker`, on a fresh profile (it counts the first-run questions as answered), waits for the
filter lists, then on example.com an ad script must be blocked and a CDN script load, no per-site
record may be kept and the console must show none of Chrome's abusive-ads messages; with a
third-party frame trying to set a cookie the address bar must show no eye icon; in the site-info
panel's "Cookies and site data", clicking the cookie switch and then the Ads switch must allow ads,
let the ad script load and leave no "reload this page" prompt; the command bar's "Block ads on
this site" must block it again, also with Settings set to "Sites can show ads"; and the site's
Settings page and the Ads page must use our wording (with
`--lb-ui-snapshot-dialog=<png> --lb-ui-snapshot-delay=0` it captures the site-info panel),
`=peek`, on a fresh profile, writes four local pages and sends real mouse and key events to them:
Shift+click must open a link in Peek with no new tab, growing to a card as tall as the window and a
little narrower than the page, with its site shown above it and the ad blocker on its page; Reload
must act on the Peek's page and View Source do nothing; Esc, the Close button, Cmd+W pressed in
the page, the Close Tab command and a click on the backdrop must each close it and no tab;
Shift+click inside the Peek must replace its page; its page must not open a window without a
click, its alert() must show and its requestFullscreen() be refused; Cmd+click in it must open a
tab behind it, the Peek keeping the keyboard, and a tab its page opens in front must keep that
page as a tab (window.opener alive); "Open as tab" must make its page a tab next to the first one
without reloading it, and record it in history; a Shift+clicked `target=_blank` link must open in
Peek too and "Open in split view" must pair its page with the first one, after which no second
split is offered; showing another tab must close the Peek; a page asking "Leave site?" must ask
and close on "Leave"; a Peek in a little window must offer no split view and "Open as tab" must
move its page into the window with tabs; Cmd+Option+N's command bar must open a little window (a
960 x 680 page, "Open in <space>" following the space's name, Copy URL) whose button moves it into
a tab, and a link's "Open Link in Little Window" must open one too; it then closes what it opened
and deletes its pages (with `--lb-ui-snapshot-peek=<png>` or
`--lb-ui-snapshot-little-window=<png>` it stops at the first Peek or little window and captures
it); the
extension is uBlock Origin Lite unless
`--lb-demo-extension=<id>`, and each logs
`lb demo: <mode> PASS` or `FAIL` once, without `--vmodule`;
`--lb-ui-snapshot-dialog=<png>` captures a dialog; `--lb-command-bar-text=<q>`
prefills it; `--lb-ui-snapshot-command-bar=<png>` captures it; `--lb-ui-snapshot-delay=<seconds>`
sets the wait before any capture, 4 by default). Add `--use-mock-keychain` to any
manual launch, or macOS asks for keychain access after every rebuild (`run.sh` does this).

Import (a fresh `--user-data-dir`, `--use-mock-keychain`, and
`--lb-import-home=desktop/lb/test/data/import/home`, a made-up Application Support, whose cookies
are read with a made-up keychain's password; without that flag the real keychain is asked):
`--lb-sidebar-demo=import-fixture` imports its Chrome profile and checks bookmarks, history, the
search shortcut, the address, open tabs (pinned, grouped), the cookie and the offered extension,
then that a second import adds nothing; `import-arc` checks Arc's spaces, tabs, profiles and
bookmarks; `import-edge` that one picked profile of a two-profile Edge whose bookmarks and session
files are saved with encrypted copies becomes a space and a profile with both, its tabs pinned and
grouped as in Edge, the group collapsed;
`import-files` a bookmarks file, a Safari export, then two bookmarks files in one import; `import-dialog` steps through the dialog: the
browsers, Back, and each browser's profiles or spaces, then imports Edge's two profiles and checks
the extensions list is grouped by profile, with one turned off in Edge unticked, and, at fifteen rows, scrolls in a dialog of bounded height (`--lb-ui-snapshot-dialog=<png>` captures that list). `import-choices` opens the choices step on Chrome and holds it (`--lb-ui-snapshot-dialog=<png>` captures the item rows, each with its icon). `import-progress` imports Edge through the dialog and checks the progress step reports each item (its spinner becoming a check). `import-first-run`, run without `--no-first-run`, checks that first run opens the dialog itself on
its first step, with the introduction and "Not now", that the next question and a second Import
wait for it, and go on once it closes (`--lb-ui-snapshot-dialog=<png>` captures it).
`import-first-run-close`, also without `--no-first-run`, closes the window under the first-run
dialog while a second window waits for it: nothing more may open over the closing window, and the
second window asks the next question; it then quits with that question open, and passes only if
the process exits with 0.
`import-arc-unreadable` breaks a copy's Arc sidebar file: Arc must be listed with its profiles
instead, with the problem named. `--lb-sidebar-demo=import --lb-import-source=<profile folder>` imports a
real Chrome-family profile and logs only how many of each item came along. Each logs
`lb demo: <mode> PASS` or `FAIL`.

Snapshot of the UI without touching the screen:

```bash
"desktop/build/src/out/Dev/Little Browser.app/Contents/MacOS/Little Browser" --user-data-dir=/tmp/lb-snap --no-first-run --lb-ui-snapshot=/tmp/lb-ui.png https://example.com
```
