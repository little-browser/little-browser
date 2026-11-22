# Little Browser — Screen layout & interaction

> Companion to [design.md](design.md). Base: **Arc's approach** (sidebar-first, content-first,
> keyboard-first), trimmed to our minimal feature set. How the desktop sidebar is built:
> [desktop-ui.md](desktop-ui.md).

**Built so far (macOS desktop):** the sidebar layout on upstream's vertical tabs, the command bar,
spaces and profiles (new-space dialog, switching by shortcut, swipe or footer, per-space tint, saved
tabs per space), pinned tabs as a list, the first-run search-engine and password-manager choices,
the ad blocker, Peek, the little window, the quit warning, Chromium's own tab groups and pinned
groups, bookmarks in the command bar, and import from other browsers with its first-run offer
(section 13). Everything else here — auto-archive, group suggestions and auto-grouping, the distinct
incognito look, top-mode additions, hiding the sidebar with Cmd+S, Cmd+D to pin, the ⌥⌘G shortcut
for groups, the footer's Downloads button and the whole mobile section — is specified but not built.

---

## 1. Principles

1. **Content first.** The page fills almost the whole window. No bookmarks bar, no status bar.
   Browser chrome lives in one sidebar (or one slim row in top mode), plus, on desktop, Chromium's
   slim toolbar above the page (back, forward, reload, address, extensions, menu;
   [desktop-ui.md](desktop-ui.md)).
2. **One place for everything.** Address, tabs and spaces are all in the sidebar.
3. **Keyboard first.** A command bar opens everything: new tab, search, switch tab, commands.
   Every action has a shortcut; nothing needs a mouse.
4. **Calm.** No badges, promos, pop-ups, "what's new" pages or permission nags. Nothing moves unless
   the user caused it.
5. **Fluent.** Every transition is animated, short (≤ 240 ms), interruptible, and spatially
   consistent: spaces sit left→right, so swiping and animations always move the same way.
6. **Forgiving, not undoable.** Tabs left alone are archived and stay searchable, and pinned tabs
   never disappear. Closing is deliberate and final: there is no Reopen Closed Tab, group or window.

---

## 2. Desktop window — sidebar mode (default)

```
┌───────────────────┬──────────────────────────────────────────────────┐
│ ● ● ●       ≡   ⌕ │ ‹ › ⟳ ╭ https://github.com/ ─────────╮  🧩 ⬇ ◯ ⋮ │
│ ╭───────────────╮ │╭────────────────────────────────────────────────╮│
│ │ github.com    │ ││                                                ││
│ ╰───────────────╯ ││                                                ││
│ ▸ Personal        ││                                                ││
│ ───────────────── ││                                                ││
│   📌 Calendar     ││             page (rounded, inset)              ││
│   📌 Mail         ││                                                ││
│ ───────────────── ││                                                ││
│   +  New Tab      ││                                                ││
│   ◦ Hacker News   ││                                                ││
│   ◦ MDN: flexbox  ││                                                ││
│                   ││                                                ││
│     ● ○ ○      ＋ │╰────────────────────────────────────────────────╯│
└───────────────────┴──────────────────────────────────────────────────┘
```

Beside the sidebar, above the page, sits Chromium's slim toolbar: back, forward, reload, the full
address, extensions, downloads, profile and the menu ([desktop-ui.md](desktop-ui.md)). The
sidebar's address pill shows the domain; the toolbar shows the full address.

Sidebar zones, top to bottom:

| Zone | Content | Notes |
|---|---|---|
| Title row | Window controls, the sidebar toggle, the command bar button | Drag area for the window |
| Address pill | Domain only (`github.com`); the full URL as its tooltip | Click or Cmd+L → command bar with the full URL selected |
| Space header | Icon + name of the current space; a profile badge (🔒 and the profile's name) if it isn't the default profile | Space colour tints the sidebar |
| Pinned tabs | Per space, persistent; **list rows** (icon and title), not tiles, and pinned tab groups among them (section 6.1). Drag to reorder; drag across "+ New Tab" (or use the right-click menu) to pin and unpin | Wandered off the pinned URL → hover shows ↺ "back to pinned page" |
| Divider + New tab | "+ New Tab" row, opens the command bar (Cmd+T) | The divider shows only when the space has pinned tabs |
| Today tabs | Unpinned tabs, newest first | Optional auto-archive (section 6) |
| Footer | Space switcher (each space's icon, the current one highlighted), **＋** in the bottom-right corner; a Downloads button on the left (not built yet) | ＋ opens a menu: New space, then, set apart, the tab actions: New tab, New split view (only while a page is open), New tab group |

- **Page area** is inset by 8 px with rounded corners and a soft shadow; this frames the page and
  makes the sidebar feel like part of the window, not a toolbar.
- **Hide the sidebar** with Cmd+S. The page then fills the window; moving the pointer to the left
  edge slides the sidebar in *over* the page, and it slides out when the pointer leaves.
- **Sidebar width** is draggable (200–400 px).
- Light/dark follows the OS; space colour is a subtle tint, never a saturated block.
- **Ads** are blocked on every site by the built-in blocker. When a site doesn't work, or to
  support it, the site-info panel (the icon left of the address) → "Cookies and site data" has an
  **Ads** switch next to the third-party cookies one; the command bar has "Allow ads on this site".
  That panel is the one place for both switches: there is no third-party cookies "eye" icon in the
  address bar.

## 3. Desktop window — top mode

For people who prefer horizontal tabs. Two rows: tabs, then a toolbar with navigation and the
full address — upstream Chromium's own layout:

```
┌──────────────────────────────────────────────────────────────────────┐
│ ● ● ●  [Personal ▾] (W)(GH)(YT) │ 📌 📌 │ GitHub ✕ │ Hacker News │ +   │
│ ‹ › ⟳  🔒 github.com                                                 │
├──────────────────────────────────────────────────────────────────────┤
│                               page                                   │
└──────────────────────────────────────────────────────────────────────┘
```

- Pinned tabs shrink to icons; today tabs are chips with titles.
- Command bar, Peek, Little window and all shortcuts work the same as in sidebar mode. A sideways
  swipe on the tab row switches space.
- In the real browser this is Chromium's upstream horizontal tab strip and toolbar plus our space
  controls; no layout patches.

## 4. Command bar

The centre of the browser. A floating palette in the middle of the window.

| Shortcut | Opens as |
|---|---|
| Cmd+T | New tab: empty input |
| Cmd+L / click address pill | Edit current URL: full URL, selected |

- Results, in one list, ranked by fuzzy match: **open tabs** (switch, across all spaces) ·
  pinned · bookmarks (by title or address; the folder they're in shown beside them) · history ·
  archived tabs · **commands** (“Toggle sidebar”, “New space”, “Tabs on top”, “Incognito window”,
  “New little window”, “Copy URL”, “Allow ads on this site” / “Block ads on this site”, “Import
  from another browser”…) · URL / “Search with ‹engine›”.
- Enter takes the top result; ↑/↓ moves; Esc closes and returns focus to the page.
- Opens in < 50 ms; it's a native/overlay view, not a page.
- Search suggestions from the engine appear only if the user turns them on (privacy default).

## 5. Spaces and profiles

- A **space** is a set of pinned and today tabs with a name, icon (emoji) and colour.
- Switch: Ctrl+1…9, Ctrl+Option+← / →, the footer dots, or a **two-finger sideways swipe on the sidebar**
  (on the tab row in top mode). The sidebar contents slide in the swipe direction; the page
  cross-fades to that space's last active tab. On the page itself the swipe stays **back /
  forward**, as in Chrome and Safari, and sideways-scrolling content keeps working.
- A **profile** is one set of logins, cookies and history. Every space uses a
  profile; several spaces can share one. Everyone starts with just the **default profile**, named
  **Personal** like its first space, so the concept stays out of sight until it's needed.
- Space menu (right-click its dot or header) → **Profile ▸** Personal / Work / … / New profile. A space
  outside the default profile shows a small 🔒 badge with the profile's name, so it's clear its logins
  are separate. Moving a space or tab to another profile reloads it in that profile's cookie jar.
- **New space** (the footer's ＋ menu, or the command bar) opens a small dialog: **name**, **profile**
  (an existing one, preselected to the current space's, or **New profile…** with its name) and
  **theme** (colour and icon); **Cancel** or **Create**.
- Spaces are browser-wide. A Chromium window belongs to one profile, so switching to a space in
  another profile brings that profile's window up in the same place and hides the current one.
- Each space keeps its tabs across restarts: the browser continues where you left off by default,
  and every window comes back on its space. Closing a window keeps its space's tabs too; closing
  all of a window's tabs leaves its space empty.
- Tabs of inactive spaces go to sleep after a while (memory goal); sleeping tabs look dimmed and
  wake on click.
- Right-click a tab → “Move to space ▸”.

## 6. Tabs

- **Pinned** tabs are the space's permanent set. Closing a pinned tab (Cmd+W, its ✕, the tab menu,
  a middle click) puts it to sleep instead of deleting it, without asking: it keeps its place, its
  page unloads, it is drawn faded, and clicking it loads the page again. The sidebar shows the tab
  used last instead, preferring tabs that are awake and tabs that aren't hidden in a collapsed
  group (a new tab if there is none). A sleeping pinned tab's button is "−", which removes it;
  closing it any other way removes it too, but a double click on "✕" only puts it to sleep. A page
  you have typed into that asks before it is left (it may hold unsaved input) is only hidden, not
  unloaded; closing it
  again closes it once the page has asked, and so does closing it while it isn't shown. A pinned
  tab in a split view closes as usual. A pinned tab that hasn't loaded since a
  restart or a space switch counts as sleeping. Cmd+D pins/unpins.
- **Today** tabs are everything else. **Auto-archive**: today tabs not used for a while are
  closed and moved to the archive (searchable in the command bar). This keeps the sidebar short and
  memory low. Choices: 12 h, **24 h (default)**, 7 days, 30 days, or Never. The first-run screen asks
  for it right after the search engine and password manager choices (section 12), with 24 h
  suggested; Settings can change it later.
- Drag to reorder pinned tabs, today tabs and groups. Dragging a tab across "+ New Tab" pins or
  unpins it, live, and it can go anywhere in the other area, a group included; the right-click
  menu pins and unpins too. A tab group dragged across "+ New Tab" is pinned or unpinned whole.
  Dragging never opens a new window: a tab dragged past the sidebar stays in its window, and
  one dragged onto another window's sidebar moves into that window. A tab dragged out of a group
  leaves it; dropped between two groups, or between a
  group's tabs, it lands there (joining the group only when dropped inside it).
- **Split view** is Chromium's own: two tabs side by side in one sidebar row. The footer's ＋ menu
  (New split view) pairs the current page with a new tab; Chromium's tab menu adds more ways.
- Middle-click or ✕ on hover closes. Speaker icon on tabs playing audio; click to mute.
- Ctrl+Tab / Ctrl+Shift+Tab cycle tabs in sidebar order.

### 6.1 Tab groups

Named, collapsible runs of tabs — Arc-style folders — among today's tabs or among the pinned tabs
(see *Pinned groups* below).

```
│ ⌄ 📁 Civic Pipelines        │      top mode:
│ ┃  ◦ deploy-staging (#8812) │      (● Civic Pipelines) [deploy-sta…][test-e2e…][build…]
│ ┃  ◦ test-e2e (#8807)       │                          ▔▔▔▔▔▔▔▔▔▔▔▔ group colour ▔▔▔▔▔▔
│ ┃  ◦ build (#8790)          │
│ › 📁 Google Sheets       3  │  ← collapsed: only the count (and the active tab, if inside)
```

**Making a group** — groups appear only when the user acts, except for one narrow automatic rule:
- Select tabs with ⌘-click / ⇧-click, then **⌥⌘G** (⌘G stays *Find next*, as in every Mac
  browser), or right-click → *Add to New Group*. The group starts with an editable name.
- **Drag a tab onto the middle of another tab** → a new group of the two (or join its group).
  Dragging onto a group header, or between its members, joins; dragging out leaves.
- **One-click suggestion**: when **3** today tabs share a project or site (fixed, no setting),
  hovering one shows a quiet group button (it outlines the tabs it would take), and the command bar
  offers *"Group 3 tabs from gitlab.com/civic/pipelines"*. One click groups them **in place** — at
  the first tab's position — with an editable suggested name (*Civic Pipelines*). Loose tabs that
  match an existing group are offered as *"Add 2 tabs to “Civic Pipelines”"*.
- **Opened one after another** (on by default): when the **3rd** new tab in a row comes from the
  same project or site (no other new tab in between; searches don't count), the three group
  themselves in place, named from the key; later tabs from the same run join.
- **Auto-group tabs by site** (a setting, **off** by default): also gathers matching tabs opened
  at different times. Groups still form in place; nothing else in the sidebar moves.

**What "the same project or site" means** — a key per tab:
- Hosts that hold many projects use host + project path: github.com, bitbucket.org, codeberg.org
  (`owner/repo`), gitlab.com and **self-hosted GitLab** (everything before GitLab's `/-/`),
  Jira / Confluence on `*.atlassian.net` (project or space key), docs.google.com (Docs / Sheets /
  Slides / Forms), notion.so (workspace), figma.com (file). The built-in list is updated with
  releases.
- Users can add their own: right-click a tab → *Group by Project on ‹host›* (synced with settings;
  the same item turns it off).
- Everything else uses the registrable domain (Public Suffix List).
- Example: GitLab job tabs of one project become *Civic Pipelines*; grouping by domain alone would
  have mixed in every other gitlab.com project.

**Living with groups**
- **New Tab Group** (the footer's ＋ menu) makes a group with one blank placeholder tab; clicking it opens the command bar, and the chosen page opens in the placeholder, inside the group. The placeholder closes by itself when another tab joins the group.
- Click the header to collapse / expand; double-click (or its ⋯ menu) to rename; the menu also has
  Ungroup and Close group. Closing a group closes its tabs for good: it doesn't stay behind as a
  hidden saved group, and nothing reopens it. Tabs removed from a group are never auto-grouped again.
- Collapsed groups' tabs **sleep sooner**: after a third of the normal sleep time, at least
  1 minute.
- **Archive**: a group is archived **as a unit**, once none of its tabs has been used for the
  auto-archive time; the command bar finds it by name and restores the whole named group.
- Moving a tab to another space, or pinning or unpinning it alone, takes it out of its group.
- **Phones**: groups sync, pinned groups too. In the all-tabs view each group is a collapsible
  section (name, count) among today's cards, and a pinned group is one among the pinned rows; a
  tab's hold menu has *Add to New Group*, *Add to Group ▸* and *Remove from Group*. No
  suggestions or drag-to-group on phones; the "opened one after another" rule applies there too.

**Pinned groups** — a group in the pinned area. Its tabs are pinned tabs: they stay, closing one
puts it to sleep, and they are never archived. It looks and works like any group: name, colour,
collapsing, renaming and Ungroup are the same.
- **Making one**: the same ways as a today group, with pinned tabs (select them, then ⌥⌘G,
  right-click → *Add to New Group* or Tab → *Group Tab*); it goes where the first of them is, but
  never inside another group. Or pin a whole today group (drag it across "+ New Tab", or its menu's
  *Pin Group*). *Unpin Group*, or dragging it back across, makes it a today group again with the
  same name, colour and collapsed state (expanded if it holds the shown tab). A tab dragged into a
  pinned group from the today tabs is pinned on the way.
- **Dragging** a group by its header lifts it: its tabs fold away under the header, a picture of
  the group follows the pointer, and a line shows where it will land, never inside another group.
  A today group held above "+ New Tab" shows the same line. Esc puts a dragged tab back in the group
  it was in.
- **Close group** puts every tab in it to sleep, as closing a pinned tab does (a split view in it
  comes apart, as a sleeping tab can't be shown in one); the group keeps its place, name and
  colour. Once all its tabs are asleep the menu offers **Remove group** instead, which removes the
  group with its tabs.
- One-click suggestions and both automatic rules group only today tabs.
- With tabs on top, and in the collapsed strip, a pinned group's tabs show as ordinary pinned
  icons, in order, and grouping pinned tabs there makes a today group, as Chromium does.
- Extensions see a pinned group's tabs as pinned tabs in no group (`chrome.tabGroups` lists only
  today groups).

**Real browser** — built on **Chromium's own tab groups** (`TabGroupModel` / saved tab groups), so
extensions (`chrome.tabGroups`), session restore and sync of open tabs keep working; our sidebar and
suggestion logic sit on top. Pinned groups are our own, on top of Chromium's pinned tabs, because
Chromium can't group pinned tabs. A new group starts in the Chromium group colour nearest its space's colour (a Blue space makes
blue groups, Teal makes cyan); the group menu changes it.

## 7. Peek (link preview)

- **Shift+click** a link, also one that opens a new page (`target=_blank`), in a normal or a little
  window. It replaces Chromium's Shift+click "open in new window", which we hardly need; the link
  menu's "Open Link in New Window" stays, and Alt+click stays "download link".
- Only the user's own Shift+click opens a Peek: a page can't open one with a script, even during a
  click.
- The card grows from the click point while the whole window dims. Open, it runs from a margin at
  the top of the window, over the toolbar, all the way down to the window's bottom edge, and is a
  little narrower than the page area (about 8% off each side), with no toolbar of its own: just the
  page, its top corners rounded. With reduced motion the card fades in where it ends up.
- In the margin above the card, a small label with the page's site, so a page in a Peek can't pass
  for the one under it: the domain alone, without "www." (`kp.ru`), as the address pill shows it,
  with the full URL as its tooltip. It isn't a button, so a secure page gets no icon; a page the
  address bar would warn about leads with its warning icon and "Not secure" or "Dangerous".
- Round buttons in a column to the right of the card, from its top: **Close**, **Open as tab** (the
  card expands into the page area and becomes a today tab next to the page it came from — the page
  is *not* reloaded — and the visit goes into history) and **Open in split view** (the card moves
  into the right half and the page becomes a tab in a split view with the one under it; not offered
  when that one is already in a split view, or in a little window). Esc, ⌘W or a left click on the
  backdrop also close it. A page that asks before it is left ("Leave site?") asks first.
- Only one Peek per window. Shift+click inside it replaces its page. Other links in it that open a
  new tab or window open in the window, as from any tab (the popup blocker included); one that
  opens a tab in front first makes the Peek's page a tab, so the page that opened it stays. Showing
  another tab lets the Peek go.
- While a Peek is open, the page commands act on it: reload, stop, back, forward, zoom and Copy
  URL; find, print, view source, DevTools, the address (⌘L) and the like do nothing until it is a
  tab. The window's own commands (new tab, switch tab, ...) work as usual.
- A page in a Peek has the ad blocker, the popup blocker, passwords and its dialogs (alert,
  confirm), as in a tab. It can't ask for permissions (camera, location, notifications, ...),
  sign in to a server or open another app, and it can't go fullscreen: those are refused until it
  is opened as a tab.
- Not built yet: Option+click as a setting instead of Shift+click, and links that leave the site of
  a pinned tab always opening in Peek.

## 8. Little window

- ⌥⌘N opens the command bar (“Search or enter an address for a little window”); what you pick opens
  in a new little window. Also: a link's context menu **“Open Link in Little Window”**. A page's own
  popups (`window.open` with a size) are little windows too.
  (Later: links from other apps.)
- The command bar for a little window lists addresses, searches and history, not open tabs or
  commands: everything chosen there opens a page.
- A small window (its page ≈ 960 × 680, smaller on a small screen; each new one a step down and to
  the right of the last while it fits) with no sidebar or tabs: below the title bar, one row with
  the address, **Copy URL**, and **“Open in ‹space›”**, which moves the page into the window
  showing that space, as its active tab, without reloading it. The label follows the space's name.
- Several little windows can be open; ⌘W closes one.
- On macOS Chromium gives ⌥⌘N to "New Split Tab"; here a split view starts from a tab's or a link's
  menu.

## 9. Incognito

- Its own window, Cmd+Shift+N. Visually distinct: dark tint, “Incognito” label in the sidebar.
- No spaces in the sidebar — only today tabs, so it's obvious nothing is saved.

## 10. Mobile (phones)

Same model as desktop — profiles, spaces, pinned and today tabs, archive —
arranged for one thumb. Everything touched often sits in the bottom third.

```
┌──────────────────────────┐   ┌──────────────────────────┐   ┌──────────────────────────┐
│ 9:41               ▮▮ ◔  │   │ 9:41               ▮▮ ◔  │   │ 9:41               ▮▮ ◔  │
│                          │   │ 🕶  (🏠 Personal) 💼 Work ⋯ │   │                          │
│                          │   │ (W) (GH) (YT) (M)        │   │                          │
│          page            │   │ PINNED                   │   │  recent tabs / results   │
│                          │   │  ▢ Calendar           ✕  │   │  (best match lowest)     │
│                          │   │ TODAY                    │   │  ▢ Web browser — Wiki…   │
│                          │   │ ┌────────┐ ┌────────┐    │   │  ⌕ wiki — Search with …  │
│                          │   │ │preview │ │preview │    │   │                          │
├──────────────────────────┤   │ └────────┘ └────────┘    │   ├──────────────────────────┤
│ 🏠 [ 🔒 github.com ] ② ⋯ │   │            ＋        Done │   │ [⌕ wiki            ] Cancel│
└──────────────────────────┘   └──────────────────────────┘   └──────────────────────────┘
      page + bar                     all tabs                         search
```

**The bar** (bottom by default, top in Settings) — space icon · address pill (domain only) ·
tab count · ⋯ menu.
- Tap the address → **search**: full screen, the input sits right above the keyboard, results grow
  upward with the best match nearest the thumb. Empty input shows recent tabs.
- **Drag the address sideways** → previous / next tab in the space, pinned first, then today.
- **Drag the bar up** (or tap the tab count) → **all tabs**.
- Tap the space icon → **spaces sheet**: switch space, incognito, new space, space options
  (rename, icon, colour, **profile**).
- ⋯ → back / forward / reload / copy link, pin, move to space, new (incognito)
  tab, archive now, settings.
- While reading, scrolling down **hides the bar completely**; scrolling up, or tapping the
  bottom edge, brings it back.

**All tabs** — one page per space; drag sideways to move between spaces (the space names on top
also jump). Each page: pinned tabs (rows), today's tabs as
**cards with page previews** (rows in Settings). Hold a tab for pin / move / close.
Footer: ＋ (new tab) · Done — only tab actions here; new spaces come from the spaces sheet. The mask button switches to incognito tabs (dark tint, "Close all").
Previews are kept in memory only.

**Pages** — back / forward with the system edge swipe. Only a few pages stay loaded (memory);
the rest sleep and keep their preview.

**Peek** — on **iPhone**, holding a link shows the **system link preview** (UIContextMenu via
`WKUIDelegate`) with our actions: Open in New Tab (also: tap the preview), Open in Background, Copy Link, Share. On **Android** it opens our **Peek sheet**: a scrollable bottom
sheet; drag up or tap Open = open as tab (no reload), drag down = close. Auto-peek from pinned
tabs works as on desktop.

**Tablets** — at ~700 pt width and above (iPad, Android tablets, open foldables) the **desktop
sidebar layout** is used (section 2); narrower (Split View, Slide Over) uses the phone layout.

**Little window** — on Android, Custom Tabs for links from other apps (design.md §7.4); none on iOS.

## 11. Motion & responsiveness budget

| Thing | Budget |
|---|---|
| Hover / press feedback | Next frame |
| Command bar open | < 50 ms, 120 ms fade/scale |
| Tab switch (loaded tab) | First paint < 50 ms, no animation |
| Space switch | 180 ms slide |
| Peek open / expand | 200 / 240 ms |
| Sidebar show / hide | 180 ms |
| Easing | `cubic-bezier(.2, .8, .2, 1)` (fast out, soft landing); no animation > 300 ms |
| Reduced motion | Respect the OS setting: replace slides with fades |

Animate only transform and opacity, so animations run on the compositor and never wait for page
work.

## 12. First run

Two questions, once per profile, one after the other in dialogs over the first window; a window
opened meanwhile waits for them. "Not now" skips either one.

Before them, on the browser's very first run only (not for each new profile), and only if another
browser's data is on the Mac: **"Bring your tabs and bookmarks from ‹browser›?"** (the Mac's default
browser if it is one of them, otherwise the first one found). **Import…** opens the import dialog
(section 13) with that browser selected; **Not now** skips it. The two questions follow once the
dialog closes and the extensions it installs are in, so spaces and extensions brought from Arc or
Chrome are already there. A password manager installed by the import counts as installed. A
window opened meanwhile waits for all of it, as for the two questions.

1. **Search engine.** There's no default; the user picks one (design.md §5). Until they do, a
   search from the command bar brings the choice back.
2. **Password manager.** Little Browser has no password manager of its own (design.md §7.8), so
   it offers one as an extension: 1Password, Bitwarden or Proton Pass, in alphabetical order, each
   with one line about it. Picking one shows Chromium's install prompt with the extension's
   permissions, and it installs from the Chrome Web Store. If the store can't be reached, a
   message says so and the question comes back the next time a window opens. The question is
   skipped if one of the three is already installed and isn't asked again; any extension can be
   added from the store later.

Auto-archive's question comes after these (section 6).

## 13. Import from another browser

Opened from first run (section 12), the command bar ("Import from another browser"), the app
menu's "Import Bookmarks and Settings…" and Settings' "Import bookmarks and settings" (also the
address chrome://settings/importData), which all open the same dialog over the window. From a
private window it opens over a normal window of the same profile, because what is imported is
kept; guest windows import nothing.

**Sources** — only those found on the Mac are listed, the default browser first:

- **Chrome-family browsers**: Brave, Chrome, Chromium, Dia, Edge, Opera, Vivaldi. Each of their
  profiles is listed separately, under the name the browser gives it ("Chrome — Work").
- **Arc**, once, with all its profiles: its spaces span profiles, so they come together.
- **Firefox**, each profile.
- **Safari**, when Little Browser has Full Disk Access, which reading its bookmarks needs. Without
  it Safari isn't listed, and the dialog says how to export from Safari instead (File → Export
  Browsing Data to File).
- **A Safari export file…** (the `.zip` that export makes) and **a bookmarks file…** (HTML, which
  every browser can export). Both open a file picker.

**What comes along**

| | Chrome-family | Arc | Firefox | Safari | Safari export | Bookmarks file |
|---|---|---|---|---|---|---|
| Spaces and profiles | — | ✅ | — | — | — | — |
| Open tabs | ✅ | ✅ | — | — | — | — |
| Bookmarks | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| History | ✅ | ✅ | ✅ | — | ✅ | — |
| Search shortcuts | ✅ | ✅ | ✅ | — | — | ✅ |
| Addresses | ✅ | ✅ | — | — | — | — |
| Extensions | ✅ | ✅ | — | — | — | — |
| Cookies (stay signed in) | opt-in | opt-in | — | — | — | — |

Passwords and payment cards never come along, even when a source has them: passwords are the
password manager's job (design.md §7.8), and its own import takes them.

**The dialog**

1. **From:** the list of sources, the first picked. Picking one shows what it has as checkboxes,
   all ticked except **Stay signed in (cookies)**; for Arc, its spaces and tabs are one checkbox,
   **Spaces, profiles and tabs**. Under **Stay signed in (cookies)** a line says that macOS will
   ask to let Little Browser use "‹browser› Safe Storage" from the keychain.
2. **Import.** "Importing from ‹source›…", then a summary ("812 bookmarks, 12,400 pages of
   history, 3 spaces, 41 tabs"). Anything that couldn't be read is named with the reason, and the
   rest still imports.
3. **Extensions**, when ticked: the source's extensions from the Chrome Web Store, each with a
   checkbox, ticked (with the profile it goes into when there are several), and **Install** or
   **Skip**. Extensions already installed aren't listed, and each comes once per profile; ones
   not from the store are counted ("2 aren't from the Chrome Web Store"). Install goes through them
   one after another, each with Chromium's install prompt and its permissions. Their settings and
   data don't come along.

The other browser can stay open: Little Browser reads copies of its files. Firefox is the
exception, because it locks its profile, so Chromium's own prompt asks to close it first. If a
copy can't be read because the browser was writing it, the dialog asks to quit that browser and
try again.

**Where things go**

- **Arc.** Each Arc space becomes a space with its name, its emoji (a space with a symbol icon gets
  the next of our icons, as a new space does) and the nearest of our colours. Its pinned tabs become pinned tabs in the same order, and its
  pinned folders become pinned groups in place, with their names, starting in the group colour
  nearest the space's colour. A folder inside a folder adds its tabs to the outer folder's group,
  in place, and its own name is dropped; a folder with no web pages is left out. Its
  unpinned tabs become today tabs, and its Favourites become pinned tabs at the top of the first
  space of their profile. Arc's default profile maps to the default profile; each other Arc
  profile becomes a profile with the same name, or uses one that already has it (if the default
  profile was deleted, the current profile stands in for it). Each Arc profile's bookmarks,
  history, search shortcuts, addresses, extensions and cookies go into its matching profile.
  With **Spaces, profiles and tabs** unticked, no space or profile is made and every Arc
  profile's data goes into the current space's profile.
- **Open tabs from a Chrome-family browser** join the current space: pinned as pinned, the rest as
  today tabs after the ones already there, and tab groups stay tab groups. Everything else from it
  goes into the current space's profile, as do imports from every other source.
- **A space whose name is taken** gets a number ("Work 2"), except that an empty space with the
  same name and profile is filled instead. So Arc's "Personal" fills the empty first space at
  first run.
- **Imported tabs sleep** until clicked, like tabs after a restart (section 6).
- **Bookmarks** keep the source's tree: its bookmarks bar, other bookmarks and mobile bookmarks
  go into the same places here, not into an "Imported" folder. A folder with the same name in the
  same place is merged into, and a link already in that folder isn't added again, so importing
  twice adds only what is new. The command bar finds them (section 4).
- **History** keeps its visit times and counts. A page comes once, and only when it is new here
  or its last visit is newer than the one history has, so importing twice adds nothing; the
  summary counts the pages added. Only web pages (`http`, `https`) visited in the last 90 days
  come along, since history keeps nothing older.
- **Search shortcuts** are the site searches the user added in the other browser (keywords such as
  `w` for Wikipedia; in Firefox, bookmarks with a keyword; in a bookmarks file, those with a
  SHORTCUTURL), not its built-in engines, and the default search engine stays the user's own
  choice (section 12).
- **Cookies**, when ticked: the source's cookies that haven't expired, except partitioned ones.
  If macOS's keychain prompt is declined, everything else still imports and the summary says
  cookies were skipped.

Importing the same source again adds its spaces and tabs again; bookmarks and history merge as
described above.

Mobile has no import dialog: a bookmarks file can be imported from Settings, and everything else
comes from a desktop through sync (design.md §7.6).

## 14. What we leave out of Arc

Boosts, Easels, Notes, Library media view, AI features, accounts, Air Traffic Control (rule-based link
routing), right/bottom tabs. Candidate for later: link routing rules.
