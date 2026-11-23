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
| Tab Search always in the strip: no Pin/Unpin in its button's menu or the window menu, no Settings switch, the toggle command does nothing, and a stored "unpinned" (an older profile) cleared when a window opens | `patches/lb/tab-search-always.patch`, `lb::SidebarController` |
| Sidebar header (the space name; no address field, the toolbar has the address) in the vertical tab strip | `lb/browser/ui/views/sidebar/sidebar_header_view.*`, hooked by `patches/lb/sidebar-hooks.patch` |
| Footer: space switcher and the "+" menu (new space; new tab, split view, tab group), below the strip | `lb/browser/ui/views/sidebar/sidebar_footer_view.*` |
| "+ New Tab" between pinned and today's tabs, replacing Chromium's new-tab button | `lb/browser/ui/views/sidebar/new_tab_row_view.*`, in a slot added by `patches/lb/new-tab-row.patch` |
| Pinned tabs as list rows | `patches/lb/pinned-list.patch` (`tab_view.cc`, `tab_view_vertical_layout.cc`, `pinned_tab_container_view.cc`) |
| Pinned groups: header rows, indents, colour lines, session data, Add to New Group, Pin Group | `lb/browser/ui/tabs/pinned_group_tracker.*`, `lb/browser/ui/views/sidebar/pinned_group_layout.*`, `patches/lb/pinned-groups.patch` |
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

**Pinned groups.** Chromium's tab model can't group pinned tabs (its pinned collection holds only
tabs and split views), so a group among the pinned tabs (ux.md §6.1) is ours, over Chromium's pinned
tabs: to Chromium and extensions they stay pinned tabs in no group, so everything pinned tabs do
(sleeping, restoring, the collapsed strip's icon tiles, tabs on top) keeps working.
`PinnedGroupTracker`, one per window and owned by `SidebarController`, keeps each group's name,
colour and collapsed state and which tabs are in it, also while their space is parked; another
window that takes a parked space takes its tabs' groups too, and a tab that goes to another window
leaves its group. After every tab strip change, and after a pin or unpin that leaves the tab where
it is (`OnTabPinnedStateChanged`), the tracker keeps each group one unbroken run: a tab that moved
or was inserted takes the group of where it landed, judged by neighbours that didn't move with it
(`lb::GroupAfterMove`: between two tabs of a group it joins, next to its own group it stays,
elsewhere it leaves), then `lb::NormalizePinnedGroups` repairs the rest. It moves tabs as units, a
split view whole (`MoveSplitTo`), so no move takes a split apart. In the expanded vertical strip
`patches/lb/pinned-groups.patch` has `PinnedTabContainerView` ask `lb::PinnedGroupLayout` (a
property of the container) for each child's row: a header view above a group's first tab, an indent
for its tabs, hidden tabs in a collapsed group (all but the active one, `lb::PinnedRows`), and a
colour line in the indent, where tab rows, which have their own layers, can't paint over it. The
layout reads a child's tabs from its nodes, never from the tab collection, which Chromium may be
changing as it lays the container out. Header views are added outside layouts, one per group, and
removed when the group goes; each looks like Chromium's vertical group header (a pill in the group's
strip colour, paler in an inactive window, with the name, the count while collapsed and the chevron
in its most readable colour). A header's click collapses and expands, a double click renames inline,
and its menu (`lb::PinnedGroupMenu`) has Rename, Colour, Unpin Group (the tabs become a Chromium
group with the same name, colour and collapsed state, expanded when it holds the shown tab), Ungroup
and Close: `lb::DecidePinnedGroupClose` picks the awake tabs, which `lb::SleepPinnedTabs` puts to
sleep together (a split comes apart first, and the shown tab sleeps first so none of the group is
shown instead), or Remove once they all sleep. A tab in a collapsed pinned group counts as collapsed
for Chromium too (`TabStripModel::IsTabCollapsed`), so Ctrl+Tab and the choice of the next tab skip
it. The same patch makes the tab menu's Add to New Group and Tab > Group Tab make a pinned group of
pinned tabs where pinned groups show (`lb::AddToNewPinnedGroup`, placed by
`lb::NewPinnedGroupPosition`, its name being edited; `lb::TogglePinnedGroup` also takes tabs out),
and adds Pin Group to a today group's menu. Each member's group is written to Chromium's session as
tab extra data (`lb_pinned_group`, also when Chromium rebuilds the session file and in tab restore's
record of a closed tab or window), and `lb::OnTabRestored`, called from `chrome::AddRestoredTab`,
gives a restored tab its group back; saved space tabs carry it in the same `group` record today
groups use, on pinned tabs. During a drag the pinned container tells `PinnedGroupLayout` where each
dragged row is drawn (a transform moves it, not its bounds) and where its place is, and the row
joins or leaves a group as `lb::DraggedTabInPinnedGroup` decides, with Chromium's thresholds for
today groups and its own place counted in its group, while the drag lasts; tabs that are all of
their group's tabs keep it, as `lb::GroupAfterMove` does when they are dropped. While a drag runs the
tracker remembers each tab's group before its first change, and a cancelled drag (Esc) gives those
back once Chromium has put the tabs back (`lb::OnTabDragEnded`, from
`TabDragController::EndDragImpl`). Dragging a pinned group's header is our own drag. The pinned area
clips what is drawn outside it, so an image of the group, its header and tabs painted as Chromium
paints drag images, follows the pointer in a small window of its own over the browser window while
the group's tabs fold away under the header; a line, in a small window of its own too, shows where
it would land. Every tab counts for that place, one hidden in a collapsed group at its group's
header, so a drop never lands inside a group (`lb::GroupDropIndex`, `lb::GroupDropLineY`). On
release `lb::DecideGroupDrop` says what happens: above the middle of "+ New Tab" the group moves
among the pinned tabs, below it it becomes a today group among the today tabs. A drag of today
groups dropped above that middle pins them, any other dragged tabs with them
(`lb::OnTabGroupDropped`).

**Sleeping pinned tabs.** Every tab close passes through `lb::SleepInsteadOfClose`
(`lb/browser/ui/tabs/pinned_tab_sleep.*`, hooked into `TabStripModel::CloseTabs` by
`patches/lb/pinned-tab-sleep.patch`). A pinned tab the user closes is kept, and right after the
close returns another tab is shown (the one focused last, preferring tabs outside collapsed groups
and awake tabs and leaving out tabs closed together with it, or a new tab; `lb::ChooseTabToShow`;
a collapsed pinned group counts as a collapsed group) and the pinned tab's page is unloaded with Chromium's
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
dragged tabs, split views whole; the drag then places them as within any area. Pinned tabs, a list
here, are dragged up and down only (`pinned-list.patch`). A drag that includes a tab group stays
among the today tabs, since Chromium's model has no pinned groups; dropped above the middle of
"+ New Tab", the group then becomes a pinned group where it was dropped (`lb::OnTabGroupDropped`,
from `TabDragController::EndDragImpl` in `pinned-groups.patch`). While it is held there, a line
shows that place (`lb::OnTabGroupDragged`, from `TabDragController::ContinueDragging`). Upstream
rebuilds a tab's view when its pin state changes, which would free the views the drag holds, so a
dragged tab keeps its view; Esc restores the original pin state. `patches/lb/no-tab-detach.patch`
makes a dragged tab never detach into a new window.

The toolbar stays Chromium's, as a **slim toolbar** above the page card: back, forward, reload, the
address bar, extensions, downloads, profile and menu. Moving them into the sidebar would be a large
change to `BrowserView` and `ToolbarView`, rebased every release.

Rebase risk is concentrated in `browser_view_tabbed_layout_impl.cc`, which changes often because
vertical tabs are new upstream. The page-card patch there stays a few lines; all logic is in `lb/`.

## Command bar

Cmd+T, the new-tab buttons and Cmd+L open the command bar. It lists open tabs
across spaces, commands, bookmarks (the best six, from `BookmarkModel`, with their folder; a page
listed as a bookmark isn't listed again from history), history and the address or search entry;
the ranking boosts per kind are in `command_matching.*`. It has a
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

The first-run questions (the import offer, the search engine, the password manager) each open
when the one before closes. Closing the window or quitting closes the open one from inside the
window's teardown (`~Browser` has already reset its window), so each question first asks
`CanAskFirstRunQuestionIn` (`browser_modal_dialog.*`, the rule in `//lb:model`'s
`browser/first_run/first_run_questions.*`) and opens nothing over a closing window or while the
app quits.

## Import

Behaviour is in [ux.md](ux.md) §13, and the split between lb-core and the shell in
[design.md](design.md) §7.6. Everything is in `browser/importer/` unless named otherwise.

- **Sources.** `import_sources.*` finds the browsers on the Mac, each once with what the dialog's
  second step lists: each Chrome-family browser with its profiles and Arc with its spaces (read
  through lb-core's `arc_sidebar`, a space outside Arc's default profile labelled with that
  profile's name), from lb-core's table of browsers (`Local State` in each data folder); Firefox
  with its profiles and Safari from Chromium's `ImporterList`. The default browser comes first
  (Launch Services' handler for https).
- **Reading** a Chrome-family profile: `chromium_profile_reader.*` (in `//lb:model`, unit tested)
  on a `MayBlock` single-threaded runner (Chromium's session backend needs one). It copies the
  profile's SQLite files (with any `-wal` and `-journal` files) and its `Sessions` folder into a
  temporary folder and reads only the copies: `sql::Database`, choosing columns and table names
  by what each file has, so older and newer Chromium versions both read; open tabs through
  `sessions::CommandStorageBackend` and `RestoreSessionFromCommands`; extensions from `Preferences`
  and `Secure Preferences`, with names an extension keeps in its `messages.json`. Addresses: only
  local ones (`addresses.record_type` 0 where the table holds Google-account addresses too). Text
  goes to lb-core as valid UTF-8 (`import_text.*`; a cookie whose name or value isn't text stays
  behind, since it must stay byte for byte). Chromium is moving `Sessions` and `Bookmarks` to
  encrypted copies (`Sessions_Encrypted`, `EncryptedBookmarks2`): a browser first saves both at once,
  then only the encrypted one. The plain file is read unless the encrypted copy is more
  than a minute newer or there is no plain one (`PlainCopyIsStale`); then the item is a problem line
  ("this browser keeps them encrypted"). Cookies only when ticked: the source's "‹browser› Safe
  Storage" password comes from the keychain (`import_keychain.mm`, which only reads, never adds an
  item), and `cookie_decryptor.*` opens each value as Chromium's `os_crypt` does on macOS
  (PBKDF2-SHA1 key through BoringSSL, AES-128-CBC). Text and rows go through lb-core's rules
  (`lb::core`, `lb/core_bridge`).
- **Writing**, on the UI thread, by `chromium_import.*`, once each service has loaded. Bookmarks:
  `BookmarkModel` `AddFolder` / `AddURL` from lb-core's merge, under the bookmarks bar, other or
  mobile node. History: `AddHistoryPages`, a history DB task that adds only pages history doesn't
  have or has with an older visit, and counts them. Search shortcuts: Chromium's own
  `ProfileWriter::AddKeywords`, which skips keywords already here. Addresses:
  `AddressDataManager::AddProfile`, after lb-core leaves out those already saved; Chromium drops
  addresses silently while "Save and fill addresses" is off, so the import reports that instead,
  and a web database that never loads is given up on after a while. Open tabs:
  `SidebarController::AddImportedTabs`, as tabs that load when first shown. Cookies: the profile's
  `CookieManager::SetCanonicalCookie`, created now (the store caps a cookie's life at 400 days
  from its creation), a reply that never comes counting as not written. Extensions are only
  listed; the dialog installs them. If the profile goes away first, the import ends with that as
  a problem.
- **A whole browser** (its chosen Chrome-family profiles, or Arc's chosen spaces):
  `browser_import.*` reads the browser's `Local State` for its profiles' names and, for Arc,
  `StorableSidebar.json` through lb-core's `chosen_arc_spaces` (the chosen spaces, Favourites in
  each profile's first chosen one); Arc's source profiles are those of the chosen spaces. It maps
  the source profiles through lb-core's `map_profiles` (making missing ones with
  `ProfileManager::CreateMultiProfileAsync`), places the spaces through lb-core (Arc's spaces, or
  one space per Chrome-family profile, named after it) with `SidebarController::AddImportedSpace`,
  or `AddImportedTabs` for an empty space it fills (first run's welcome page counts as empty,
  `IsUntouchedTabUrl`), and then imports each source profile into its profile as above, one after
  another (two may go into one profile), a Chrome-family profile's open tabs into its space. The
  browser's keychain item is read once for all of them, and with several profiles each problem
  line names the one it came from. Without "Spaces, profiles and tabs" it makes no profile or
  space, brings no tabs and imports everything into the current profile.
- **Chromium's importers** (Firefox, Safari, bookmarks HTML): `other_import.*` runs
  `ExternalProcessImporterHost` with a `ProfileWriter` subclass. Its `AddBookmarks` turns the
  entries' folder paths into lb-core's flat list (`imported_bookmarks.*`, which maps the source's
  root folders, such as the toolbar and Firefox's "unfiled", to our places) and merges them,
  instead of making an "Imported from" folder; its `AddHistoryPage` passes history through
  lb-core's rule (web pages, once each) and `AddHistoryPages`; its `AddKeywords` adds search
  shortcuts only when they are ticked, and counts them. Firefox's chosen profiles are imported
  one after another. An import that never started (Firefox
  left open and its lock prompt dismissed) or didn't finish is named in the summary. Safari's
  export `.zip` is unzipped (`zip::Unzip`, only its `.html` and `History*.json` files; its
  passwords and cards never reach the disk): the bookmarks go to Chromium's bookmarks-file
  importer, every history file through lb-core, and the folder is deleted in the background.
- **The dialog** is `ui/views/importer/import_dialog.*`, a `BrowserModalDialog` that steps from
  the browser (Next) to its profiles or spaces and items (with a Back button as the dialog's
  extra view) to the progress line and the summary (`import_choices.*` in `//lb:model` holds what
  each source offers, when the profiles or spaces are listed, when Import can be pressed, the
  labels and the summary line). The first step lists each browser with its macOS app icon
  (`import_app_icon.mm`, from the browser's bundle id, a globe when it isn't installed; the two
  file choices get a document glyph) and the "what comes along" step gives each item a leading
  icon. Each row is an icon beside a radio or a checkbox; the radios sit in those rows yet stay
  one group through a cascading radio-group view set on the body. While a Chrome-family browser
  or Arc imports, the progress step lists the chosen items, each with a spinner that becomes a
  check as it comes over: the profile writer reports each item once it is written (or has nothing
  to write), through `ChromiumImportRequest::on_item_written`; `ImportBrowser` passes on the
  reports of the last source profile that imports each item, so a check means every profile's
  share is in, and reports spaces, profiles and tabs together once the tabs have joined their
  spaces (an item still unreported when the import ends is reported then). It then
  installs the ticked
  extensions one after another (`importer/extension_install.*`: Chromium's
  `WebstoreInstallWithPrompt` with no prompt, so `WebstoreStandaloneInstaller` goes straight to
  the store download, and no "added" bubble); what follows the dialog (the next first-run
  question) waits for the installs. Files are picked with
  `ui::SelectFileDialog`, attached to the dialog's own window (a second sheet on the browser
  window would wait behind the dialog's).
- **Entry points.** `IDC_IMPORT_SETTINGS` (the app menu's "Import Bookmarks and Settings…") is
  taken in `lb::HandleCommandFirst`, which opens the dialog over a normal window of the original
  profile (a private window's is never used) and does nothing in guest or forced-incognito
  windows; the command bar's "Import from another browser" runs it, and so does Settings'
  importData route, which the row, the app menu with no window open and the address all reach
  (`patches/lb/import-settings-row.patch`), instead of Chromium's WebUI dialog. On first run
  the dialog opens by itself before the other questions (`MaybeShowImportOffer`, Local State
  `lb.import_offered`, on the browser's first run only; `NextImportOfferStep` in
  `import_choices.*`), with the browsers it already found, an introduction on its first step and
  "Not now" for Cancel; other windows' first-run questions wait until the dialog and its installs
  are over. The dialog's body is a `views::ScrollView` clipped to 480 high, so a long step
  scrolls under a fixed title and buttons.
- **Address autofill** is on by default (`browser/prefs/pref_defaults.*`); ungoogled-chromium
  turns it off, and Chromium then drops imported addresses.
- **Check.** The `import-fixture`, `import-arc`, `import-arc-unreadable`, `import-edge`,
  `import-files`, `import-dialog`, `import-choices`, `import-progress`, `import-first-run` and
  `import-first-run-close` demos import the made-up sources in `lb/test/data/import/home`, and
  `import-extensions` installs a store extension as an import does (`lb/README.md`); `import` with
  a real profile folder logs only how many of each item came along.

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
ads", blocking only on sites known for misleading ads).

**Element hiding** hides what is left of a blocked ad. Each list update also saves the lists'
hiding rules ("Hiding Rules", next to the ruleset; lines that aren't UTF-8 and exceptions a
`badfilter` cancels are left out), which lb-core indexes in the browser process
(`adblock/element_hiding_host.*`); they are read at startup without waiting for it to finish.
Every frame gets an `ElementHidingAgent` (`lb/renderer/element_hiding_agent.*`, created by
`patches/lb/element-hiding.patch`), which talks to the browser over
`lb/common/element_hiding.mojom`; prerendered pages may ask too. When an http(s) document starts
it asks for its site's rules plus the generic rules that name no class or id first, and inserts
them as a user stylesheet, whose `display: none !important` wins over the page's styles. A
document that asks before the rules are loaded gets its answer once they are. Where generic rules
apply, it then sends the class and id names the document uses (each once, up to 20,000 names of
up to 256 characters, `lb/common/page_names.*`) at DOMContentLoaded, at load and 1, 3 and 8
seconds later, and inserts the generic rules for those names. A frame is answered by its own
site's rules; the browser also passes the hosts of the pages that embed it, whose exceptions
(`$elemhide`, `$document`, `$generichide`) apply in it too, and each host's public suffix, which
`name.*` rules need. Where the top page allows ads nothing is hidden. A ruleset whose hiding
rules didn't load (missing, or damaged) makes the next check download the lists again, and a
partial download then fills them in.

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
in Chromium on macOS. `patches/lb/little-window-menu.patch` adds the File menu's "New little
window" (`little_window_menu.mm`, right after "New Incognito window", with ⌥⌘N) and has every
browser window enable it, since the window's menu check asks Chromium's command table, which
doesn't list our id. `patches/lb/little-window-link-menu.patch` adds "Open Link in Little Window"
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
