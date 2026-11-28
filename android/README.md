# Android — Chromium fork

Chromium's own Android app (`chrome_public_apk`) with the app ID `app.littlebrowser.browser`, the
engine patches every Chromium platform shares (`chromium/patches`) and our Android privacy and
ad-blocker patches, with our source tree linked in, and our shell on top (`chromium/lb/android`):
spaces, the bar at the bottom, the search screen, the all-tabs view and spaces sheet, the tablet
sidebar on Chromium's vertical tabs, Peek opening as a tab, the little window, first run's
search engine and password-manager questions, a window per profile, bookmarks file import and the
per-site Ads switch. See [docs/design.md §4.2](../docs/design.md#42-android); the shell, what it
shares with desktop and how each macOS feature comes to Android are in
[docs/android-ui.md](../docs/android-ui.md). It uses
`lb-core` through the desktop's C++ binding, and its Java reaches it through Chromium's JNI
(`chromium/lb/android`).

It is built from Chromium itself, not ungoogled-chromium: ungoogled-chromium has no maintained
Android platform repo, and its patches and flags are only tested on desktop. The Chromium version is
the desktop one, in `chromium/VERSIONS`. `fetch.sh` links `src/lb` → `chromium/lb` and `src/lb_core`
→ `core/lb-core`, as the desktop's does, and applies the shared series before ours, so the build
compiles `//lb`'s targets for every platform (`chromium/lb/AGENTS.md`). The `android-arm64` and
`android-x64` CI jobs build it and run our Java's Robolectric tests; what has been checked on a phone emulator is in
[docs/android-ui.md](../docs/android-ui.md).

## Layout

| Path | What |
|---|---|
| `args/android.gn` | GN args: Android, official release build, app ID, no Google API keys or field-trial config |
| `patches/series` | Our Android Chromium patches, in order, one concern each, applied after `chromium/patches/series` |
| `scripts/fetch.sh` | depot_tools, a gclient checkout of Chromium with the Android SDK, NDK and PGO profiles, then the shared patches and ours, and the links to our source tree, into `build/src`; `--patches-only` checks both series still apply |
| `scripts/build.sh` | Generate args and build `ChromePublic.apk` with GN + Ninja, then run our Java's Robolectric tests (`lb_junit_tests`) |
| `scripts/java_classpath.py` | Pack the jars on Chrome's Java compile classpath and the generated sources into `java-classpath.tar.gz`, so our Java and the Java our patches change compile with plain `javac` on any machine; tested by `test_java_classpath.py` |

`build/` and `third_party/` are git-ignored. The scripts share their helpers with the desktop
ones (`chromium/scripts/lib.sh`: the series, GN args, gn + ninja, sccache, the CI build log).

The patches change native code or Java, never both in one patch; `chromium/scripts/check_series.py`
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
| `no-thumbnails-on-disk` | Tab previews written to disk: they stay in memory (docs/ux.md §10) |
| `translate-off-by-default` | Translate offers (they fetch from `translate.googleapis.com`) and the ranker model |
| `welcome-terms-url`, `welcome-terms` | The welcome screen's terms link opens our terms of use (littlebrowser.app/terms), not Google's |
| `no-account-button` | The Google account button on the new tab page's bar, which only led to sign-in |
| `privacy-default-prefs` | Google sign-in not allowed; Touch to Search off. The other privacy defaults are shared with the desktop (`chromium/lb/browser/prefs/pref_defaults.*`): third-party cookies blocked; search suggestions, hyperlink auditing, address and card autofill, password saving and auto sign-in, and payment-method queries off |
| `first-run-usage-stats-off` | The first-run screen no longer opts in to usage statistics |

HTTPS-First (balanced mode) is already Chromium's default.

Not yet as the baseline promises, on Android:

- **Safe Browsing:** on Android, Chromium checks pages through Google Play services, which this
  build has no bridge to, so pages are not checked.

## No promos

Chromium's app promotes its own features; these patches remove that, as docs/ux.md §1 (Calm)
asks:

| Patch | Removes |
|---|---|
| `no-home-modules` | The new tab page's home modules: Chrome tips and the setup list, "continue with this tab", Safety Hub notices, price tracking, the promo to share tabs with Android's search |
| `no-ntp-customization-promo` | The "Now you can customize your homepage" sheet, its help bubble and its promo card; customising stays in the menu |
| `no-startup-notification-prompt` | The notification permission prompt at startup; a site or feature that needs notifications still asks when it does |

## Branding

`scripts/fetch.sh` renames the product in Chromium's string files with the desktop's script,
`chromium/scripts/rebrand_strings.py`, re-keying their translations. Chromium's Android strings
call the app "Chrome" as often as "Chromium" ("Welcome to Chrome"), so it renames both
(`--chrome`), but not other things named after Chrome (ChromeOS, the Chrome Web Store) or
Google's terms of service. The app's name itself is a resource, not a string: `app-name.patch`.
`scripts/brand_assets.sh` copies the launcher icon's layers and the logos from
`branding/exports/android` over Chromium's: `app-icon.patch` puts the layers together as the
adaptive icon, with our mark as the monochrome layer of themed icons, and `logo-dark.patch` lists
the lockup's white copy for dark mode. Chromium's animated logo (`signin_spin.json`), drawn over the
welcome screen's logo, is taken out of its layouts (`no-chromium-logo-animation.patch`): it only
turns into the Google account's picture when signing in, which this build doesn't allow.

## Prerequisites

Chromium builds Android only on Linux x64. Ubuntu 22.04 or 24.04, 100 GB+ free disk, 16 GB+
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

Build (first run takes hours; re-run for incremental builds). The default is arm64, for phones, tablets
and Arm Chromebooks; `x64` is for Intel and AMD Chromebooks and x64 emulators:

```bash
android/scripts/build.sh
```

The APK is `android/build/src/out/arm64/apks/ChromePublic.apk` (`out/x64/` for an x64 build).
It is signed with Chromium's public debug key, so it is for testing only.

## Run it on an emulator

The emulator runs on any machine with the Android SDK, such as a Mac with Android Studio. Copy the
APK there from the build host. An Apple Silicon Mac runs arm64 system images, so it needs the arm64
build; an x64 machine needs `x64`.

Put the SDK's tools on your `PATH` (Android Studio installs the SDK in `~/Library/Android/sdk` on
macOS and `~/Android/Sdk` on Linux). In `~/.zshrc` or `~/.bashrc`:

```bash
export ANDROID_HOME="$HOME/Library/Android/sdk"
export PATH="$ANDROID_HOME/emulator:$ANDROID_HOME/platform-tools:$PATH"
```

Create a virtual device in Android Studio's Device Manager, then list the names the emulator knows.
The name is the device's `.ini` file in `~/.android/avd`, such as `Medium_Phone_API_36.1`, not
its `.avd` folder:

```bash
emulator -list-avds
```

Start it, and wait until it has booted (`adb install` fails on a device that is still starting):

```bash
emulator -avd Medium_Phone_API_36.1 &
```

```bash
adb wait-for-device && until [ "$(adb shell getprop sys.boot_completed | tr -d '\r')" = 1 ]; do sleep 2; done
```

Install the APK (`-r` replaces an installed build and keeps its data), and start it:

```bash
adb install -r ChromePublic.apk
```

```bash
adb shell monkey -p app.littlebrowser.browser -c android.intent.category.LAUNCHER 1
```

Its log:

```bash
adb logcat --pid="$(adb shell pidof app.littlebrowser.browser)"
```

To start again with no data, uninstall it before installing:

```bash
adb uninstall app.littlebrowser.browser
```

The same `adb` commands work on a phone connected by USB with USB debugging on.

## Changing a patch

`fetch.sh --patches-only` checks both series, the shared one (`chromium/patches`) and then
Android's, against a sparse checkout of just the files they touch, on any OS, in seconds, and
leaves that checkout in `build/patch-check/` with both applied. To change a patch, edit its files
there and replace everything below the patch's `#` header with `git -C android/build/patch-check
diff -- <its files>`. That diff holds every patch's changes to those files, so where another patch
changes the same file (`chrome/browser/prefs/browser_prefs.cc` is in
`chromium/patches/sidebar-prefs.patch` and in Chromium's own tree changes), keep only this
patch's hunks; the next `--patches-only` run shows whether it still applies after the patches
before it. For a new file, add its path to the sparse checkout first
(`git sparse-checkout add /<path>`). Patches are applied with `git apply`, so a patch must match
exactly: no fuzz.

## CI

- `android-patch-check` runs `fetch.sh --patches-only` on merge requests that change
  `chromium/VERSIONS` or the patches.
- `android-arm64` and `android-x64` (manual) run the full build for their CPU on the
  `lb-linux-build` runner and keep the APK for 30 days, with what reads its crash stacks: R8's
  map of the renamed Java classes (`ChromePublic.apk.mapping`) and `libchrome-symbols.txt.gz`,
  libchrome.so's function addresses, which the APK's stripped copy lacks; a failed job keeps them too. `android-arm64` also
  keeps `java-classpath.tar.gz`. Google Play serves the x64 APK to Intel and AMD Chromebooks and the
  arm64 one to everything else; Chromium gives each CPU its own version code, so both can be
  uploaded for the same release. It extends `.linux-chromium`, the template every Linux-hosted Chromium build uses:
  a fresh `ubuntu:24.04` prepared by `desktop/scripts/linux-build-host.sh`, and sccache with its
  cache in S3 (`LB_CC_WRAPPER=sccache`, `sccache/mr/`).
