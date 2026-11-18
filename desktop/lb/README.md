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
lb/browser/ui/
  lb_ui_snapshot.*                  --lb-ui-snapshot=<png>: save the window's UI and quit (dev tool)
  views/sidebar/sidebar_controller.*   spaces (Local State) and favourites (profile prefs), space switching, saved tabs
  views/sidebar/sidebar_header_view.*  address pill, favourites (favicons, add/remove), space name
  views/sidebar/sidebar_footer_view.*  space switcher: switch, new; right-click rename/icon/colour/delete
  views/sidebar/new_space_dialog.*     new space: name, profile, colour, icon
  views/command_bar/command_bar.*      the command bar (Cmd+T / Cmd+L / address pill)
  views/search/search_choice_dialog.*  first-run search engine choice
  views/frame/contents_card.h       the page as an inset rounded card next to the sidebar
```

Hooked into Chromium by `patches/lb/sidebar-hooks.patch`, `sidebar-prefs.patch`,
`command-hooks.patch`, `session-spaces.patch`, `tab-metrics-detached-guard.patch` and
`contents-card.patch`; vertical tabs are the default via `patches/lb/vertical-tabs-default.patch`.

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

Sample favourites and a second space, switched to automatically: add `--lb-sidebar-demo`
(`=command-bar` also opens the command bar through the Cmd+T path, `=collapsed` collapses the
strip, `=new-space` opens the new-space dialog, `=new-profile` creates a space in a new profile,
`=search-again` opens the search choice, closes it and opens it again,
`=restore-after-crash` does what the crash bubble's Restore button does, `=tab-group` groups a
tab in Work, leaves the space and comes back, `=split` does the same with a split view,
`=reopen-window` closes a second window and reopens it as Cmd+Shift+T does; `--lb-ui-snapshot-dialog=<png>` captures a dialog; `--lb-command-bar-text=<q>`
prefills it; `--lb-ui-snapshot-command-bar=<png>` captures it; `--lb-ui-snapshot-delay=<seconds>`
sets the wait before any capture, 4 by default). Add `--use-mock-keychain` to any
manual launch, or macOS asks for keychain access after every rebuild (`run.sh` does this).

Snapshot of the UI without touching the screen:

```bash
"desktop/build/src/out/Dev/Little Browser.app/Contents/MacOS/Little Browser" --user-data-dir=/tmp/lb-snap --no-first-run --lb-ui-snapshot=/tmp/lb-ui.png https://example.com
```
