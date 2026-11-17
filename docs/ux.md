# Little Browser — Screen layout & interaction

> Companion to [design.md](design.md). Base: **Arc's approach** (sidebar-first, content-first,
> keyboard-first), trimmed to our minimal feature set. How the desktop sidebar is built:
> [desktop-ui.md](desktop-ui.md).

**Built so far (macOS desktop):** the sidebar layout on upstream's vertical tabs, the command bar,
spaces and profiles (new-space dialog, switching by shortcut, swipe or footer, per-space tint, saved
tabs per space), favourites per profile, the first-run search-engine choice, the quit warning,
and Chromium's own tab groups. Everything else here — auto-archive, group suggestions and
auto-grouping, Peek, the little window, the distinct incognito look, top-mode additions and the
whole mobile section — is specified but not built.

---

## 1. Principles

1. **Content first.** The page fills almost the whole window. No toolbar row, no bookmarks bar, no
   status bar. All browser chrome lives in one sidebar (or one slim row in top mode).
2. **One place for everything.** Address, tabs, favourites and spaces are all in the sidebar.
3. **Keyboard first.** A command bar opens everything: new tab, search, switch tab, commands.
   Every action has a shortcut; nothing needs a mouse.
4. **Calm.** No badges, promos, pop-ups, "what's new" pages or permission nags. Nothing moves unless
   the user caused it.
5. **Fluent.** Every transition is animated, short (≤ 240 ms), interruptible, and spatially
   consistent: spaces sit left→right, so swiping and animations always move the same way.
6. **Forgiving.** Closing is cheap: Cmd+Shift+T reopens, archived tabs are searchable, pinned tabs
   never disappear.

---

## 2. Desktop window — sidebar mode (default)

```
┌───────────────────┬──────────────────────────────────────────────────┐
│ ● ● ●     ‹  ›  ⟳ │╭────────────────────────────────────────────────╮│
│ ╭───────────────╮ ││                                                ││
│ │ 🔒 github.com  │ ││                                                ││
│ ╰───────────────╯ ││                                                ││
│ ┌──┐┌──┐┌──┐┌──┐  ││                                                ││
│ │W ││GH││YT││M │  ││             page (rounded, inset)              ││
│ └──┘└──┘└──┘└──┘  ││                                                ││
│ ▸ Personal        ││                                                ││
│   📌 Calendar     ││                                                ││
│   📌 Mail         ││                                                ││
│ ───────────────── ││                                                ││
│   +  New tab      ││                                                ││
│   ◦ Hacker News   ││                                                ││
│   ◦ MDN: flexbox  ││                                                ││
│                   ││                                                ││
│ ⬇    ● ○ ○    ＋  │╰────────────────────────────────────────────────╯│
└───────────────────┴──────────────────────────────────────────────────┘
```

Sidebar zones, top to bottom. The address pill stays in the sidebar: the page gets the full
window height, and with the sidebar hidden the domain is one hover or Cmd+L away.

| Zone | Content | Notes |
|---|---|---|
| Title row | Window controls, back / forward / reload | Drag area for the window |
| Address pill | Domain only (`github.com`), lock/shield state, loading bar underneath | Click or Cmd+L → command bar with the full URL selected |
| Favourites | Icon grid, 4 per row, **per profile**: shared by all spaces that use the same profile | Each favourite keeps its own page; audio indicator on the tile; Cmd+1…9 |
| Space header | Name + icon of the current space; profile badge if it isn't the default profile | Space colour tints the sidebar |
| Pinned tabs | Per space, persistent | Wandered off the pinned URL → hover shows ↺ "back to pinned page" |
| Divider + New tab | | |
| Today tabs | Unpinned tabs, newest first | Optional auto-archive (section 6) |
| Footer | Downloads, space switcher (dots/icons), new space | |

- **Page area** is inset by 8 px with rounded corners and a soft shadow; this frames the page and
  makes the sidebar feel like part of the window, not a toolbar.
- **Hide the sidebar** with Cmd+S. The page then fills the window; moving the pointer to the left
  edge slides the sidebar in *over* the page, and it slides out when the pointer leaves.
- **Sidebar width** is draggable (200–400 px).
- Light/dark follows the OS; space colour is a subtle tint, never a saturated block.

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

- Favourites and pinned tabs shrink to icons; today tabs are chips with titles.
- Command bar, Peek, Little window and all shortcuts work the same as in sidebar mode. A sideways
  swipe on the tab row switches space.
- In the real browser this is Chromium's upstream horizontal tab strip and toolbar plus our space and
  favourites controls; no layout patches.

## 4. Command bar

The centre of the browser. A floating palette in the middle of the window.

| Shortcut | Opens as |
|---|---|
| Cmd+T | New tab: empty input |
| Cmd+L / click address pill | Edit current URL: full URL, selected |

- Results, in one list, ranked by fuzzy match: **open tabs** (switch, across all spaces) ·
  favourites · pinned · history · archived tabs · **commands** (“Toggle sidebar”, “New space”,
  “Tabs on top”, “Incognito window”, “Copy URL”…) · URL / “Search with ‹engine›”.
- Enter takes the top result; ↑/↓ moves; Esc closes and returns focus to the page.
- Opens in < 50 ms; it's a native/overlay view, not a page.
- Search suggestions from the engine appear only if the user turns them on (privacy default).

## 5. Spaces and profiles

- A **space** is a set of pinned and today tabs with a name, icon (emoji) and colour.
- Switch: Ctrl+1…9, Ctrl+Option+← / →, the footer dots, or a **two-finger sideways swipe on the sidebar**
  (on the tab row in top mode). The sidebar contents slide in the swipe direction; the page
  cross-fades to that space's last active tab. On the page itself the swipe stays **back /
  forward**, as in Chrome and Safari, and sideways-scrolling content keeps working.
- A **profile** is one set of logins, cookies, history and favourites. Every space uses a
  profile; several spaces can share one. Everyone starts with just the **default profile**, named
  **Personal** like its first space, so the concept stays out of sight until it's needed.
- Space menu (right-click its dot or header) → **Profile ▸** Personal / Work / … / New profile. A space
  outside the default profile shows a small 🔒 badge with the profile's name, so it's clear its logins
  are separate. Moving a space or tab to another profile reloads it in that profile's cookie jar.
- **New space** ("+" in the footer, or the command bar) opens a small dialog: **name**, **profile**
  (an existing one, preselected to the current space's, or **New profile…** with its name) and
  **theme** (colour and icon); **Cancel** or **Create**.
- Spaces are browser-wide. A Chromium window belongs to one profile, so switching to a space in
  another profile brings that profile's window up in the same place and hides the current one.
- Each space keeps its tabs across restarts: the browser continues where you left off by default,
  and every window comes back on its space.
- Tabs of inactive spaces go to sleep after a while (memory goal); sleeping tabs look dimmed and
  wake on click.
- Right-click a tab → “Move to space ▸”.

## 6. Tabs

- **Pinned** tabs are the space's permanent set. Cmd+W on a pinned tab puts it to sleep instead of
  deleting it. Cmd+D pins/unpins.
- **Today** tabs are everything else. **Auto-archive**: today tabs not used for a while are
  closed and moved to the archive (searchable in the command bar). This keeps the sidebar short and
  memory low. Choices: 12 h, **24 h (default)**, 7 days, 30 days, or Never. The first-run screen asks
  for it right after the search engine choice, with 24 h suggested; Settings can change it later.
- Drag to reorder, or drag between pinned and today.
- Middle-click or ✕ on hover closes. Speaker icon on tabs playing audio; click to mute.
- Ctrl+Tab / Ctrl+Shift+Tab cycle tabs in sidebar order.

### 6.1 Tab groups

Named, collapsible runs of tabs — Arc-style folders — inside pinned or today.

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
- Click the header to collapse / expand; double-click (or its ⋯ menu) to rename; the menu also has
  Ungroup and Close tabs in group. Tabs removed from a group are never auto-grouped again.
- Collapsed groups' tabs **sleep sooner**: after a third of the normal sleep time, at least
  1 minute.
- **Archive**: a group in today is archived **as a unit**, once none of its tabs has been used
  for the auto-archive time; the command bar finds it by name and restores the whole named group.
  Groups in pinned are never archived.
- Moving a tab to another space, pinning it alone, or making it a favourite takes it out of its
  group.
- **Phones**: groups sync. In the all-tabs view each group is a collapsible section (name,
  count) among today's cards; a tab's hold menu has *Add to New Group*, *Add to Group ▸* and
  *Remove from Group*. No suggestions or drag-to-group on phones; the "opened one after another"
  rule applies there too.

**Real browser** — built on **Chromium's own tab groups** (`TabGroupModel` / saved tab groups), so
extensions (`chrome.tabGroups`), session restore and sync of open tabs keep working; our sidebar and
suggestion logic sit on top. Each group gets one of Chromium's group colours, picked from its key.

## 7. Peek (link preview)

- **Shift+click** a link (configurable: Option/Alt+click). It replaces Chromium's "open in new
  window", which we hardly need; Alt+click stays "download link". Optional: links that leave the site of a
  favourite/pinned tab always open in Peek.
- The card grows from the click point to ~85% of the page area over a dimmed backdrop.
- Controls at the card's top-right: **Open as tab** (the card expands into the page area and becomes
  a today tab — the page is *not* reloaded), **Open in Little window**, **Close** (Esc or click the
  backdrop).
- Only one Peek at a time; opening another replaces it.

## 8. Little window

- Cmd+Alt+N, or a link's context menu “Open in Little window”. (Later: links from other apps.)
- A small window (≈ 960 × 680) with no sidebar or tabs: one header row with the domain pill, copy
  URL, and **“Open in ‹space›”**, which moves the page into the main window as a tab.
- Several little windows can be open; Cmd+W closes one.

## 9. Incognito

- Its own window, Cmd+Shift+N. Visually distinct: dark tint, “Incognito” label in the sidebar.
- No favourites or spaces in the sidebar — only today tabs, so it's obvious nothing is saved.

## 10. Mobile (phones)

Same model as desktop — profiles, spaces, favourites per profile, pinned and today tabs, archive —
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
  upward with the best match nearest the thumb. Empty input shows recent tabs and the profile's
  favourites.
- **Drag the address sideways** → previous / next tab in the space, pinned first, then today.
- **Drag the bar up** (or tap the tab count) → **all tabs**.
- Tap the space icon → **spaces sheet**: switch space, incognito, new space, space options
  (rename, icon, colour, **profile**).
- ⋯ → back / forward / reload / copy link, pin, add to favourites, move to space, new (incognito)
  tab, archive now, settings.
- While reading, scrolling down **hides the bar completely**; scrolling up, or tapping the
  bottom edge, brings it back.

**All tabs** — one page per space; drag sideways to move between spaces (the space names on top
also jump). Each page: the profile's favourites (icons), pinned tabs (rows), today's tabs as
**cards with page previews** (rows in Settings). Hold a tab for pin / favourite / move / close.
Footer: ＋ (new tab) · Done — only tab actions here; new spaces come from the spaces sheet. The mask button switches to incognito tabs (dark tint, "Close all").
Previews are kept in memory only.

**Pages** — back / forward with the system edge swipe. Only a few pages stay loaded (memory);
the rest sleep and keep their preview.

**Peek** — on **iPhone**, holding a link shows the **system link preview** (UIContextMenu via
`WKUIDelegate`) with our actions: Open in New Tab (also: tap the preview), Open in Background, Add
to Favourites, Copy Link, Share. On **Android** it opens our **Peek sheet**: a scrollable bottom
sheet; drag up or tap Open = open as tab (no reload), drag down = close. Auto-peek from pinned
tabs / favourites works as on desktop.

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

## 12. What we leave out of Arc

Boosts, Easels, Notes, Library media view, AI features, accounts, Air Traffic Control (rule-based link
routing), split view, right/bottom tabs. Candidates for later: split view, link routing rules.
