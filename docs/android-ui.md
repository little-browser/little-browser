# Android — the shell on Chromium's Android app

How the Android browser is built: what it shares with the desktop build, what is its own, and how
each feature the macOS build has comes to Android. The behaviour is in [ux.md](ux.md) (§10 for
phones; tablets use the desktop layout, §2–§6); the engine and privacy baseline are in
[design.md](design.md) §4.2 and §5.

**Built so far:** Chromium's own app (`chrome_public_apk`) with our app ID and privacy patches,
plus the shared tree: our C++ tree, the shared engine patches and the version pins in `chromium/`,
which the Android build links and applies ([android/README.md](../android/README.md)). On top of it,
engine parity: the ad blocker with element hiding and the per-site Ads switch (page info and Site
settings), filter lists read as for mobile, tracking parameters, the privacy pref defaults shared
with the desktop, the shared tab and password-manager rules in lb-core, and lb-core reached from
Java (`TabRules`), with Robolectric tests. `android-arm64` and `android-x64` (for Intel and AMD
Chromebooks) build it and run those tests, and on a
phone emulator the blocker blocks and hides ads and the Ads switch and setting work. The phone
shell, Peek, the little window, first run's search engine and password-manager questions and multi-profile
(steps 3 to 5 below) are built as far as those steps say. Everything else on this page is
specified, not built; the order of work is at the end.

## Approach

The Android build is the same Chromium fork as desktop, so it shares everything below the UI. Our
Android UI is **Java code in our own tree, on top of Chromium's own Android models and views**,
hooked in by thin patches, the way the desktop sidebar sits on upstream's vertical tabs. We don't
fork Chromium's Android UI: we replace the toolbar and tab switcher on phones with our own views and
reuse what Chromium already has (tab model, tab groups, pinned tabs, vertical tabs, the
link-preview sheet, Custom Tabs, page info, settings). These are all in Chromium 157's Android
app, the version `chromium/VERSIONS` pins.

### What is shared, and where

Every platform is three layers. A rule goes in the lowest layer whose types it needs:

| Layer | What | Platforms | Where |
|---|---|---|---|
| **lb-core** (Rust) | Rules and data on plain types: spaces, tab order, pinned close, tab names and links, pinned groups, the command bar's ranking, filter lists, element hiding, tracking parameters, the search and password-manager choices, import readers, sync | All five | `core/lb-core` |
| **Chromium layer** (C++) | Features on Chromium's engine types: the ad blocker (subresource filter, list downloads), element hiding (renderer agent, Mojo host), tracking-parameter stripping (navigation), pref defaults, saved space tabs, the C++ binding of lb-core (`core_bridge`), engine patches | macOS, Windows, Linux, Android | `chromium/lb` (`src/lb`), `chromium/patches` |
| **Shell** | What the user sees and touches | Each its own | Views: `chromium/lb/browser/ui/views` (macOS, Windows, Linux); Android: `chromium/lb/android`; iOS: `ios/` (Swift) |

- **lb-core on Android** is reached through the same `cxx` bridge as on desktop
  (`chromium/lb/core_bridge`), then from Java through Chromium's JNI generator (`jni_zero`): a
  small C++ class per area with `@CalledByNative`/`@NativeMethods` glue in `chromium/lb/android`.
  No UniFFI, no JNA and no second Rust toolchain inside the Chromium build: Chromium's Rust
  toolchain already builds `lb_core` for Android. UniFFI stays for iOS, which isn't Chromium.
- **Rules move down, not sideways.** A rule that the desktop keeps in `//lb:model` and Android
  needs too moves into lb-core (plain types) before Android uses it; the desktop then calls it
  there. In lb-core: pinned close and the tab to show, tab restore order, tab names and pinned
  links (clean a name, the title to show, settle a pinned link, the name menu)
  (`core/lb-core/src/tabs.rs`); pinned groups (normalise, group after a move, restored group,
  closing a group, its rows and menu) (`pinned_groups.rs`); and the password-manager list, with
  each manager's extension id and Android package, and when first run offers it
  (`password_manager.rs`). The desktop's `tabs/` and `password/` files keep only Chromium's types
  around them. What stays in `//lb:model`: rules on desktop-only types (`sidebar_width`,
  `swipe_tracker`, `little_window_rules`, `peek_rules`, command matching over Chromium's omnibox
  types, pinned-group drag and drop).
- **Engine patches are shared.** A patch in `chromium/patches` (engine layer, untagged) applies to
  every Chromium platform. The desktop tree is Chromium plus ungoogled-chromium's patches,
  Android's is Chromium alone, so the shared series is checked on both: `check_patches.py` on the
  desktop's, `android/scripts/fetch.sh --patches-only` on Android's. Where the bases differ, the
  patch is two entries, the desktop's tagged `[desktop]` and its twin `[android]` (none is needed
  today). A hook that only the desktop needs sits
  in `#if !BUILDFLAG(IS_ANDROID)` in the shared patch (`sidebar-prefs.patch`). View patches (Views,
  Cocoa, WebUI) stay in `desktop/patches`; Android's Java hooks and Android-only privacy patches
  stay in `android/patches`, Java and native apart (`check_series.py`).
- **One state format.** Spaces live in Local State `lb.spaces`, their tabs in `lb.space_tabs`, in
  the desktop's format, read and written by the same C++ (`chromium/lb/browser/spaces`). Sync
  (design.md §8) later moves the same records between devices.

### Repository layout

```
chromium/                  shared by every Chromium platform
  VERSIONS                 Chromium (and ungoogled-chromium) pins
  lb/                      our C++ and Java, linked as src/lb
    browser/               engine features and //lb:model (no views)
    browser/ui/views/      the desktop shell (Views)
    android/java/src/app/littlebrowser/lb/…   the Android shell
    android/junit/…        its Robolectric tests
    common/, renderer/, core_bridge/
  patches/                 engine patches every platform applies, with their series
  scripts/                 lib.sh, check_series.py, check_patches.py, check_lb_tests.py, their tests
desktop/                   desktop only: platforms/, args/, view and macOS patches, packaging
android/                   Android only: args/, Java hook and privacy patches, fetch/build
core/                      lb-core
```

`android/scripts/fetch.sh` links `src/lb` → `chromium/lb` and `src/lb_core` → `core/lb-core`, as
the desktop `fetch.sh` does, and applies `chromium/patches/series` before `android/patches/series`.
Every Chromium platform builds `//lb:engine` (view-free engine logic), `//lb:prefs` (the prefs
every platform keeps), `//lb:browser` (browser-process code, which `chrome/browser` depends on),
`//lb:renderer`, `//lb:mojom`, `//lb:page_names`, `lb_core` and `core_bridge`. `//lb:model` and
`lb_unittests` are desktop-only (`!is_android`); the Views code is built by the desktop's view
patches. The Android shell is `//lb/android`: its Java (`:java`, which Chromium's Java depends on
through `android/patches`), whose native side `//lb:browser` adds on Android
(`chromium/lb/android/*/*_android.cc`), and `lb_junit_tests`.

## Features, macOS → Android

Every feature the macOS build has, and how Android gets it. "Reuse" names what is shared or what
Chromium already has.

| macOS feature | On Android | Reuse | Android's own |
|---|---|---|---|
| Sidebar: space header, pinned and today tabs, "+ New Tab", footer with spaces | Phones: the bar, all-tabs view and spaces sheet (ux.md §10). Tablets (≥ 700 dp): the sidebar, on Chromium's Android vertical tabs (`AndroidVerticalTabs`) | lb-core spaces and tab order; Chromium's `TabModel` (which holds tab groups too) and its phone toolbar, at the bottom by default (`bar-at-bottom.patch`), its address showing the domain only (`address-domain-only.patch`), its home button the space button; on tablets `VerticalTabListCoordinator` | `LbPhoneShell` (`app.littlebrowser.lb.shell`, on phones and tablets), made by the tabbed activity's UI coordinator (`phone-shell.patch`); `AllTabsView`, `SpacesSheet`; on tablets `SidebarSpaces` in the vertical tab rail's header and footer (`vertical-tabs-sidebar.patch`) |
| Spaces: new-space dialog, switch, tint, saved tabs per space | Spaces sheet, and tapping a tab in another space's page of all tabs; the tint as a disc behind the space button on the bar and a wash on each space's page of all tabs | lb-core `spaces.rs` (defaults, colours, `space_tint`, which the desktop's tint uses too); the shared store (`space_store.*` in `//lb:engine`: `lb.spaces`, `lb.space_tabs`) through `SpaceStore` | Switching saves the shown space's tabs and closes them, and makes the target's saved tabs, each unloaded until shown, as after a restart (a phone keeps few pages loaded); back history isn't kept across a switch. Names and links Chromium's tab state doesn't keep are matched back to its restored tabs from the saved list (`SpaceTabs`) |
| Profiles per space | Spaces sheet → space options → Profile (or New profile…), and the new-space dialog's profile, as on desktop; a window per profile | The shared space store's profile field; Chromium's C++ `ProfileManager` and its attributes storage, through `Profiles` | `ProfileSpaces` (which space a window shows, when a switch takes another window); `profile-windows.patch`; `profile-prefs-own.patch` so that a profile created after startup loads its own prefs and makes its own database provider (Android's startup holds only the initial profile's), `gcm-driver-per-profile.patch` so that it gets a push-messaging driver of its own (web push reaches only the profile the app starts with: Android's push messages come to one driver), `post-profile-init-once.patch` so that it gets only the per-profile part of Chromium's post-profile setup, and `last-used-profile-loaded.patch` so that Chromium's "last used profile" never loads one synchronously. Multi-profile on Android (below) |
| Command bar (⌘T, ⌘L) | Search screen: tap the address; the bar stays at the bottom and results grow upward from the input (`suggestions-grow-upward.patch`). Tabs come first, nearest the input: with nothing typed the recent tabs (this space's, then other spaces'), with text the tabs it finds by name, page title or address, best first. Not there yet: the desktop's archived tabs and commands (New space, Incognito window, ...) | Chromium's omnibox and its suggestions (history, bookmarks, `TemplateURLService`); lb-core `command_bar.rs` matching and open-tab boost through the shared `command_matching.*` (`Matching`) | `SearchTabs` picks the tabs, `TabSuggestions` hands them to the omnibox, which draws them as basic rows (`search-tabs.patch`) |
| Pinned tabs as list rows, drag to pin and unpin | Pinned rows at the top of each space's page; the hold menu and the ⋯ menu pin and unpin | Chromium's Android pinned tabs (`TabModel.pinTab`, and `PinnedTabStripCoordinator` on tablets) | Rows in the all-tabs view (`AllTabsPage`) |
| Closing a pinned tab sleeps it, and takes it back to its link | Same: any close of one tab by the user (the menu, the hold menu) | lb-core's `decide_pinned_close`, `choose_tab_to_show` (`TabRules`), `after_pinned_sleep`, `on_tab_shown` (`TabNames`) | `PinnedClose`, called from Chromium's tab remover (`phone-shell.patch`); a sleeping tab is discarded and reloads when shown |
| Tab names, pinned links (Rename, Edit Link, Use Current Page as Link), a pinned row keeping its link's title | Hold menu; Rename and Edit Link ask in a text dialog | lb-core name and link rules through the shared `tab_names.*` (`TabNames`) | Kept with the space's saved tabs (`lb.space_tabs`), written as tabs change, and matched back to Chromium's restored tabs at startup |
| Tab groups | Collapsible sections among today's cards; hold menu: Add to New Group, Add to Group ▸, Remove from Group (ux.md §6.1) | Chromium's tab groups (`TabModel`) | Section headers in the all-tabs view |
| Pinned groups | A pinned group among the pinned rows | lb-core pinned-group rules through the shared `pinned_groups.*` (`PinnedGroups`); the saved tab's group fields | Group rows; saved with the space's tabs like names |
| Split view | Tablets only: Android's own two windows side by side ("Open in new window"); not on phones | Chromium's Android multi-instance | — |
| Hidden sidebar, sidebar width | Tablets: Chromium's vertical-tab collapse and resize; phones: the bar hides while scrolling | Chromium | — |
| Hidden sidebar's edge zone (pointer just left of the window slides it in) | ❌ No pointer outside the window on Android; a tablet's collapsed vertical tabs open with a tap or a swipe from the edge | — | — |
| Load progress line along the top of the page | Not built yet: Android shows Chromium's own toolbar progress bar. To come: the same line along the bar's page edge (top or bottom, wherever the bar is) | `chromium/lb/browser/tabs/load_progress.*` (its state: grows, never shrinks during a load, fills and fades), which moves to `//lb:engine` with it | The line view, replacing Chromium's toolbar progress bar |
| Peek (Shift+click); Esc closes it | Long-press a link → Preview page: the Peek sheet, its header naming the site; its open-in-tab button = open as tab without reloading, drag down or Back = close | Chromium's link-preview sheet (`EphemeralTabCoordinator`) for the sheet, its page, its header and the link menu's Preview page item | Its "open in new tab" loaded the address again in a new tab; `peek-open-as-tab.patch` makes it hand its `WebContents` to the new tab instead (`createTabWithWebContents`, as Custom Tabs' reparenting does), and `peek-open-as-tab-native.patch` keeps the tab from typing the page for the performance manager a second time |
| Little window | A web link from another app opens in a Custom Tab of its own (the browser's own links and pages still open as tabs); its toolbar has Copy URL and "Open in ‹space›", with the space's icon, which moves the page to the browser's window on the space it shows, without reloading, whichever browser is the device's default. The page is in the default profile, as a Custom Tab's is, so the space is one of that profile's, the one its window shows, and the page moves into that profile's window (a link that starts the app opens its little window before the browser has started, and the button then says "Open in Little Browser") | Chromium's Custom Tabs (`CustomTabActivity`), whose "Open in browser" already moves the tab without reloading (tab reparenting) | `LittleWindow` (which links, the two buttons); `little-window.patch` marks the link in Chromium's launcher and adds the buttons to the toolbar |
| First run: search engine | As on desktop: lb-core's engines in its order, asked once per profile the first time one of its windows opens (a new profile's too), before the password-manager question; a second window of the profile waits for the first. A full screen with no "Not now": Chromium's own default engine stands in until a choice, so the profile counts as asked only once an engine is picked, and a window that opens before then asks again | lb-core `search_choice.rs`; setting the engine with the desktop dialog's code (`lb/browser/search/search_engine_choice`), through `SearchEngines`; the shared `lb.search_choice_shown` pref | `SearchEngineScreen` |
| First run: password manager | Offers 1Password, Bitwarden, Proton Pass as the autofill service, once the window has focus after Chromium's first-run screens and the search engine question: opens the app's Play Store page if missing, then, back with it installed, Android's autofill picker; Chromium's "Autofill using another service" is set, and takes effect from the next start, as when it is set in Settings | lb-core password-manager list (Android package per manager) and rule, through `PasswordManagers`; the shared `lb.password_manager_choice_shown` pref | `PasswordManagerScreen`; `password-manager-queries.patch` lets it see whether they are installed |
| Ad blocker, element hiding, per-site Ads switch | Same engine; filter lists' `!#if` conditions read as for mobile (`env_mobile`); the switch in page info → Cookies and site data, under third-party cookies, and in Site settings → Ads, on every page with an Ads setting (web pages). As with Android's cookie switch, a change applies when page info closes, in one reload with the cookie switch's | Shared C++ (`adblock/`, renderer agent, Mojo host) and engine patches; `SiteAds` over the shared `ad_blocker.h` and `site_ads.h` | `SiteAdsSwitch` (what the switch shows and does, Robolectric-tested); Java patches adding it to page info and showing every site's Ads setting, without Chrome's intrusive-ads banner; the settings wording (`android/patches/ads-*.patch`) |
| Tracking-parameter stripping | Same | Shared engine patch and C++ | — |
| Safe Browsing (standard) | Not checked yet: Chromium on Android checks only through Google Play services. The engine is standard-only on every platform (`chromium/patches/safe-browsing-standard-only.patch`), and the settings offer only that: no Enhanced protection option in Settings or Privacy Guide, and no extended-reporting switch | The shared engine patch | `safe-browsing-standard-only-settings.patch` (Java) | A local-database path, once the Safe Browsing agreement is in place (desktop's database is empty until then too) |
| Privacy defaults (pref defaults) | Same values | `chromium/lb/browser/prefs/pref_defaults.*`, set on every platform after Chromium registers its prefs | `privacy-default-prefs.patch`: what those defaults can't set, Google sign-in (registered later, by a keyed service) and Touch to Search (Android only) |
| No reopening closed tabs | Recent tabs has no Recently closed list (without sync it lists nothing, and the page is empty), closing has no Undo bar (closing is committed at once), and Ctrl+Shift+T does nothing | — | `no-reopen-closed.patch` |
| Import from other browsers | A bookmarks HTML file (⋯ menu → Import bookmarks…), picked with Android's file picker: its bookmarks merge into the window's profile as every import's do, and its keyword searches become search shortcuts; later, sync from a desktop | lb-core's bookmarks-HTML reader (`bookmarks_html.rs`, iOS reads it too) and bookmark merge; the shared `bookmark_merge.*` in `//lb:browser`, which the desktop's importers use too | `ImportBookmarks` (the picker, the summary) over `BookmarksFile` |
| Extensions (Chrome Web Store) | None on phones (design.md §3) | — | — |
| Updater (Sparkle) | The app store's updates | — | — |
| Quit warning, data folder, keychain name, Dock and menu items | Not on Android | — | — |
| Incognito | Chromium's incognito tabs, shown in the all-tabs view's mask page | Chromium | Dark tint, "Close all" |
| Branding | App name, and Chromium's strings, which also say "Chrome"; the app icon and the logos in the app | `chromium/branding/BRANDING` and `chromium/scripts/rebrand_strings.py`; `branding/exports/android/` | `fetch.sh` renames "Chrome" too; `app-name.patch`; the icon and logos: `brand_assets.sh`, `app-icon.patch`, `logo-dark.patch` |

### Multi-profile

Chromium's C++ `ProfileManager` already handles many profiles, and names them in its
`ProfileAttributesStorage` on Android as on the desktop; Chromium on Android starts one regular
profile, and much of its Java asks for "the last used regular profile". We build on both:

- **A profile has its own window.** As on desktop, where a window belongs to one profile, each
  profile's tabs live in their own `ChromeTabbedActivity` instance. The window's intent names its
  profile's directory (`Profiles.WINDOW_EXTRA`, "Default" unless it says; a window Android
  recreates gets its intent back), and its profile provider loads and gives that profile
  (`profile-windows.patch`), so its tab model, incognito and pages belong to it.
- **The window in front is the last used profile.** When a window gains focus it records its
  profile as Local State's `profile.last_used`, as the desktop does for the browser window in
  front, so what Chromium's Java and C++ ask of "the last used profile" (settings, downloads,
  history, bookmarks, the new tab page) follows the window the user is in, without changing
  each caller.
- **Switching to a space in another profile** brings that profile's open window to the front on
  the space, or opens one through Chromium's multi-window launch, as desktop brings up the other
  profile's window. A window shows only its profile's spaces; the spaces sheet lists them all.
- **New profile** (from the new-space dialog, or a space's options → Profile → New profile…) is
  created through the C++ `ProfileManager` in its own "Profile <n>" folder and named in its
  attributes storage; the default profile stays Chromium's, called Personal. Moving a space to
  another profile (space options → Profile) shows it in that profile's window, and the window
  that showed it shows another of its own spaces.
- Incognito stays one off-the-record profile per regular profile, as on desktop.
- **A closed window comes back.** Each profile remembers its Chromium window
  (`ProfileWindows`), and opening the profile again reopens that window, so Chromium restores
  its tabs; a window that has none of its space's tabs (a new one, or one whose state is gone)
  opens the space's saved tabs instead of saving over them.
- **One entry in Recents.** The window in front is listed; the other profiles' windows are
  hidden there, and come back through the spaces.

## Testing

The same rule as desktop (AGENTS.md): every behaviour comes with tests that fail if it breaks.

- **Rules**: lb-core's `cargo test` and coverage gate; moving a rule there brings its tests.
- **Shared C++**: `lb_unittests` and `lb_core_unittests` run in the desktop builds and test
  `:engine` and `:prefs` too. `lb_unittests` is desktop-only for now (it also tests `:model`); it
  comes to the Android build, run on an emulator, once the CI runner has one.
- **Java**: logic in the Android shell (what a hold-menu item does, which tab the bar's swipe goes
  to) sits in plain Java classes over small state objects, the Java twin of `//lb:model`, tested
  with Robolectric JUnit on the build host (`lb_junit_tests`, Chromium's `robolectric_binary`,
  which `android/scripts/build.sh` builds and runs); no emulator needed. `check_lb_tests.py`
  requires a `<Name>Test.java` for every class in `chromium/lb/android/java/src`, listed in
  `lb_junit_tests`. A bridge class's test replaces its native methods with a fake
  (`<Name>Jni.setInstanceForTesting`) and checks what crosses the bridge; the rules behind it are
  tested in lb-core or `lb_unittests`.
- **UI**: the desktop's demo switches carry over. Debuggable builds read Chromium's command line
  from `/data/local/tmp/chrome-command-line`, so `--lb-sidebar-demo=<mode>` drives a state and its
  log says whether it passed; `adb exec-out screencap -p` captures it. Instrumentation tests on an
  emulator follow when CI has one.

## Order of work

Each step ends with its tests and docs, and is usable on a device.

1. **Shared tree.** In place: `chromium/` holds our C++ tree, the shared engine patches, the
   version pins and the shared scripts; the Android build links the tree, applies the shared
   series and builds `//lb`'s targets for every platform.
2. **Engine parity.** In place: the ad blocker with element hiding and the per-site Ads switch,
   filter lists read as for mobile, tracking parameters, shared pref defaults; lb-core reached from
   Java; the shared tab and password-manager rules in lb-core; Safe Browsing settings that offer
   standard protection only.
3. **Spaces and the phone shell.** In place: spaces and saved tabs per space with their tint, the
   bar at the bottom with the space button and the domain only, the search screen growing upward
   with recent and matching tabs first, the all-tabs view with pinned rows, cards with page
   previews and group sections (the Settings choice of rows instead of cards isn't built yet), pinned close, tab names and links, groups and pinned groups, the
   ⋯ menu's Pin and Move to Space, the spaces sheet and the new-space dialog. The ⋯ menu's
   Archive Now comes with auto-archive, which no platform has yet. The cards' previews are kept
   in memory only (`TabPreviews`), and Chromium's thumbnail cache writes none to disk
   (`no-thumbnails-on-disk.patch`).
4. **Peek, little window, first run.** In place: the Peek sheet opening as a tab without
   reloading, the little window for links from other apps with its two toolbar actions, the
   search engine and password-manager questions, no reopening.
5. **Multi-profile.** In place: a profile per space, a window per profile, new profiles, moving
   a space to another profile, reopening a profile's window with its tabs, one Recents entry
   (Multi-profile above).
6. **Tablets.** In place: Chromium's vertical tabs on by default (vertical-tabs-on.patch,
   vertical-tabs-sidebar.patch), with the shown space in the rail's header (tap: the spaces
   sheet) and every space in its footer (`SidebarSpaces`), and spaces working as on phones.
7. **Import.** In place: a bookmarks HTML file through lb-core, from the ⋯ menu.
8. **Release.** Our signing key (Play App Signing), the Play listing, the APK on
   downloads.littlebrowser.app, and the `android-arm64` and `android-x64` CI jobs on every merge request.

Chromium builds Android only on Linux x64, so every Android build, local or CI, runs on a
Linux host (android/README.md).
