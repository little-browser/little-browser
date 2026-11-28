# Desktop (macOS, Windows, Linux) — Chromium fork

Upstream Chromium **stable** + **ungoogled-chromium** patch set + our patches and source tree.
See [docs/design.md §4.1](../docs/design.md#41-desktop-how-we-fork-chromium).

## Layout

| Path | What |
|---|---|
| `VERSIONS` | Pinned Chromium version and ungoogled-chromium tags (core and each platform repo) |
| `args/` | GN build args: `common.gn` + one per platform, applied after ungoogled's `flags.gn` |
| `platforms/` | One entry per desktop platform (`mac.sh`): what differs in fetching and building; `platforms/README.md` |
| `patches/series` | Our Chromium patches, in order, one concern and one layer each, some tagged for one platform; `patches/README.md` |
| `patches/ungoogled-exclude.txt` | ungoogled patches we skip (Safe Browsing and the Chrome Web Store are kept, MV2 is off) |
| `patches/domsub-exclude.txt` | Paths kept out of domain substitution (Safe Browsing, Widevine, the Chrome Web Store) |
| `branding/BRANDING` | Product name, company, copyright, bundle id |
| `lb/` | Our own code, linked into the tree as `src/lb` |
| `scripts/fetch.sh` | Download + prepare the source tree into `build/src` (`--platform <name>`; default: this machine's) |
| `scripts/lib.sh` | Helpers shared with the Android scripts (clones, the series for a platform, GN args, gn + ninja, the CI build log) and the platform-entry loader; tested by `test_lib_sh.py` |
| `scripts/rebrand_strings.py` | Rename the product in Chromium's branded strings and re-key all 81 translations |
| `scripts/brand_assets.sh` | Install the shared product logos from the repo's `branding/exports`; each platform entry adds its own icons (macOS `app.icns` + `Assets.car` via `actool`) |
| `scripts/build.sh` | Generate args and build with GN + Ninja (`--dev` for the fast dev build) |
| `scripts/check_patches.py` | Check that our patch series applies to a clean tree (about a minute, no re-fetch); see `AGENTS.md` |
| `scripts/check_series.py` | Check the patch series' layers and platform tags (desktop and Android) |
| `scripts/linux-build-host.sh` | Prepare a CI container on the Linux build runners: fetch tools and sccache |
| `scripts/run.sh` | Launch a build with a separate developer profile |
| `scripts/upstream.sh`, `release-bot.sh`, `macos-host.sh` | Release automation (CI); see `AGENTS.md` |

`build/` and `third_party/` are git-ignored.

## Prerequisites (macOS, the only platform with an entry)

- ~100 GB free disk, 16 GB+ RAM (32 GB+ comfortable)
- Xcode with the Metal toolchain: `xcodebuild -downloadComponent MetalToolchain` if
  `xcrun metal --version` fails (≈840 MB from Apple)
- A macOS SDK the pinned LLVM can read: `build.sh` picks the newest one without the `arm64e.x1`
  target (on this setup the Command Line Tools' 26.5 SDK); override with `LB_MAC_SDK=/path/to/sdk`
- Homebrew tools:

```bash
brew install ninja coreutils esbuild typescript go python
```

- Python 3.10 or newer as `python3`: macOS's own `/usr/bin/python3` (3.9) fails Chromium's build
  scripts, so `/opt/homebrew/bin` has to come before `/usr/bin` in `PATH`

## Build

Prepare the source tree (downloads the Chromium tarball and toolchains, applies all patches):

```bash
desktop/scripts/fetch.sh
```

Build (first run takes hours; re-run for incremental builds):

```bash
desktop/scripts/build.sh
```

Fast development build into `out/Dev` (component build, no LTO; first build takes hours like the
release one, after that most edits rebuild and link in seconds to a couple of minutes):

```bash
desktop/scripts/build.sh --dev
```

Run a build with a separate developer profile (`--dev` for `out/Dev`, `--fresh` for an empty profile):

```bash
desktop/scripts/run.sh --dev
```

Ad-hoc signed `.dmg` of the release build (`build/LittleBrowser-macos-<arch>.dmg`, `<arch>` being
`arm64` or `x64`, the app next to an Applications link), through `scripts/package-macos.sh`:

```bash
desktop/scripts/build.sh --package
```

Releases use `scripts/package-macos.sh --release` in CI: a Developer ID signature with the
hardened runtime, notarised and stapled, with the certificate and an App Store Connect API key read from
AWS Secrets Manager (`little-browser/signing/apple-developer-id` and `apple-notarization`) by the
release builder. While those secrets are empty it falls back to an ad-hoc signature. An ad-hoc
signature carries no signing options: the hardened runtime's library validation needs a Team ID.

Local builds are ad-hoc signed and the signature changes with every rebuild, so macOS asks for
the "Little Browser Safe Storage" keychain item each time ("Always Allow" is tied to the
signature). `run.sh` passes `--use-mock-keychain`. The flag covers only that item: importing
cookies still reads the other browser's real "‹browser› Safe Storage" key, and macOS asks.

The item is Little Browser's own (`patches/lb/keychain-name.patch`, `lb/browser/keychain/`), not
Chromium's default "Chromium Safe Storage", which real Chromium shares. An install whose profile
already existed copies its password from "Chromium Safe Storage" once; a new install never reads
that item. A release's designated requirement pins our Team ID, because macOS lets whatever meets
it read the item without asking.

| Build | Folder | Use for |
|---|---|---|
| Release | `out/Default` | packaging, performance/memory measurements, final checks |
| Dev | `out/Dev` | day-to-day feature work (`args/dev.gn`) |

Those folders hold builds for this Mac's own architecture. A build for the other one
(`build.sh x64` on Apple silicon, `build.sh arm64` on an Intel Mac) goes to its own folder,
`out/Default-x64` or `out/Dev-arm64`, so it never replaces the native build; `run.sh --arch x64`
and `build.sh --package x64` use it. `build.sh` refuses to reuse a folder that holds another
architecture's build. One source tree builds both: `fetch.sh` installs this Mac's toolchains
and the other architecture's Rust standard library, which an x64 build on Apple silicon (or an
arm64 one on Intel) links. `fetch.sh --toolchains-only` installs the toolchains again in an
existing tree (the next build rebuilds bindgen and Crubit). Run the
scripts in a native shell: under Rosetta an Apple silicon Mac passes for an Intel one, and they stop.

We follow ungoogled-chromium-macos's recipe (`third_party/ungoogled-chromium-macos`, pinned in
`VERSIONS`), with two differences: the Chromium **source tarball** instead of a git clone (no
`depot_tools`, much smaller download), and PGO off (`chrome_pgo_phase = 0`: the tarball has no
profiles).

**Toolchain provenance — fix before any public release.** The recipe downloads prebuilt LLVM from a
community buildbot on GitHub (pinned by SHA-512), Rust nightly from rust-lang.org, Node.js from
nodejs.org and Crubit from Google's GitHub. For releases we should build LLVM ourselves or use
Chromium's own published toolchain, so we don't ship binaries built with a compiler we can't vouch
for.

CI cannot use standard hosted runners (time and disk limits). Builds run on a per-release EC2 Mac
host that `scripts/macos-host.sh` brings up, with an sccache compiler cache (see `AGENTS.md`).

## Things the base patch set changes that we must undo

ungoogled-chromium removes every Google endpoint. We keep three on purpose:

1. **Safe Browsing, standard mode.** ungoogled removes it in four layers; we undo each:

   | ungoogled layer | Our counter-measure |
   |---|---|
   | `safe_browsing_mode=0` in `flags.gn` | `safe_browsing_mode = 1` in `args/common.gn` |
   | 3 build-without-Safe-Browsing patches (core ×2, macOS ×1) | skipped (`patches/ungoogled-exclude.txt`). The macOS one also removes Screen AI / translation sandbox code that `disable-ai.patch` needs gone; that hunk is carried as `lb/macos-no-ai-sandbox-params.patch` |
   | `remove-unused-preferences-fields.patch` (~6,000 lines: Safe Browsing + unused sign-in prefs) and `move-js-optimizer-unfamiliar-sites.patch` (only needed because of it) | skipped as a whole — cheaper to maintain than splitting it every release. Cost: some unused sign-in fields stay in the Preferences file; sign-in itself stays disabled |
   | `pruning.list` deletes `safe_browsing_prefs.{h,cc}` and `signin_pref_names.{h,cc}` | kept (`patches/prune-keep.txt`) |

   `remove-uneeded-ui.patch` is written on top of `remove-unused-preferences-fields.patch`, so
   without it one hunk needs fuzz, which GNU `patch` (Linux CI) rejects. We skip it too and carry
   it rebased onto our tree, split by layer: `lb/ungoogled-remove-uneeded-ui.patch` and its `-ui`
   half. At 157.0.8103.78 the other 124 ungoogled patches apply cleanly without these six and the
   two in item 3.

   Then `patches/lb/safe-browsing-standard-only.patch` makes it *standard only*: enhanced protection
   and extended reporting are hard-disabled (no pref, policy or sync can turn them on), real-time
   hash-prefix lookups are off (local hash-prefix database only), and Settings hides both options.

   Settings shows only Standard / No protection. Its only traffic is
   `safebrowsing.googleapis.com/v4/threatListUpdates:fetch` (no component updater, no real-time
   relay). A fresh profile also downloads the ad blocker's filter lists from their maintainers a
   few seconds after startup (`easylist.to`, `ublockorigin.github.io`, `pgl.yoyo.org`;
   `docs/desktop-ui.md`).

   Limitations:
   - **No list data yet.** Google answers that request with `400` / "Permission denied" until an
     agreement with Google is in place (alongside the Widevine licence), so Safe Browsing is wired
     up but has no threat lists. The key will be passed at build time (`google_api_key` GN arg from
     a local, git-ignored file) and never committed.
   - **Standard-mode pings beyond hash prefixes are not audited.** Settings' own text says standard
     mode sends "URLs and bits of page content" for suspicious pages (client-side phishing
     detection), and Chrome also checks risky downloads with Google. With a key in place, audit
     these, disable what goes beyond hash-prefix lookups, and fix the Settings text.
2. **Widevine on demand.** The CDM arrives through Chromium's component updater from Google
   (`enable_widevine = true` in `args/common.gn`). ungoogled's domain substitution breaks those
   URLs, so the updater paths are excluded from substitution
   (`patches/domsub-exclude.txt`). Not built yet: a patch that restricts the updater to Widevine
   only, after user consent.
3. **Extensions from the Chrome Web Store, updated without pings.** ungoogled keeps extensions
   but cuts them off from the store; we undo that and keep Manifest V2 off:

   | ungoogled layer | Our counter-measure |
   |---|---|
   | Domain substitution of the store's addresses, its update URL, which pages get `chrome.webstorePrivate`, which requests extensions may not see, the namespace of the update manifests `ExtensionDownloader` reads, and the update server store extensions check (`update.googleapis.com`) | `extension_urls.cc`, `_api_features.json`, `web_request_permissions.cc`, `safe_manifest_parser.cc` and `component_updater_url_constants.cc` are excluded (`patches/domsub-exclude.txt`) |
   | `disable-webstore-urls.patch`: no update checks, no process isolation for the store, store extensions treated as off-store | skipped (`patches/ungoogled-exclude.txt`) |
   | `0005-disable-default-extensions.patch` empties the store installer's download and install steps | put back by `lb/web-store-install.patch` (the rest of that patch stays) |
   | `remove-uneeded-ui.patch` removes every link to the store | `lb/web-store-links.patch` restores "Visit Chrome Web Store" (app menu → Extensions) and the store links on `chrome://extensions`; the empty list's link takes the store address from `extension_urls`, like the sidebar's |
   | `extensions-manifestv2.patch` keeps MV2 extensions running | skipped: MV3 only, as upstream |

   Updates: store extensions update through `update_client` from `update.googleapis.com`; those
   installed by policy, by another program or from a self-hosted server through
   `ExtensionDownloader`. ungoogled's `block-requests.patch` stops every `update_client` check;
   `lb/extension-updates.patch` lets extensions' checks (product id CRX) through, while the
   component updater's stay stopped, and takes out the pings, the install id and the hardware
   fields. `ExtensionDownloader` sends no pings because its ping domain stays domain-substituted.
   What a check carries is listed in `docs/design.md` §5. `lb/web-store-no-referrer-chain.patch`
   keeps Safe Browsing's referrer chain from the store page.

   Checked in the built browser with `--lb-sidebar-demo=web-store`, `=extension-update`,
   `=update-manifest`, `=mv2-off` and `=store-links` (`lb/README.md`). In `lb_unittests`,
   `WebStoreTest` fails if the store's addresses or update server get substituted, and
   `ExtensionUpdateCheckTest` runs an update check against a local server: it fails if a check
   carries a ping, an install id or hardware fields, or if a check that isn't an extension's
   goes out. `fetch.sh` stops if a line of `patches/domsub-exclude.txt` matches no file.

`patches/domsub-exclude.txt` is broader than strictly needed (it also keeps endpoints that other
patches switch off). Narrow it once a network audit shows which hosts the built browser contacts.

## Branding

Done by scripts at the end of `fetch.sh`, not by patches — they survive Chromium updates unchanged:

- `branding/BRANDING` → product name, company, copyright, macOS bundle ID (`app.littlebrowser.LittleBrowser`).
- `rebrand_strings.py` replaces "Chromium" (not "Chromium OS", "The Chromium Authors" or URLs) in
  `chrome/app/chromium_strings.grd` and `components/components_chromium_strings.grd` plus their
  `.grdp` parts. Translations are keyed by a fingerprint of the English text, so it recomputes the
  fingerprints with Chromium's own grit and re-keys every translation — otherwise all locales would
  silently fall back to English. (157.0.8103.78: 967 replacements, 591 messages, ~44,000
  translations in 81 languages.)
- `brand_assets.sh` copies the icons exported by the repo-root `branding/` (run `npm run export` there after
  changing the SVG masters) and compiles the macOS asset catalog with Xcode's `actool`.

Strings outside those two files that mention Chromium (e.g. some developer-facing pages) are not
rebranded.

## Platform patches

ungoogled-chromium's core repo is not enough to build on its own; each platform repo adds build
patches and steps. **macOS is wired up** (`ungoogled-chromium-macos`, its series applied after the
core series, minus `ungoogled-exclude.txt`). Windows (`ungoogled-chromium-windows`) and Linux
(`ungoogled-chromium-portablelinux`) are still to add.
