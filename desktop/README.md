# Desktop (macOS, Windows, Linux) — Chromium fork

Upstream Chromium **stable** + **ungoogled-chromium** patch set + our patches and source tree.
See [docs/design.md §4.1](../docs/design.md#41-desktop-how-we-fork-chromium).

## Layout

| Path | What |
|---|---|
| `VERSIONS` | Pinned Chromium version and ungoogled-chromium tag |
| `args/` | GN build args: `common.gn` + one per platform, applied after ungoogled's `flags.gn` |
| `patches/series` | Our Chromium patches, in order, one concern each |
| `patches/ungoogled-exclude.txt` | ungoogled patches we skip (Safe Browsing and the Chrome Web Store are kept, MV2 is off) |
| `patches/domsub-exclude.txt` | Paths kept out of domain substitution (Safe Browsing, Widevine, the Chrome Web Store) |
| `branding/BRANDING` | Product name, company, copyright, bundle id |
| `lb/` | Our own code, linked into the tree as `src/lb` |
| `scripts/fetch.sh` | Download + prepare the source tree into `build/src` |
| `scripts/rebrand_strings.py` | Rename the product in Chromium's branded strings and re-key all 81 translations |
| `scripts/brand_assets.sh` | Install icons from the repo's `branding/exports` (macOS `app.icns` + `Assets.car` via `actool`, product logos) |
| `scripts/build.sh` | Generate args and build with GN + Ninja (`--dev` for the fast dev build) |
| `scripts/check_patches.py` | Check that our patch series applies to a clean tree (about a minute, no re-fetch); see `AGENTS.md` |
| `scripts/run.sh` | Launch a build with a separate developer profile |
| `scripts/upstream.sh`, `release-bot.sh`, `macos-host.sh` | Release automation (CI); see `AGENTS.md` |

`build/` and `third_party/` are git-ignored.

## Prerequisites (macOS, the only supported platform)

- ~100 GB free disk, 16 GB+ RAM (32 GB+ comfortable)
- Xcode with the Metal toolchain: `xcodebuild -downloadComponent MetalToolchain` if
  `xcrun metal --version` fails (≈840 MB from Apple)
- A macOS SDK the pinned LLVM can read: `build.sh` picks the newest one without the `arm64e.x1`
  target (on this setup the Command Line Tools' 26.5 SDK); override with `LB_MAC_SDK=/path/to/sdk`
- Homebrew tools:

```bash
brew install ninja coreutils esbuild typescript go
```

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

Ad-hoc signed `.dmg` of the release build (`build/LittleBrowser-macos-<arch>.dmg`, the app next to
an Applications link), through `scripts/package-macos.sh`:

```bash
desktop/scripts/build.sh --package
```

Releases use `scripts/package-macos.sh --release` in CI: a Developer ID signature with the
hardened runtime, notarised and stapled, with the certificate and an App Store Connect API key read from
AWS Secrets Manager (`little-browser/signing/apple-developer-id` and `apple-notarization`) by the
release builder. While those secrets are empty it falls back to an ad-hoc signature. An ad-hoc
signature carries no signing options: the hardened runtime's library validation needs a Team ID.

Local builds are ad-hoc signed and the signature changes with every rebuild, so macOS asks for
the "Chromium Safe Storage" keychain item each time ("Always Allow" is tied to the signature).
`run.sh` passes `--use-mock-keychain`; releases need Developer ID signing, and the keychain item
is still named "Chromium Safe Storage" (not renamed yet).

| Build | Folder | Use for |
|---|---|---|
| Release | `out/Default` | packaging, performance/memory measurements, final checks |
| Dev | `out/Dev` | day-to-day feature work (`args/dev.gn`) |

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

   At 157.0.8103.78 the other 125 ungoogled patches apply cleanly without these five and the two
   in item 3.

   Then `patches/lb/safe-browsing-standard-only.patch` makes it *standard only*: enhanced protection
   and extended reporting are hard-disabled (no pref, policy or sync can turn them on), real-time
   hash-prefix lookups are off (local hash-prefix database only), and Settings hides both options.

   Settings shows only Standard / No protection. On a local build, a fresh profile left running
   5½ minutes contacted exactly one host, `safebrowsing.googleapis.com/v4/threatListUpdates:fetch`,
   and nothing else (no component updater, no real-time relay).

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
