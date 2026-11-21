# Little Browser — Design

---

## 1. TL;DR

- **Desktop (macOS, Windows, Linux):** a Chromium fork built the "patch set on top of upstream" way
  (like ungoogled-chromium / Brave), with a C++ UI in Chromium's own Views toolkit. This is the only
  approach that gives us full Chrome-extension support, DevTools, autofill, PiP and codec/DRM support
  without rebuilding them.
- **iOS:** **cannot be Chromium.** Apple requires WebKit worldwide; non-WebKit engines are only
  allowed in EU- and Japan-specific builds, and no major vendor has shipped Blink on iOS. So iOS is a
  native Swift app on `WKWebView`.
- **Android:** a Chromium fork, built **after** desktop and iOS.
- **Shared code:** a small **Rust core** (data model, sync, importers, settings) bound to C++
  (desktop), Swift (iOS) and Kotlin (Android). This gives us one sync engine across three very
  different shells. Content blocking uses each engine's own blocker with the same filter lists:
  Chromium's subresource filter on desktop and Android, WebKit content rules on iOS.
- **Privacy:** block third-party cookies, a **built-in native ad/tracker blocker** (not an extension:
  Manifest V2 is now gone from Chrome, and extensions don't exist on mobile), tracking-parameter
  stripping, no telemetry, no Google services, HTTPS-First.
- **Sync without storing user data:** local-first, **end-to-end encrypted**, with two transports:
  (1) direct device-to-device and (2) the user's own cloud storage (iCloud / Google Drive / Dropbox /
  WebDAV / any synced folder). We run no sync server. No accounts, no emails, no passwords: devices
  pair by QR code.
- **Screen layout & interaction** follow Arc's sidebar-first approach — see [ux.md](ux.md); how the
  desktop sidebar is built is in [desktop-ui.md](desktop-ui.md).
- **Open source** under MPL-2.0 (section 13).
- **Principle for staying small:** prefer *build flag off* → *feature/pref off by default* → *hide in
  UI* → *patch code out*, in that order. Every line we patch must be rebased every 4 weeks, forever.

---

## 2. Goals

1. Minimal, fast, low-memory browser on all five platforms.
2. Private by default: no tracking, no telemetry, no vendor account.
3. A small set of distinctive features (spaces, peek preview, little window, flexible tabs, media).
4. Minimal server footprint: **we never hold readable user data.**

---

## 3. Platform reality

| Platform | Engine | Shell / UI | Extensions | Notes |
|---|---|---|---|---|
| macOS | Chromium (Blink/V8) | Chromium Views (C++) | Chrome Web Store, MV3 | Sign + notarize; Sparkle-style updates |
| Windows | Chromium | Chromium Views | Chrome Web Store, MV3 | Code signing; own updater |
| Linux | Chromium | Chromium Views | Chrome Web Store, MV3 | Flatpak + .deb/.rpm repos |
| Android | Chromium fork | Java/Kotlin | None | Play Store + F-Droid/APK |
| iOS / iPadOS | **WebKit (WKWebView)** — required | Swift (UIKit/SwiftUI) | None (third-party browsers can't load Safari extensions) | App Store |

**iOS detail.** Since iOS 17.4 Apple allows other engines (via BrowserEngineKit and a special
entitlement) **only for apps distributed in the EU**, and since iOS 26.2 also in Japan. Those builds
must be region-specific and the engine must take security updates within tight deadlines. Nobody has
shipped Blink on iOS so far. We should not attempt it; WebKit gives us good privacy (Intelligent
Tracking Prevention) and native content-blocking rules anyway.

**Consequence:** "Chromium browser" is true on four platforms. On iOS we are a WebKit browser with the
same UI concepts, the same Rust core and the same sync.

---

## 4. Architecture

```
┌─────────────────────────── Little Browser ───────────────────────────┐
│                                                                       │
│  Desktop shell (C++)        Android shell (Java/Kotlin)   iOS (Swift) │
│  Chromium fork + Views UI   Chromium fork (or WebView)    WKWebView   │
│        │                            │                         │       │
│        └──────────── FFI (cxx / UniFFI / JNI) ────────────────┘       │
│                                 │                                     │
│                     ┌───────────┴───────────┐                         │
│                     │    lb-core (Rust)      │                        │
│                     ├────────────────────────┤                        │
│                     │ data model: spaces,    │                        │
│                     │  profiles,             │                        │
│                     │  tabs,                 │                        │
│                     │  settings, addresses   │                        │
│                     │ sync engine (CRDT+E2EE)│                        │
│                     │ transports: P2P, BYO   │                        │
│                     │  storage, file export  │                        │
│                     │ filter lists →         │                        │
│                     │  WKContentRuleList     │                        │
│                     │  (iOS content          │                        │
│                     │  blocker)              │                        │
│                     │ importers (bookmarks   │                        │
│                     │  HTML, Firefox, Safari,│                        │
│                     │  Chrome profiles)      │                        │
│                     └────────────────────────┘                        │
└───────────────────────────────────────────────────────────────────────┘
```

### 4.1 Desktop: how we fork Chromium

**How the fork is structured:**
- Base: upstream Chromium **stable** + the **ungoogled-chromium** patch set (BSD-3) as an upstream
  dependency. That patch set already removes Google sign-in, sync, telemetry, field-trial downloads and
  most background calls to Google. It is maintained by a community, so we inherit their rebases.
- Exceptions to the base patch set: ungoogled-chromium removes Safe Browsing and cuts extensions
  off from the Chrome Web Store; we **keep** Safe Browsing in standard mode and the store (install
  and update), so we skip or revert those patches (`desktop/README.md`).
- Our own code is licensed **MPL-2.0**, which lets us reuse Brave and adblock-rust code and is
  safe for App Store distribution.
- Our code lives in a **separate source tree** (`lb/`) that hooks into Chromium through as few
  and as small patches as possible (Brave's `brave-core` model: override files + thin patches).
- Release cadence: follow Chromium stable (new major every 4 weeks); ship security point releases
  within **days** of upstream, because Chromium security fixes become public in the source tree.

**UI technology:** Chromium Views (C++) for the window frame, sidebar, tab strip, peek overlay and
little window. It's cross-platform and much lighter than a web-based UI (Vivaldi-style). Settings and
other rarely used pages are WebUI (HTML), as in Chrome. The sidebar sits on upstream's vertical tab
strip with a slim Chromium toolbar above the page card; see [desktop-ui.md](desktop-ui.md).

**User data** lives in its own folder, never Chromium's: on macOS
`~/Library/Application Support/Little Browser` (the admin initial-preferences file and the system
NativeMessagingHosts folder move with it). Windows and Linux follow with their builds.

### 4.2 Android

Android is a Chromium fork (the Brave/Cromite model), built after desktop and iOS. It shares the
engine and patches with desktop, gives full cookie and network control, runs the native blocker in
the network stack, and already has incognito, DevTools over USB, PiP and media notifications. The
cost is a large build and a reskin of Chrome's Android UI, which is a lot of Java. There is no
interim System WebView app: a WebView app can't keep the privacy promise (request blocking only
through `shouldInterceptRequest`, no cosmetic filtering at engine level, weaker incognito
isolation).

### 4.3 iOS

Swift app, `WKWebView`, UIKit for the browser chrome (SwiftUI for settings). Key building blocks:
`WKContentRuleList` (compiled from the same filter lists by the Rust core), `WKWebsiteDataStore`
(persistent per profile / `nonPersistent()` for incognito), `WKUserScript` for tracking-parameter
stripping and cosmetic tweaks, `isInspectable` for Safari Web Inspector.

### 4.4 Rust core

- Exposed through **UniFFI** (Swift/Kotlin) and **cxx** (C++). This is the same pattern Mozilla uses
  for Firefox's shared components.
- Content blocking: desktop (and Android) use Chromium's own **subresource filter**, fed the
  filter lists the browser downloads and parses itself (`desktop/lb/browser/adblock`); it blocks
  network requests, not page elements. On iOS the core will convert the same lists into WebKit
  content-blocker JSON. Brave's **adblock-rust** (MPL-2.0) is the candidate for element hiding,
  which Chromium's filter doesn't do.
- Storage: SQLite per profile.
- On desktop the core is not the source of truth for Chromium's own models (bookmarks, prefs). We
  bridge: Chromium `BookmarkModel` / prefs ⇄ core via observers, and the core does sync.

---

## 5. Privacy and security baseline

| Area | Default | Notes |
|---|---|---|
| Third-party cookies | **Blocked** | Plus Chromium's storage partitioning. Google abandoned its own plan to deprecate them, so this is on us. |
| Ads & trackers | **Native blocker on** | EasyList, EasyPrivacy, uBlock filters and Peter Lowe's list, downloaded from their maintainers over HTTPS every 4 days (they aren't signed). Desktop: Chromium's subresource filter blocks matching requests on every site; element hiding isn't built yet. Ads can be allowed per site: the site-info panel's "Cookies and site data" has an Ads switch, the command bar has "Allow ads on this site", and Settings → Site settings → Ads lists those sites. No per-site record of the blocking is kept. |
| Tracking parameters | Stripped | `utm_*`, `fbclid`, `gclid`, `mc_eid`, … — rule list updatable. Bounce-tracking mitigations on. |
| Fingerprinting | Reduce | Reduced user-agent (upstream), block known fingerprinting scripts via lists. Brave-style randomisation ("farbling") is a later, separate project. |
| HTTPS | HTTPS-First mode | Warn before HTTP. |
| DNS | System resolver, with DNS-over-HTTPS upgrade when available | User can pick a provider. |
| Telemetry | **None** | No UMA/UKM, no field trials from Google. Crash reporting is opt-in. |
| Google services | Removed | Sign-in, sync, Lens, on-device AI models, Google-hosted suggestions, promos. |
| Search | **No default — choice screen on first run** | Offer privacy-respecting engines first (e.g. DuckDuckGo, Brave Search, Startpage), plus the mainstream ones. Search **suggestions off by default** (each keystroke would go to the provider). |
| Safe Browsing | **Standard mode on** | Local database of hash prefixes; only a short hash prefix is sent to Google on a match, never the URL. "Enhanced" (real-time) mode removed. The threat lists need an agreement with Google; until it is in place the database stays empty ([legal/safe-browsing.md](legal/safe-browsing.md)). |
| Site isolation | **On** | Costs memory but is our main defence against Spectre-class attacks. Not negotiable. |
| Extensions | Chrome Web Store, **MV3 only**, updated without pings | Install from the store's pages and update automatically from it, as in Chrome. An update check sends, per extension: its id and version, whether it is enabled (and why not), how it was installed, its install date and store cohort, and hashes of update files the browser already has; and once per check: the browser version, OS and its version, architecture, language and whether the computer is in a managed domain. No hardware details, no active-user or roll-call pings, no install ids, no install/update/uninstall events. Extensions not from the store that name no update server are checked against the store too, as in Chrome. The store page gets no referrer chain (the pages that led to it); as in Chrome, it can see which extensions are installed and turn them on or off (`chrome.management`), and extensions can't see or block its requests. Chrome removed the last Manifest V2 switches in mid-2026. Keeping MV2 alive means patching code upstream has deleted, every release. Our native blocker makes it unnecessary; uBlock Origin Lite (MV3) still works for those who want it. |
| Incognito | Extensions off by default, all third-party storage blocked, wiped on close | |

---

## 6. Performance and memory

The engine dominates memory, so savings come from **running less** and **sleeping more**, not from
magic. Targets must be measured, not promised.

**Run less**
- Release builds: official build, PGO, ThinLTO, no symbols in shipped binaries.
- Remove or default-off: on-device AI model downloads (multiple GB), Lens, Translate, Cast/Media
  Router (opt-in), Google sync/sign-in, real-time Safe Browsing, field trials, promos, shopping,
  reading list, side-panel features we don't use.
- Lighter UI: Views, not a web-based UI.

**Sleep more**
- Memory Saver on by default; tabs in **inactive spaces** are discarded after N minutes
  (configurable), pinned tabs keep only their state.
- Lazy session restore: restore tabs as placeholders, load on first focus.
- Freeze background tabs that are not playing media or holding a WebRTC/WebSocket connection.
- Preloading limited to "standard" (no aggressive prerendering of search results).

**Measure**
- Benchmarks in CI on every rebase: Speedometer 3, memory at idle / 10 tabs / 50 tabs, startup time,
  binary size. Compare against same-version Chrome and Brave. Block releases on regressions.
- Targets: idle memory ≥ 20% below same-version Chrome with the same
  tabs; cold start not slower than Chrome.

---

## 7. Features

Legend: ✅ planned for the platform · 🟡 adapted · ⏳ later · ❌ not on this platform

Built so far, on macOS desktop: spaces and profiles, the command bar, the first-run search-engine
and password-manager choices, Chrome Web Store extensions (MV3), the ad blocker (network filtering),
Peek, the little window, tab groups and split view (Chromium's own), Safe Browsing in standard mode,
the quit warning, and everything Chromium already provides (incognito, DevTools, PiP, import). The
rest of the table is specified but not built.

| Feature | Desktop | Android | iOS |
|---|---|---|---|
| Spaces & profiles | ✅ | 🟡 swipe between | 🟡 swipe between |
| Peek (link preview overlay) | ✅ Shift+click | 🟡 long-press → sheet | 🟡 long-press → sheet |
| Tab position left/top | ✅ | 🟡 address bar top/bottom | 🟡 address bar top/bottom |
| Little window | ✅ | 🟡 as Custom Tabs provider | ⏳ |
| Import from other browsers | ✅ | 🟡 file / via sync | 🟡 file / via sync |
| Incognito | ✅ | ✅ | ✅ |
| Form autofill | ✅ addresses | 🟡 OS autofill + addresses | 🟡 OS AutoFill |
| Developer tools | ✅ built-in | 🟡 remote over USB | 🟡 Safari Web Inspector |
| Picture-in-Picture | ✅ | ✅ | ✅ |
| Streaming-site compatibility | ✅ | ✅ | ✅ |
| Update check and prompt | ✅ | 🟡 via the app store | 🟡 via the App Store |

### 7.1 Spaces and profiles
- A **space** = name, icon/colour, its own pinned and today tabs, and the **profile** it runs in.
- A **profile** = one Chromium profile: logins, cookies, history and extensions.
  Profiles are named (Arc model). Everyone starts with the **default profile** and all spaces share
  it, like Arc Spaces. A space can switch to another profile or a new one (e.g. "Work" vs
  "Personal" Google accounts); **several spaces can share a profile**. Each extra profile costs
  memory, so "New profile" explains that. A new profile starts with empty cookies (we don't copy
  logins across).
- Switch: Ctrl+1…9, Ctrl+Option+←/→, the footer or a swipe on the sidebar. Inactive spaces' tabs sleep (section 6).
- Chromium has tab groups and "saved tab groups"; we build spaces as our own model in the core and
  use Chromium profiles for logins, not tab groups.

### 7.2 Peek (quick preview overlay)
- Trigger: Shift+click. Not built yet: other triggers as a setting (Alt+click, middle-click,
  long-press), and the rule that links from a pinned tab that leave its site open in Peek (Arc does
  this).
- UI: a floating card over the current page, as tall as the window and a little narrower than the
  page, over the dimmed window, with the page's site above it and round buttons beside it:
  "close", "open as tab", "open in split view" (Esc / ⌘W / click outside also close it). Scrolling
  and interaction work inside it.
- Implementation: a separate `WebContents` with the helpers a tab's page needs from its first load
  (ad blocking, popup blocking, passwords, ...), hosted in a child widget over the whole window; on
  "open as tab" we **move** that `WebContents` into the tab strip, so nothing reloads
  (desktop-ui.md).
- Mobile: long-press on a link → on iOS the system link-preview menu via `WKUIDelegate` with our
  actions added; on Android our bottom-sheet Peek (ux.md §10).

### 7.3 Tabs with configurable position
- **Left or top only** — exactly what upstream Chromium supports (Chrome shipped native vertical
  tabs in 2026). We add no layout patches; we only expose the choice in our settings and make sure
  our sidebar (space switcher) sits with the vertical tab strip.
- If upstream later adds more positions, we get them for free.
- Mobile: address bar at the bottom by default (top as an option), all-tabs view with page
  previews; tablets use the sidebar layout when wide (ux.md §10).

### 7.3a Tab groups
- Manual, Arc-style groups among today's tabs, one-click suggestions by project/site, optional
  auto-grouping; full behaviour in ux.md §6.1.
- Built on **Chromium's own tab groups** (tab group model + saved tab groups), so extensions
  (`chrome.tabGroups`), session restore and open-tab sync keep working; no parallel model.
- The project/site key uses the Public Suffix List (already in Chromium) plus a small built-in list
  of shared hosts in `lb-core`, so iOS and Android group the same way.

### 7.4 Little window
- A minimal window with **no tab strip**: just the page and a compact address/actions bar.
  Shortcut ⌥⌘N, through the command bar.
- Not built yet: an option to open links coming from **other apps** (mail, chat) in a little window
  instead of the main window.
- "Open in ‹space›" turns it into a normal tab without reloading (same `WebContents` move).
- Built on Chromium's popup window type: its one-row toolbar (address only) plus our two buttons.
- Android equivalent: serve **Custom Tabs** for other apps — a small overlay browser that other apps
  open, which is exactly the "little window" idea on mobile.

### 7.5 Pinned tabs and tab groups
- Per space, shown as list rows (icon and title) under the space name; groups (Arc-style folders)
  are Chromium's own tab groups, among the today tabs (Chromium can't group pinned tabs).
- Pinned tabs and groups are saved with the space (Local State, section 8 for sync).

### 7.6 Import
- Desktop: bookmarks, history, search engines, open tabs where possible, autofill addresses from
  Chrome, Edge, Brave, Arc, Firefox, Safari, and bookmarks HTML. Chromium already includes Firefox and
  Safari importers; Chrome-family profiles (same on-disk format) need our own importer.
- **No password import** — we point users to their password manager's own import.
- Safari on macOS needs Full Disk Access for some files; fall back to Safari's own HTML export.
- Mobile: import a bookmarks HTML file, or (simplest) sync from a desktop.

### 7.7 Incognito
- Desktop/Android: Chromium off-the-record profile + the stricter defaults in section 5.
- iOS: `WKWebsiteDataStore.nonPersistent()`; app switcher snapshot blurred; optional Face ID lock.

### 7.8 Form autofill
- Desktop: Chromium address autofill, **payment cards disabled**, **password manager disabled and
  hidden** (pref off + UI removed, rather than deleting code — far cheaper to maintain). Passwords
  are an extension's job: on first run the user can pick 1Password, Bitwarden or Proton Pass, which
  installs from the Chrome Web Store after Chromium's install prompt ([ux.md](ux.md) §12).
- Android: Android Autofill framework (password managers plug in here) + Chromium addresses.
- iOS: system AutoFill (contacts, passwords from third-party credential providers). Behaviour inside `WKWebView` must be verified.

### 7.9 Developer tools
- Desktop: Chromium DevTools as-is.
- Android: remote debugging over USB (`chrome://inspect` on a desktop).
- iOS: `WKWebView.isInspectable = true` → Safari Web Inspector on a Mac. Optional later: an
  on-device console for quick checks.

### 7.10 Picture-in-Picture
- Desktop: Chromium video PiP and Document PiP are built in. We add (a) a "pop-out" button that
  appears on hover over any playing video, and (b) **auto-PiP** when you switch tab/space while a
  video is playing (per-site setting, on by default for video sites).
- Android: Chromium's PiP when leaving the app during video; extend it to non-fullscreen video.
- iOS: `allowsPictureInPictureMediaPlayback`, an `AVAudioSession` in playback mode and the "Audio,
  AirPlay and Picture in Picture" background mode (iOS requires this mode for PiP itself; we do
  **not** use it to keep hidden pages playing).

### 7.11 Background and lock-screen playback — not supported
We do not work around sites that pause media when the page is hidden.
Whatever the engine does by default stays as it is (e.g. desktop tabs keep playing in the
background; mobile sites that pause themselves stay paused).

### 7.12 Streaming-site compatibility (YouTube, Twitch, BiliBili, Vimeo, Dailymotion, …)
These sites need standard web media features, all present in Chromium/WebKit if we build them in:

| Need | Desktop | Android | iOS |
|---|---|---|---|
| H.264 / AAC | Build with proprietary codecs; OS decoders on Mac/Win; **Linux needs a patent/licensing check** | OS decoders | Native |
| VP9, AV1, Opus | Built in | Built in | Built in (AV1 depends on device hardware) |
| HEVC (used by some BiliBili streams) | Hardware decoders via OS, where available | Hardware decoders | Native |
| HLS / DASH (Media Source Extensions) | Built in | Built in | Built in (HLS native) |
| DRM — Netflix, Disney+, Spotify, paid YouTube content | **Widevine, off until first use**: licensed and signed, but downloaded from Google only after the user agrees on the first site that needs it | Widevine via the OS | FairPlay via WebKit, works out of the box |

**DRM details.** Paid streaming sites send encrypted media; the browser decrypts it through the
EME web standard using a closed-source Content Decryption Module (CDM). Streaming services only accept
Widevine (Google), FairPlay (Apple) or PlayReady (Microsoft). For desktop we need: (1) a Widevine
licence agreement with Google (small browsers report long waits); (2) a
Widevine-approved ("VMP") signature on our Windows/macOS binaries, without which most services refuse
to play or drop quality; (3) the CDM download, served by Google and only triggered when a site needs
it **and** the user agrees ("This site needs Widevine from Google. Enable?"). Expect third-party
desktop browsers to be capped at roughly 720p–1080p by services; 4K usually needs hardware DRM.
Mobile needs none of this. Application process, checklist and drafts: [legal/widevine.md](legal/widevine.md).

Plus:
- Make sure the content blocker doesn't break players (sites fight ad blockers; filter lists need
  fast updates — ship list updates independently of browser releases).
- A **compatibility test matrix** (the sites above + Netflix/Spotify with Widevine enabled) runs before each
  release: playback, seeking, quality switch, fullscreen, PiP, live streams.

### 7.13 Update check and prompt
The desktop browser checks for a new version and asks the user to update. It checks at launch and
then every few hours, reading the static, signed update feed (§9; Sparkle on macOS, our own
updater on Windows, the package repository on Linux). When a newer version is out, the browser
shows a small prompt: **Update now**, which downloads it, verifies the signature and offers to
restart, or **Later**. A security release says so in the prompt and asks again sooner. The check
sends only the current version and platform. On Linux installs managed by a package manager or
Flatpak, the prompt points to that instead of updating itself. Android and iOS leave updates to
the app stores.

---

## 8. Sync without storing user data

### 8.1 The problem
Chromium's sync talks to Google; our iOS app isn't Chromium anyway. We need our own sync, and we
don't want a server that holds user data.

### 8.2 Transports
**Local-first, end-to-end encrypted, transport-agnostic.** The Rust core keeps an encrypted change
log; a transport just moves opaque encrypted blobs. We run no server that stores user data.

| Transport | Our servers | Works when devices are never online together | Cross-ecosystem (iPhone ↔ Windows) |
|---|---|---|---|
| **A. Device-to-device** (LAN discovery + internet hole-punching, relayed if needed) | Only a relay that forwards encrypted bytes and stores nothing | ❌ | ✅ |
| **B. User's own cloud storage** (iCloud Drive, Google Drive, Dropbox, OneDrive, WebDAV/Nextcloud, any synced folder) | None | ✅ | ✅ if the user has a common provider |
| **E. Export/import file** (manual fallback) | None | ✅ | ✅ |

- **A** gives instant sync when devices are online together.
- **B** serves devices that are never online at the same time (most phones).
- Known gap: a user with no shared cloud storage whose devices are never online together only
  syncs when both are open; the UI says so and suggests B.
- Connecting devices across the internet usually needs a relay that forwards encrypted bytes
  (stores nothing). We use the public relays of the P2P library we pick (e.g. iroh) as a fallback
  and let users set their own; LAN-only works with no relay at all.

### 8.3 How it works
- **Pairing:** the first device generates a random sync key. Adding a device: show a QR code on one
  device, scan with the other; a short-code fallback for desktop↔desktop. Pairing uses an
  authenticated key exchange, so the relay never learns the key.
- **Recovery:** optional recovery phrase (24 words). If you lose every device and the phrase, the
  synced data is gone — that's the price of us holding nothing, and the UI must say so clearly.
- **Removing a device:** rotate the key and re-share it with the remaining devices.
- **Merging:** a CRDT so devices can edit offline and merge without conflicts. Bookmarks
  need a *movable tree*. The CRDT library is not chosen yet: candidates are **Loro** (has a
  movable-tree type) and **Automerge** (both Rust), with simple last-writer-wins records with hybrid logical clocks for flat data as the fallback.
- **Encryption:** XChaCha20-Poly1305 for payloads, keys derived per data type; metadata (sizes,
  timings) padded/batched to leak less.
- **Mobile background limits:** iOS gives apps little background time, so phones sync mainly when
  the app opens; that's why B matters for phones.

### 8.4 Firewalls, NAT and VPNs

| Transport | Behind firewall / NAT / VPN | Why |
|---|---|---|
| **B — own cloud storage** | Works on almost any network | Each device only makes normal outbound HTTPS to the provider; devices never connect to each other |
| **A — device-to-device**, tried in order: | | |
| 1. Same LAN (mDNS discovery) | Works unless a VPN blocks local traffic (common with corporate VPNs) | Direct local connection |
| 2. Direct over the internet (hole punching) | Works through most home/mobile routers; often fails on strict corporate NAT, carrier-grade NAT, some VPNs | Both devices dial out at the same moment |
| 3. Relay fallback | Works almost everywhere | Both devices make outbound HTTPS to the relay, which forwards ciphertext only and stores nothing |

Where sync can still fail:
- **Locked-down corporate networks** (allow-lists, TLS interception that rejects non-browser
  traffic): relay/provider may be blocked → recommend B with an allowed provider.
- **Censored networks** blocking relay or provider domains → user-configured relay or WebDAV server.
- **Devices never online together** → only B helps.

**Do we run servers for this? No.**
- **WebDAV:** never ours. It's one option for users who already have one (Nextcloud, NAS, hosting
  provider). Most users pick iCloud / Google Drive / Dropbox / OneDrive.
- **Relay: we host our own** as the default, with the library's public relays as a fallback and a
  user-configurable relay URL. The relay stores nothing and only forwards end-to-end-encrypted
  bytes.
  - What it is: a single open-source binary (e.g. iroh's relay) behind HTTPS on port 443, plus STUN
    for hole punching. Stateless, so instances can be added or replaced freely.
  - Deployment: 2–3 small cloud VMs in different regions (e.g. EU, US, Asia), DNS per region,
    automatic TLS certificates, health checks. Only *relayed* sessions cost bandwidth; most syncs are
    direct or via cloud storage, and sync payloads are small.
  - Privacy: the relay sees IP addresses, timing and sizes of encrypted traffic. Policy: **no
    request logging**, only aggregate metrics (connections, bytes); publish this in the privacy policy.
  - Abuse: per-device rate and bandwidth limits; only paired devices (authenticated keys) can open
    relayed connections, so it can't be used as a general-purpose proxy.
  - Ops: a small but permanent cost (VMs + bandwidth + on-call for outages). Config and deployment
    scripts live in this repo so others can run identical relays.
- Risk: relay outage → sync falls back to public relays, direct connections, or cloud storage.

UX: sync settings show the route in use per device (**Local · Direct · Relayed · Cloud**) and a
clear message when no route works, suggesting cloud storage.

The chosen P2P library must be tested behind a corporate VPN, on hotel/captive Wi-Fi, and on mobile
carrier networks.

### 8.5 What syncs

| Data | Default | Note |
|---|---|---|
| Bookmarks | On | |
| Spaces, profiles & pinned tabs | On | Space layout only on desktop; logins don't sync |
| Open tabs | On | "Send tab to device" for free |
| Settings | On | Platform-specific settings stay local |
| Autofill addresses | Opt-in | |
| History | **Off** | Large and sensitive; opt-in, last 30 days |
| Extensions list | Desktop only, opt-in | Re-installs from Chrome Web Store |
| Passwords, cards | **Never** | That's the password manager's job |

---

## 9. Servers we still need (none hold user data)

| Purpose | What it is | Sees |
|---|---|---|
| Updates (desktop) | Static, signed update feeds + binaries on a CDN (Sparkle on macOS; equivalent on Windows; apt/rpm repos + Flatpak on Linux) | IP, version |
| Mobile distribution | App Store, Google Play, F-Droid/APK | — (store's own data) |
| Filter-list updates | Every 4 days, straight from the lists' maintainers: `easylist.to`, `ublockorigin.github.io`, `pgl.yoyo.org`; an hour later after a failed download | IP, version (the user agent); no cookies |
| Widevine DRM component | Downloaded from Google, only after the user agrees on first use | IP, version |
| Safe Browsing | Google's update API (hash prefixes only) | Hash prefixes, IP |
| Extensions (desktop) | The Chrome Web Store: its pages, the extension download, update checks to `update.googleapis.com` every few hours while extensions are installed, and `chromewebstore.googleapis.com` for the details of an extension being installed from the first-run password manager choice, one another program installed, or one being repaired | IP, the update check's contents (§5): installed extensions' ids, versions, state and install dates, browser version, OS, language; the store page also sees which extensions are installed |
| Crash reports | **Opt-in** only, self-hosted | Crash dump |
| P2P relay for device-to-device sync — **ours by default** (2–3 regions), public/user relays as fallback | Forwards encrypted bytes, stores nothing, no request logs | IP, timing/size of encrypted traffic |

---

## 10. Engineering reality (cost)

- A Chromium source tree is ~15 GB plus 10–25 GB of output; a clean build took ~2.5 h on a 14-core
  M4 Pro. We need dedicated builders with build caching, plus macOS and Windows signing, from day
  one — see §14.
- Every 4 weeks a new Chromium major: rebase, fix patches, run the test matrix, release. Security
  point releases in days. This is **permanent** work: plan for 1–2 engineers on it, always.

---

---

## 11. Build status

| Area | State |
|---|---|
| macOS desktop | Builds and runs: sidebar, spaces and profiles, pinned tabs as a list, command bar, search-engine and password-manager choices, Chrome Web Store extensions (MV3), the ad blocker (network filtering), Peek, the little window, Safe Browsing (standard), branding, quit warning |
| Windows, Linux desktop | Not built: the platform recipes aren't wired up |
| Element hiding, tracking-parameter stripping, import, auto-PiP, on-demand Widevine | Specified, not built |
| `lb-core` | Data model and the sync `Transport` trait with an in-memory transport; no dependencies yet |
| Sync, iOS, Android | Not started |

The order of work is desktop, then iOS and sync, then Android. iOS comes before Android because a
WKWebView app is far less work than an Android Chromium fork.

---

## 12. Main risks

| Risk | Impact | Mitigation |
|---|---|---|
| Rebase burden grows with every feature | Releases slip, security fixes late | Minimal patch policy (section 1), separate source tree, upstream features first (e.g. vertical tabs) |
| Sites break due to blocking | Users leave | Per-site toggle, fast list updates, compatibility matrix |
| Widevine licence slow or refused | Netflix/Spotify don't work on desktop | Apply early; desktop can ship without it if needed |
| Sync UX too hard without accounts | Low adoption of sync | QR pairing, clear recovery story, guided own-cloud-storage setup |
| iOS feature parity limits | Different behaviour across platforms | Document differences, same concepts not identical behaviour |

---

## 13. Open-source release

Little Browser is open source. What that means in practice:

| Topic | Plan |
|---|---|
| Licence | Our code: **MPL-2.0**. Chromium: BSD-3 (its third-party notices are generated into `about:credits`). Each file carries the MPL header. |
| Repository | Hosted on **GitLab**, which runs CI and releases. A **read-only mirror on GitHub** (`github.com/little-browser/little-browser`) carries the protected branches (`main`) and release tags; feature, fix and `bump/*` branches stay on GitLab. GitHub pull requests are off, because the next push would overwrite a merge there; GitHub Issues are on for bug reports. |
| Name and IDs | **Little Browser**; site **littlebrowser.app**. App IDs use its reverse: macOS `app.littlebrowser.LittleBrowser`; iOS and Android take the same `app.littlebrowser.` prefix. An app ID can't change after release without breaking updates and store listings. Logo and usage rules: `branding/`. |
| Contributions | **DCO** sign-off (`git commit -s`) rather than a CLA — lighter for contributors; see `CONTRIBUTING.md`. |
| Trademark | MPL grants no rights to the name or logo. Forks must rebrand (Firefox model). |
| What stays private | Code-signing keys, update-feed signing keys, app-store accounts, the Widevine licence and VMP certificate (forks need their own). |
| Closed parts we use | The Widevine CDM (downloaded from Google only on user consent); OS media decoders. Nothing closed is in our repo. |
| Filter lists | Not bundled; downloaded at runtime from their maintainers — keeps their licences (GPL / CC BY-SA) separate from ours. |
| Codecs | Distributing binaries with H.264/AAC is a **patent** question, separate from open-source licensing. Needs legal review, especially for Linux builds. |
| Security | `SECURITY.md`: private reporting through GitHub's private vulnerability reporting; we follow Chromium security releases within days. |
| Trust | Publish build arguments and an SBOM per release; reproducible builds as a goal. |
| Governance | Core team decides for now; write a governance doc before accepting outside maintainers. |

## 14. Build and release pipeline

| Job | Runs on | When |
|---|---|---|
| Fast checks: Rust core, script lint, `patch-check`, release bot | `lb-linux`: small AWS Linux spot | Every push and merge request |
| Chromium build, macOS arm64 (x64 later) | `lb-macos`: EC2 Mac host (mac2-m2pro) allocated per release by the `macos-arm64-host` job; a cold build on the instance's own disk (a persistent data volume is optional) | **Release tags only**: hours per version bump |
| Chromium build, Linux / Windows | 64 vCPU AWS **spot** instances (300–400 GB disk), one job per host: `lb-linux-build` / `lb-windows-build` for merge requests, `lb-linux-release` / `lb-windows-release` for protected refs | **Every merge request** and release tags, once those platform recipes exist |
| Compiler cache | **sccache** via GN `cc_wrapper`, keyed by file content, so it survives `fetch.sh` recreating the tree. S3 bucket for the spot instances, with separate `sccache/mr/` and `sccache/release/` prefixes and roles, and on the Mac only when it keeps a persistent data volume. Same checkout path on every runner so keys match | Always |

**What a build costs.** A change to our own code on the same Chromium version mostly hits the
cache: minutes. A Chromium version bump, about weekly, changes headers that nearly everything
includes, so it is close to a full rebuild: about an hour on a 64+ core spot instance, several hours
on the macOS runner. A patch to a widely included header can cost almost as much.

**A release, step by step**
1. **Watch.** A scheduled pipeline (`upstream-watch`, `desktop/scripts/release-bot.sh bump`) checks
   for ungoogled-chromium (and platform repo) tags newer than `desktop/VERSIONS`
   (`desktop/scripts/upstream.sh`). On a new one it pushes `bump/<version>` with `VERSIONS` updated
   and opens a merge request. Checks there: every patch applies (`patch-check`:
   `fetch.sh --patches-only`, no compile), then the Linux and Windows builds once they exist.
2. **Merge.** A person reviews and merges the bump, fixing patches on the branch if needed.
3. **Tag.** Merging a change to `VERSIONS` tags `v<CHROMIUM_VERSION>-1` (e.g. `v157.0.8103.78-1`)
   automatically (`auto-tag`); a platform-only bump on the same Chromium version gets the next
   free revision.
   Releases of our own changes on the same Chromium version are tagged by hand with the next
   revision (`-2`, `-3`, …).
4. **Build.** On the tag, builders run `desktop/scripts/fetch.sh` + `build.sh` from scratch, with
   the compiler cache; keys (Safe Browsing, Widevine signing) are injected from protected
   CI/CD variables, never committed.
5. **Sign.** macOS Developer ID + notarization → `.dmg`; Windows code signing → installer; Linux
   `.deb`/`.rpm` + Flatpak.
6. **Internal channel.** The signed `.dmg` is kept as a pipeline artifact for 30 days; internal
   testers download it from the tag's pipeline.
7. **Publish (manual).** After internal testing, a person runs the `publish` job
   (`desktop/scripts/publish-release.sh`): it uploads the same file to
   `downloads.littlebrowser.app/<version>/` and adds it, with its size and SHA-256, to
   `releases.json` in the `beta` channel. littlebrowser.app reads that file for its download button
   and its list of releases, which shows each file's full SHA-256 with a button to copy it.
   Published files are never replaced or deleted. Later: the exact build arguments and an SBOM
   alongside, and the Sparkle update feed (static, signed files, no user data). Nothing is rebuilt.

The website itself is the `site/` folder, uploaded to `littlebrowser.app` by the `site` job when it
changes on `main` (`desktop/scripts/deploy-site.sh`).

Security releases follow Chromium's (roughly weekly) within days, so only steps 2 and 7 wait on a
person.

**Prerequisites:** Apple Developer account (US$99/yr), a Windows signing identity, the macOS
runner, and replacing the community-built LLVM before the first public build (`desktop/README.md`).
The pipeline lives in `.gitlab-ci.yml`; its Chromium jobs are untested until the runners exist.

---

## References

- Chrome removing the last MV2 workarounds (mid-2026):
  [9to5Google](https://9to5google.com/2026/06/15/google-chromes-next-update-will-mark-the-end-of-popular-ad-blockers/),
  [AlternativeTo](https://alternativeto.net/news/2026/6/chrome-moves-to-fully-block-mv2-extensions-effectively-killing-ublock-origin-workarounds/),
  [Michael Tsai](https://mjtsai.com/blog/2026/07/30/dropping-manifest-v2-extensions/)
- Alternative browser engines on iOS:
  [Apple — EU](https://developer.apple.com/jp/support/alternative-browser-engines/),
  [Apple — Japan](https://developer.apple.com/support/alternative-browser-engines-jp),
  [Michael Tsai — iOS 26.2 in Japan](https://mjtsai.com/blog/2026/01/06/ios-26-2-alternative-browser-engines-in-japan/)
- Native vertical tabs in Chrome (April 2026):
  [TechCrunch](https://techcrunch.com/2026/04/07/chrome-is-finally-getting-vertical-tabs/),
  [Chrome Unboxed](https://chromeunboxed.com/google-is-officially-rolling-out-native-vertical-tabs-to-the-stable-version-of-chrome/)
