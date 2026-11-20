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
  views/frame/contents_card.h       the page as an inset rounded card next to the sidebar
```

Hooked into Chromium by `patches/lb/sidebar-hooks.patch`, `new-tab-row.patch`,
`pinned-list.patch`, `pinned-tab-sleep.patch`, `tab-click-hook.patch`, `sidebar-prefs.patch`,
`command-hooks.patch`, `session-spaces.patch`, `restore-selected-first.patch`,
`tab-metrics-detached-guard.patch` and `contents-card.patch`; vertical tabs are the default via `patches/lb/vertical-tabs-default.patch`.

Not built yet: peek, the little window, the content blocker, tracking-parameter stripping, the
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
`--lb-ui-snapshot-dialog=<png>` captures a dialog; `--lb-command-bar-text=<q>`
prefills it; `--lb-ui-snapshot-command-bar=<png>` captures it; `--lb-ui-snapshot-delay=<seconds>`
sets the wait before any capture, 4 by default). Add `--use-mock-keychain` to any
manual launch, or macOS asks for keychain access after every rebuild (`run.sh` does this).

Snapshot of the UI without touching the screen:

```bash
"desktop/build/src/out/Dev/Little Browser.app/Contents/MacOS/Little Browser" --user-data-dir=/tmp/lb-snap --no-first-run --lb-ui-snapshot=/tmp/lb-ui.png https://example.com
```
