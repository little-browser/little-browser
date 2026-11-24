# Android — Chromium fork

Chromium's own Android app (`chrome_public_apk`) with our privacy patches and the app ID
`app.littlebrowser.browser`. No Little Browser UI or features yet. See
[docs/design.md §4.2](../docs/design.md#42-android). It will use `lb-core` through UniFFI.

It is built from Chromium itself, not ungoogled-chromium: ungoogled-chromium has no maintained
Android platform repo, and its patches and flags are only tested on desktop. The Chromium version
is the desktop one, in `desktop/VERSIONS`.

## Layout

| Path | What |
|---|---|
| `args/android.gn` | GN args: Android, official release build, app ID, no Google API keys or field-trial config |
| `patches/series` | Our Chromium patches, in order, one concern each |
| `scripts/fetch.sh` | depot_tools, a gclient checkout of Chromium with the Android SDK, NDK and PGO profiles, then our patches, into `build/src`; `--patches-only` checks the patches still apply |
| `scripts/build.sh` | Generate args and build `ChromePublic.apk` with GN + Ninja |

`build/` and `third_party/` are git-ignored. The scripts share their helpers with the desktop
ones (`desktop/scripts/lib.sh`: the series, GN args, gn + ninja, sccache, the CI build log).

The patches change native code or Java, never both in one patch; `desktop/scripts/check_series.py`
checks this along with the desktop series (`desktop/patches/README.md`).

## Privacy

The baseline is docs/design.md §5. A Chromium build that isn't Google Chrome already leaves out
usage statistics (UMA), crash uploads, repeated field-trial fetches, Omaha update checks, Google
sign-in and accounts (no Google Play services bridge), Lens, surveys and WebAPK minting. It has
no Google API keys. Our patches turn off what still reaches Google on its own, and set the privacy
defaults the desktop build has:

| Patch | Stops or changes |
|---|---|
| `no-first-run-variations-seed` | Field-trial seed from `clientservices.googleapis.com` during first run |
| `no-component-updates` | Component updater checks to `update.googleapis.com` |
| `no-network-time-queries` | Time queries to `clients2.google.com` |
| `no-autofill-server` | Form signatures sent to `content-autofill.googleapis.com` |
| `no-search-preconnect` | Background connections to the search engine |
| `no-popular-sites` | The "popular sites" list from `www.gstatic.com` for new tab tiles |
| `no-discover-feed` | The Discover feed (`discover-pa.googleapis.com`) on the new tab page |
| `no-google-doodle` | Doodle and logo downloads from `www.google.com` on every new tab |
| `no-google-favicon-server` | Favicons from `t0.gstatic.com`, which would learn the sites behind tiles and bookmarks |
| `translate-off-by-default` | Translate offers (they fetch from `translate.googleapis.com`) and the ranker model |
| `privacy-default-prefs` | Third-party cookies blocked; search suggestions, hyperlink auditing, address and card autofill, password saving and auto sign-in, payment-method queries and Touch to Search off; Google sign-in not allowed |
| `first-run-usage-stats-off` | The first-run screen no longer opts in to usage statistics |
| `first-run-search-choice` | No default search engine: first run asks, with Chromium's own first-run page (the engines Chromium has built in for the user's country, shuffled, none preselected); without a choice it asks again on the next start |

HTTPS-First (balanced mode) is already Chromium's default.

Not yet as the baseline promises, on Android:

- **Safe Browsing:** on Android, Chromium checks pages through Google Play services, which this
  build has no bridge to, so pages are not checked.
- **Ad blocker:** the desktop build's blocker (Chromium's subresource filter fed our filter
  lists, `desktop/lb/browser/adblock`) isn't on Android yet; tracking-parameter stripping isn't
  built anywhere.

## Prerequisites

Chromium builds Android only on Linux x86_64. Ubuntu 22.04 or 24.04, 100 GB+ free disk, 16 GB+
RAM, and `git`, `python3`, `curl`, `lsb-release` and `sudo`. Not macOS, so not the desktop build
machine.

## Build

Prepare the source tree. This is a gclient checkout without history (tens of GB), not the source
tarball, which lacks the Android SDK and NDK:

```bash
android/scripts/fetch.sh
```

Install Chromium's build dependencies for Android (uses `sudo`):

```bash
android/build/src/build/install-build-deps.sh --android --no-chromeos-fonts
```

Build (first run takes hours; re-run for incremental builds). The default is arm64; `x64` is for
x86_64 emulators:

```bash
android/scripts/build.sh
```

Install it on a device or emulator:

```bash
adb install android/build/src/out/arm64/apks/ChromePublic.apk
```

The APK is signed with Chromium's public debug key, so it is for testing only.

## Changing a patch

`fetch.sh --patches-only` checks the series against a sparse checkout of just the files it
touches, on any OS, in seconds, and leaves that checkout in `build/patch-check/` with the series
applied. Each file belongs to one patch, so to change a patch, edit its files there and replace
everything below the patch's `#` header with `git -C android/build/patch-check diff -- <its
files>`. For a new file, add its path to the sparse checkout first
(`git sparse-checkout add /<path>`). Patches are applied with `git apply`, so a patch must match
exactly: no fuzz.

## CI

- `android-patch-check` runs `fetch.sh --patches-only` on merge requests that change
  `desktop/VERSIONS` or the patches.
- `android-arm64` (manual) runs the full build on the `lb-linux-build` runner and keeps the APK
  for 30 days. It extends `.linux-chromium`, the template every Linux-hosted Chromium build uses:
  a fresh `ubuntu:24.04` prepared by `desktop/scripts/linux-build-host.sh`, and sccache with its
  cache in S3 (`LB_CC_WRAPPER=sccache`, `sccache/mr/`).
