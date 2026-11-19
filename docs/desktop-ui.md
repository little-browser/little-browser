# Desktop UI — the sidebar on Chromium's Views

How the desktop browser's sidebar is built. The behaviour it implements is in [ux.md](ux.md) §2–§6;
the file map and developer switches are in [desktop/lb/README.md](../desktop/lb/README.md).

## Approach

The sidebar is **native Views code on top of Chromium's own vertical tabs**. Our code lives in
`desktop/lb/` and thin patches in `desktop/patches/lb/` hook it in. There is no WebUI sidebar and no
custom window frame, so the 4-weekly Chromium rebase touches as little as possible.

| Piece | Where |
|---|---|
| Vertical tabs on by default | `patches/lb/vertical-tabs-default.patch` |
| Sidebar header (address pill, space name) in the vertical tab strip | `lb/browser/ui/views/sidebar/sidebar_header_view.*`, hooked by `patches/lb/sidebar-hooks.patch` |
| Footer: space switcher and the "+" menu (new space; new tab, split view, tab group), below the strip | `lb/browser/ui/views/sidebar/sidebar_footer_view.*` |
| "+ New Tab" between pinned and today's tabs, replacing Chromium's new-tab button | `lb/browser/ui/views/sidebar/new_tab_row_view.*`, in a slot added by `patches/lb/new-tab-row.patch` |
| Pinned tabs as list rows | `patches/lb/pinned-list.patch` (`tab_view.cc`, `tab_view_vertical_layout.cc`, `pinned_tab_container_view.cc`) |
| The page as an inset card with rounded corners | `lb/browser/ui/views/frame/contents_card.h`, `patches/lb/contents-card.patch` |
| Command bar | `lb/browser/ui/views/command_bar/`, `patches/lb/command-hooks.patch` |
| Spaces, saved tabs | `lb/browser/ui/views/sidebar/sidebar_controller.*`, `lb/browser/spaces/` |

Upstream's vertical tab strip already provides collapse and expand, expand on hover, resizing, tab
groups, split views, drag and drop, keyboard focus and a two-finger swipe controller. We add views
around it instead of forking it: the header goes in at index 0 of the strip's flex layout, the
footer at the end, and the "+ New Tab" row in a slot of the tab list's vertical layout. Chromium's
new-tab button is hidden; its bottom container stays, because it is what fills the space between
the tabs and the footer.

**New Tab Group** runs `IDC_CREATE_NEW_TAB_GROUP` through `SidebarController::NewTabGroup()`, which
remembers the blank tab the group starts with. When another tab joins that group the controller
closes the placeholder; moving onto it opens the command bar in `kOpenHere` mode, so the chosen page
opens in the placeholder, inside its group. Placeholders work in private windows too.

The footer's "+" menu runs Chromium's own commands: `IDC_NEW_TAB` (the command bar),
`chrome::NewSplitTab` and `IDC_CREATE_NEW_TAB_GROUP`. New split view is offered only while the
active tab shows a page and isn't split already.

**Pinned tabs as a list.** Upstream draws pinned tabs as outlined icon tiles in a flow layout. With
`patches/lb/pinned-list.patch` an expanded strip draws them as rows, one per line with icon and
title, like the other tabs, and a close button; only the collapsed strip keeps icon tiles.

**Sleeping pinned tabs.** Every tab close passes through `lb::SleepInsteadOfClose`
(`lb/browser/ui/tabs/pinned_tab_sleep.*`, hooked into `TabStripModel::CloseTabs` by
`patches/lb/pinned-tab-sleep.patch`). A pinned tab the user closes is kept, and right after the
close returns another tab is shown (the one focused last, preferring awake tabs and leaving out
tabs closed together with it, or a new tab) and the pinned tab's page is unloaded with Chromium's
own discard (`TabLifecycleUnitExternal::DiscardTab`, the call `chrome.tabs.discard` makes, which
has no eligibility policy), marked `NeedsReload()` so it loads again whenever it is shown. A tab
muted on its own stays muted. A page with a beforeunload handler (it may hold unsaved input) is
only hidden, not unloaded. The patch also makes a discard clear the old page's media, capture and
device alerts, which Chromium otherwise keeps, since its own policy never discards such tabs; a
tab replaced in a split view closes with `CLOSE_NONE`, so it doesn't sleep.

Closing a pinned tab that is already asleep removes it, as do closes without a user gesture,
closing the window, and closing a pinned tab in a split view (Chromium takes it out of the split). A pinned tab counts as asleep when it is discarded or not loaded yet (as after a
restart or a space switch). The same patch draws a sleeping pinned tab faded (`TabIcon` and the
title) with "−" instead of "✕" on its close button. Chromium's "press ⌘W again to close a pinned
tab" toast is skipped: `lb::HandleCommand` closes the selected pinned tabs itself.

**Dragging** is Chromium's own, with two changes. `patches/lb/drag-pins.patch` lets a tab cross
between the pinned and the today area the way it crosses into a group. The pointer picks the area
(above the middle of the "+ New Tab" row is pinned), and entering the other area pins or unpins the
dragged tabs, split views whole; the drag then places them as within any area. A drag that includes
a tab group stays among the today tabs, since Chromium's model has no pinned groups. Upstream
rebuilds a tab's view when its pin state changes, which would free the views the drag holds, so a
dragged tab keeps its view; Esc restores the original pin state. `patches/lb/no-tab-detach.patch`
makes a dragged tab never detach into a new window.

The toolbar stays Chromium's, as a **slim toolbar** above the page card: back, forward, reload, the
address bar, extensions, downloads, profile and menu. Moving them into the sidebar would be a large
change to `BrowserView` and `ToolbarView`, rebased every release.

Rebase risk is concentrated in `browser_view_tabbed_layout_impl.cc`, which changes often because
vertical tabs are new upstream. The page-card patch there stays a few lines; all logic is in `lb/`.

## Command bar

Cmd+T, the new-tab buttons, Cmd+L and the address pill open the command bar. It lists open tabs
across spaces, history, commands and the address or search entry, and has a
rename-space mode. Two hooks in `patches/lb/command-hooks.patch` route into it: the command
controller's entry point (keyboard and menus) and the `kActionNewTab` action in
`browser_actions.cc`, because Chromium runs some commands through its actions framework, which
bypasses `HandleCommandWithDisposition`. Both work with tabs on top too.

Shortcuts: Ctrl+1…9 and Ctrl+Option+←/→ switch spaces
(view accelerators, since plain Ctrl+arrows belong to macOS); a two-finger sideways swipe on the
sidebar switches space, once per gesture and without wrap-around.

## Spaces

- **Model.** Spaces are browser-wide and live in Local State (`lb.spaces`); each names its
  profile. Each profile remembers its last active
  space (`lb.active_space`).
- **Tint.** A space's colour tints each window on its own (`BrowserWidget::SetUserColorOverride`),
  so two windows of one profile can show different spaces.
- **Animation.** The sidebar contents slide and fade on a space switch (180 ms).
- **Collapsed strip.** The header hides; pinned tabs stay as icon tiles.
- **Editing.** Right-click a space → Rename…, Icon, Colour, Delete; the same actions are
  commands. The new-space dialog (`new_space_dialog.*`) asks for name, profile (existing or new),
  colour and icon.
- **Switching inside a profile** detaches the current space's tabs, keeping them alive but frozen
  (`SetPageFrozen`), and inserts the target space's tabs. Groups and split views move whole, via
  `DetachTabsAndCollectionsForInsertion` and re-insertion the way Chromium moves tabs between
  windows, so they keep their identity, title, colour and collapsed state. Chromium code assumes a
  tab always has a window, so detached tabs need guards (`patches/lb/tab-metrics-detached-guard.patch`).
- **Switching to a space in another profile** opens or reuses that profile's window at the same
  bounds and hides the current one.
- **After every switch** the controller saves the window's space and calls
  `ResetFromCurrentBrowsers()`; without it Chromium's session file keeps listing detached tabs.
- **Private windows** get one unsaved "Private" space.

## Saving and restoring tabs

- Every space's tabs are saved in Local State (`lb.space_tabs`, space id → list), about a second
  after any tab change and not while a window is closing. The pref is separate from `lb.spaces`,
  which sidebars observe, so saving doesn't redraw every window.
- Saved records keep each tab's active flag, tab group (including its saved-group id) and split
  view. `AddSavedTabs` rebuilds groups first, then splits with `RestoreSplit`, keeping layout and
  ratio.
- Saved tabs open lazily, as in session restore: `chrome::AddRestoredTab` with one navigation
  entry per tab. Only the space's active tab loads; the others load when first shown. Inactive
  spaces' tabs come back as fresh pages, without back/forward history.
- A window's space is saved as session window extra data (`lb_space`,
  `patches/lb/session-spaces.patch`), and "continue where you left off" is the default
  (`patches/lb/continue-session-default.patch`).
- Restoring into an open window (the crash bubble's Restore, or a profile's first window on macOS)
  goes through `lb::OnRestoringIntoWindow`, which saves that window's space first. If the restored
  tabs belong to the space the window shows, only the untouched copies it loaded from saved tabs
  close, so nothing the user opened is lost. If they belong to a free space, the window's own
  tabs are parked and the window moves there, unless it was opened for a chosen space, in which
  case the restored tabs are parked. A space another window uses is never taken twice.
- A window reopened from Recently Closed (Cmd+Shift+T) returns to its space through
  `lb::OnWindowRestored`, called from `BrowserLiveTabContext::Create`.
- **Saved tab groups** (Chromium's `TabGroupSyncService`): a group parked with its space is in no
  tab strip, so it is unsaved while parked and saved again when the space returns; a saved group
  with no strip would crash the Tab groups menu. A group rebuilt from saved tabs reconnects to its
  saved entry once the service is ready (`TabGroupSaver`) instead of adding a copy on every
  restart.
- With more than one profile Chromium asks "Who's using Little Browser?" at launch. Spaces span
  profiles, so launches restore the spaces instead (`patches/lb/profile-picker-off-at-startup.patch`);
  the picker's "Show on startup" box turns it back on.

## Search engine choice

`search/search_choice_dialog.*` runs once per profile while ungoogled's "No Search" is the
default, and again from the command bar. It offers Brave Search, DuckDuckGo, Ecosia, Google,
Microsoft Bing, Qwant and Startpage, alphabetically, without suggestions; Google uses our own
definition because ungoogled replaced the built-in one. After "Not now", a search from the command
bar brings the choice back and runs once an engine is picked.

Browser-modal dialogs don't own their `DialogDelegate` (only allow-listed classes may ask the
widget to), so ours delete themselves when the widget is destroyed.

## Quit warning

With "Warn Before Quitting" on (the default), every user quit asks — Cmd+Q, the menu, the Dock and
AppleScript — with Quit / Cancel / Quit and Don't Ask Again (`patches/lb/confirm-every-quit.patch`).
Restarts, logout and shutdown never ask.

## Checking UI changes

`--lb-ui-snapshot=<png>` reads the window from Chromium's compositor, so UI can be checked without
screen access. On macOS, pages render in their own compositor, which the tool doesn't capture: the
page area is blank. `--lb-sidebar-demo=<mode>` drives states (new space, collapsed strip, tab
group, restore after a crash, …). Modes and flags are listed in
[desktop/lb/README.md](../desktop/lb/README.md); they don't cover keyboard shortcuts, right-click
menus or animation, which need real input.
