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
footer at the end, and the "+ New Tab" row in a slot of the tab list's vertical layout, under
Chromium's own pinned/unpinned separator, which the slot shows whenever there are pinned tabs.
Chromium's new-tab button is hidden; its bottom container stays, because it is what fills the
space between the tabs and the footer. The header, footer and row switch to their compact,
icons-only form (`lb::IsSidebarCompact`) while the strip is collapsed and not expanded on hover;
an expanded strip dragged narrow keeps the full form, as its tabs do.

**New Tab Group** runs `IDC_CREATE_NEW_TAB_GROUP` through `SidebarController::NewTabGroup()`, which
remembers the blank tab the group starts with. When another tab joins that group the controller
closes the placeholder, but not while a tab drag is under way (a dragged tab passes through groups).
Clicking the placeholder opens the command bar in `kOpenHere` mode, so the chosen page opens in the
placeholder, inside its group: `patches/lb/tab-click-hook.patch` reports every tab row click to
`lb::OnTabClicked`, also on the tab already active, which sends no tab strip notification.
Placeholders work in private windows too.

**Group colour and closing.** A group the user or an extension makes gets its colour in
`SidebarController::OnTabGroupAdded`, which Chromium sends only for brand-new groups: the window's
space hue, mapped to the nearest Chromium group colour by `TabGroupColorForHue` in the model.
Restored groups and groups moved between windows keep their own colour. Chromium keeps every group
as a saved group (`TabGroupSyncService`); Little Browser shows none of them. The group menu is
Chromium's editor bubble built as if saved groups were off (`patches/lb/one-group-close.patch`), so
it has one Close group and no Delete group, and the app menu has no Tab groups submenu
(`patches/lb/no-app-menu-tab-groups.patch`). Close group is the one way a group ends that Chromium
keeps as a closed saved group, so `SidebarController::OnTabGroupWillBeRemoved` deletes that group's
saved copy; groups that end otherwise (last tab closed, ungrouped) are deleted by Chromium, and
groups closed with their window stay with their space. `lb::RegisterProfilePrefs` turns off
Chromium's "delete this saved group?" questions (the `kTabGroupsDeletionSkipDialogOn*` prefs).

**No reopening.** Reopen Closed Tab (File menu and the window's right-click menu), Cmd+Shift+T, and
the recently closed lists in the History menu, the app menu's History submenu and Tab Search are
removed (`patches/lb/no-reopen-closed.patch`).

The footer's "+" menu runs Chromium's own commands: `IDC_NEW_TAB` (the command bar),
`chrome::NewSplitTab` and `IDC_CREATE_NEW_TAB_GROUP`. New split view is offered only while the
active tab shows a page and isn't split already.

**Pinned tabs as a list.** Upstream draws pinned tabs as outlined icon tiles in a flow layout. With
`patches/lb/pinned-list.patch` an expanded strip draws them as rows, one per line with icon and
title, like the other tabs, and a close button; only the collapsed strip keeps icon tiles.

**Sleeping pinned tabs.** Every tab close passes through `lb::SleepInsteadOfClose`
(`lb/browser/ui/tabs/pinned_tab_sleep.*`, hooked into `TabStripModel::CloseTabs` by
`patches/lb/pinned-tab-sleep.patch`). A pinned tab the user closes is kept, and right after the
close returns another tab is shown (the one focused last, preferring tabs outside collapsed groups
and awake tabs and leaving out tabs closed together with it, or a new tab; `lb::ChooseTabToShow`) and the pinned tab's page is unloaded with Chromium's
own discard (`TabLifecycleUnitExternal::DiscardTab`, the call `chrome.tabs.discard` makes, which
has no eligibility policy), marked `NeedsReload()` so it loads again whenever it is shown. A tab
muted on its own stays muted. A page with a beforeunload handler that the user has typed into
(`PageNode::HadUserEdits`, the check Chromium's memory saver makes before a discard) may hold
unsaved input, so it is only hidden, not unloaded; a handler alone doesn't count, as many pages
register one and never ask. The next close of such a page, or a close while it isn't shown, goes
through Chromium's own close, which asks the page first. These rules are `lb::DecidePinnedClose`
(`lb/browser/tabs/pinned_close.h`, unit tested). The patch also makes a discard clear the old page's media, capture and
device alerts, which Chromium otherwise keeps, since its own policy never discards such tabs; a
tab replaced in a split view closes with `CLOSE_NONE`, so it doesn't sleep.

Closing a pinned tab that is already asleep removes it, as do closes without a user gesture,
closing the window, and closing a pinned tab in a split view (Chromium takes it out of the split). A pinned tab counts as asleep when it is discarded or not loaded yet (as after a
restart or a space switch). A restored session loads only the shown tab
(`patches/lb/no-restore-background-load.patch`); Chromium would load up to 20 restored tabs in
the background, pinned tabs first, waking every sleeping pinned tab at start. It also adds the
selected tab to a restored window first (`patches/lb/restore-selected-first.patch`,
`lb::TabRestoreOrder`, unit tested): the first tab added to an empty window becomes active for a
moment, and on macOS that shows and loads it, so the top pinned tab would wake although another
tab is selected. `pinned-tab-sleep.patch` draws a sleeping pinned tab faded (`TabIcon` and the
title) with "−" instead of "✕" on its close button; the second click of a double click on "✕",
which lands on that "−", is ignored. Chromium's "press ⌘W again to close a pinned tab" toast is
skipped: `lb::HandleCommand` closes the selected pinned tabs itself.

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
  after any tab change and once more when a window starts closing (`WillCloseAllTabs` with
  `closing_all()`). Closing every tab of a window instead leaves its space's list empty. The pref is separate from `lb.spaces`,
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
- A window reopened through Chromium's tab restore returns to its space through
  `lb::OnWindowRestored`, called from `BrowserLiveTabContext::Create`. The browser itself offers
  no reopening (see "No reopening" above); an extension can, with `chrome.sessions.restore`.
- **Saved tab groups** (Chromium's `TabGroupSyncService`): a group parked with its space is in no
  tab strip, so it is unsaved while parked and saved again when the space returns: Chromium's
  saved-group code expects a connected group to be in a tab strip. A group rebuilt from saved tabs reconnects to its
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

## Password manager choice

`password/password_manager_choice_dialog.*` runs once per profile, right after the search engine
choice, unless one of the offered password managers is already installed. The list and that rule
are in `//lb:model` (`browser/password/password_manager_choice.*`). Picking one runs Chromium's
`WebstoreInstallWithPrompt`: it fetches the extension's details from the store, shows the install
prompt and installs from the Chrome Web Store.

Browser-modal dialogs don't own their `DialogDelegate` (only allow-listed classes may ask the
widget to), so ours derive from `BrowserModalDialog` (`browser_modal_dialog.*`), which deletes the
dialog when its widget is destroyed.

## Ad blocker

Chromium's **subresource filter** does the blocking: the engine Chrome uses against abusive ads,
which ungoogled-chromium keeps. `patches/lb/ad-blocking.patch` turns it on for every site (a preset
that outranks Chrome's own, which only act on sites Safe Browsing lists), and stops it from keeping
a per-site record of where it ran and from showing Chrome's "intrusive ads blocked" notice.

`lb/browser/adblock/ad_blocker.*` keeps the rules fresh. It starts with the browser (a line in
`patches/lb/ad-blocking.patch`, next to Chromium's ruleset service), whatever windows open. Soon
after startup and every six hours it checks when the filter lists were last downloaded (Local State
`lb.filter_lists_updated`); after four days, or if the newest downloaded rules were never indexed
(the browser quit or crashed while Chromium indexed them), it downloads them again, without
cookies, along with the files a list includes (`!#include`, as uBlock filters does for its yearly
lists). `filter_lists.*` (in `//lb:model`) turns the text into the subresource filter's unindexed
format: it skips uBlock Origin's `!#if` blocks that don't apply to Chromium on the desktop, rewrites
uBlock Origin's option names to the ones Chromium's filter-list parser knows (`xhr`, `frame`, `1p`,
`3p`, `from=`, ...), honours `badfilter`, and keeps URL rules only. The file goes to
`Filter Lists/<version>/` in the user data folder, and Chromium's ruleset service indexes it after
startup and shares it with the page processes. A download that fails or takes more than five
minutes keeps the current rules, and the lists are tried again an hour later; with no rules yet,
the lists that did download are used meanwhile. Until the first download finishes after a new
install, nothing is blocked; the lists and the rules are shared by all profiles.

Ads are allowed per site with Chromium's "ads" content setting, which the subresource filter reads.
The site-info panel's "Cookies and site data" view has an **Ads** switch under the third-party
cookies one, on every web page (`adblock/site_ads_row.*`, placed by
`patches/lb/ad-blocking-page-info.patch`); the command bar offers "Allow ads on this site" or "Block
ads on this site" (`adblock/site_ads.*`). Both go through `SetAdsAllowedForTab`, which stores an
exception only where it differs from what applies anyway (the Settings default, a wider site
pattern, or, in a private window, the normal profile), and reload the page; that reload also
applies a change to the cookie switch, so the panel doesn't ask for one. Chrome's third-party cookies "eye"
icon, which repeats the cookie switch on most sites, is hidden
(`patches/lb/no-cookie-controls-icon.patch`), so that panel is the one place for both. Settings → Privacy and security → Site settings → Additional
content settings → **Ads** lists the sites and can turn the blocker off for all of them;
`patches/lb/ad-blocking-settings.patch` gives that page our wording instead of Chrome's ("Intrusive
ads", blocking only on sites known for misleading ads). Element hiding is
not done: Chromium's filter indexes element-hiding rules but nothing in Blink applies them yet.

## Peek and the little window

**Peek** (`lb/browser/ui/views/peek/peek.*`) catches Shift+click in the tabs' web contents
delegate (`patches/lb/peek-hooks.patch`): a link the page opens with `OpenURLFromTab`, and a page
the renderer creates for a `target=_blank` link (`AddNewContents`). The rules are in `//lb:model`
(`lb/browser/peek/peek_rules.*`). `OpensInPeek` takes Shift+click's new-window disposition, started
by the page with a user gesture from a trusted event, in a normal or little window; the link menu's
"Open Link in New Window" is started by the browser, and a script's dispatched Shift+click is
untrusted, so both still open a window.

The Peek's page is its own `WebContents`. Chromium creates most of a tab's helpers (`TabFeatures`)
only when a page joins a tab strip, so `PreviewTab::Adopt` attaches, before its first navigation,
those it needs in the Peek and that must have seen that navigation once it is a tab: page content
settings, the subresource filter (the ad blocker), the popup blocker, the favicon driver, the
password manager, page-load metrics and the extensions' tab helpers, as Chromium's prerendering
does for pages it later turns into tabs. `TabFeatures` reuses them. The Peek is the page's
delegate: dialogs go to the app-modal dialog manager; fullscreen, permission prompts, HTTP sign-in
and external-protocol prompts are refused (they need a tab's dialog managers and a browser to
anchor to); links stay in it, replace it on the user's Shift+click (`ActionForLinkInPeek`), or go
to the window through the popup blocker, and one that opens a tab in front first turns the Peek's
page into a tab (`PromotesPeekBeforeOpening`), so `window.opener` survives. While a Peek is open,
`lb::HandleCommandFirst` (`patches/lb/command-hooks.patch`, before Chromium's command table) sends
each browser command through `PeekCommandActionFor`: Esc/⌘W close the Peek (after the page's
beforeunload), reload, back, zoom and Copy URL act on the Peek's page, other page commands do
nothing, and the window's commands pass.

It is shown in a child widget over the whole browser window: the dimmed backdrop, the card (from
a margin at the top down to the window's bottom edge, rounded at the top only, and a little
narrower than the page area, `BrowserView::contents_container()`; `PeekCardBounds`), the site label
above it (`PeekLabelBounds`; its text, tooltip and warning from `PeekSiteLabelFor`, which takes the
site from `url_formatter::FormatUrlForDisplayOmitSchemePathAndTrivialSubdomains` and the warning
from the page's security level, with the address bar's icon for that warning only) and the round
buttons beside it (`PeekControlBounds`). The card is laid out once at its size and moves
by its layer's transform, so the page isn't resized while it grows from `PeekStartBounds` or
expands; the page's corners are rounded on the web view's layer, as Chromium rounds its own. "Open
as tab" animates the card to the page area, then adds the same `WebContents` to the tab strip after
the page it came from and records the visit in history (a page isn't recorded while in a Peek);
"Open in split view" animates it to the right half (`PeekSplitBounds`), adds it the same way and
pairs it with that page (`AddToNewSplit`, as Chromium's "Open link in split view" does), when
`CanOpenPeekInSplitView` allows. A `TabStripModelObserver` lets the Peek go when the window shows
another tab.

The **little window** (`lb/browser/ui/views/little_window/little_window.*`) is Chromium's popup
window, whose toolbar is a single row with the address. `patches/lb/little-window-toolbar.patch`
adds Copy URL and "Open in ‹space›" to that row; the button reads the space of the profile's most
recently used window with tabs whenever spaces change and when the little window comes to the
front, and moves the page there as Chromium's own "Show as tab" does (detach the `WebContents`, add
it to the window `chrome::ScopedTabbedBrowserDisplayer` finds or opens). New little windows get
their bounds from `LittleWindowBounds` (`little_window_rules.*`): a page of 960 × 680 when the
screen has room, the window (with its title bar and toolbar, measured from the newest little
window) centred, or a step down from the last one while it fits. ⌥⌘N is `IDC_NEW_LITTLE_WINDOW`,
an id of ours (`lb/browser/ui/lb_command_ids.h`) that Chromium's command table doesn't list, so
`lb::HandleCommandFirst` handles it before that table: it opens the command bar in its
little-window mode, which lists only pages to open (`OffersResult`), in a new little window.
`patches/lb/little-window-shortcut.patch` maps the key, taking it from "New Split Tab", which has it
in Chromium on macOS. `patches/lb/little-window-link-menu.patch` adds "Open Link in Little Window"
to a link's menu; it opens with the page's referrer and initiator, as "Open Link in New Window"
does.

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
