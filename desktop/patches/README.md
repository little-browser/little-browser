# Desktop patches

Our Chromium patches for the desktop, applied by `scripts/fetch.sh` after ungoogled-chromium's core
and platform patches and after the shared series, in the order `series` lists them. How to write
one is in `../AGENTS.md` ("Changing Chromium code"); `ungoogled-exclude.txt`, `domsub-exclude.txt`
and `prune-keep.txt` are explained in `../README.md`.

## Shared and desktop patches

The engine patches for a feature every Chromium platform has (the ad blocker, element hiding,
tracking parameters, Safe Browsing's standard mode, our prefs) are in `../../chromium/patches`,
with their own `series`, applied before this one. A shared patch holds engine code only: its view
half (`<name>-ui.patch`) stays here, and its Java half goes in `android/patches`. Everything that
only the desktop needs, engine code included (the tab strip, sessions, extensions, menus), is
here. Desktop and Android both apply the shared series before their own. The two trees differ (desktop
has ungoogled-chromium's patches, Android doesn't), so `check_patches.py` checks the shared series
on the desktop tree, `android/scripts/fetch.sh --patches-only` on Android's. A shared patch that
can't fit both is two entries in the shared series: the desktop's copy tagged `[desktop]` (every
desktop platform) and its twin tagged `[android]`.

## Layers

A patch for every platform holds one layer:

- **Engine:** everything that isn't view code: components, `content`, `extensions`,
  `chrome/browser` outside its UI folders, and models such as the tab strip and menu models.
- **View:** Views (`ui/views/`, `chrome/browser/ui/views/`), Cocoa (`chrome/browser/ui/cocoa/`)
  and WebUI (`chrome/browser/resources/`, `ui/webui/resources/`, `chrome/browser/ui/webui/`).

Strings (`.grd`, `.grdp`) can go with either. A change that needs both layers is two patches,
`<name>.patch` with the engine half and its reason, and `<name>-ui.patch` with the view half,
listed right after it. Linux and Windows use the same Views code, but Android and any other shell
don't: keeping the layers apart lets them take the engine half as it is. Our own UI lives in
`../../chromium/lb/`; view patches only hook it in.

## Platform tags

A patch for one platform starts its reason with the platform's tag, as in
`lb/updater.patch  # [mac] start the updater …`. The other platforms' builds skip it. A patch that
touches a platform's own files must be tagged: for macOS that is `.mm`, `*_mac.*`, `mac/` and
`cocoa/` (`_win`/`win/` and `_linux`/`linux/` likewise). A tagged patch is for one platform only,
so it may span layers. When a change needs a platform's files and shared ones that the other
platforms need too, the shared part is its own patch: `no-reopen-closed.patch` (engine),
`no-reopen-closed-ui.patch` (Views, WebUI) and `no-reopen-closed-mac.patch` (`[mac]`, Cocoa).

## Checks

- `../../chromium/scripts/check_series.py` (CI `patch-layers`, every push): every entry exists and every patch
  is listed, tags name a platform in `../platforms/`, platform files are tagged, and no untagged
  patch mixes layers. It checks `android/patches` too, where Java and native code are the layers.
- `../../chromium/scripts/check_patches.py [--platform <name>]`: the platform's series applies to a clean
  tree with no fuzz or offset. CI's `patch-check` runs `fetch.sh --patches-only` and then this,
  for every platform, on a Chromium bump or a patch change. It uses GNU `patch`, which is stricter
  than macOS's about uneven context: a hunk with more context lines on one side needs fuzz there.
