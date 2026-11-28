# desktop/ — the Chromium fork

`README.md` here covers prerequisites, the Safe Browsing and Widevine exceptions, and branding.
This file covers how to work in the fork.

## How the source tree is made

`scripts/fetch.sh` builds `build/src` (git-ignored, about 100 GB) in this order:

1. the Chromium tarball pinned in `VERSIONS`;
2. ungoogled-chromium's core patches, then its platform patches (macOS: ungoogled-chromium-macos),
   minus `patches/ungoogled-exclude.txt`;
3. our patch series, `patches/series`, without the patches tagged for another platform;
4. domain substitution, which rewrites Google domains to `*.qjz9zk` in listed files, minus
   `patches/domsub-exclude.txt`;
5. the platform's toolchains (macOS: LLVM, Rust, Node.js, Crubit), for this machine, with the
   other architecture's Rust standard library so the tree builds both (`build.sh x64`);
   `fetch.sh --toolchains-only` redoes this step in an existing tree;
6. branding: `rebrand_strings.py`, `brand_assets.sh` and the platform's own icons;
7. symlinks `src/lb` → `desktop/lb` and `src/lb_core` → `core/lb-core`.

`fetch.sh` and `build.sh` do the steps every platform shares; what differs per platform (its
ungoogled-chromium repo, the tarball and toolchain download, its icons, the tools it builds before
GN, what it packages) is one file, `platforms/<name>.sh`, plus `args/<name>.gn`.
`platforms/README.md` lists what an entry defines and how to add one.

So **our patches apply before domain substitution**. A patch must be made against the text as it
is before substitution, not against what you see in `build/src`.

## Changing Chromium code

Follow the minimal-patch rule in `CONTRIBUTING.md`, in this order: build flag (`args/`), default
pref or feature flag, hiding in the UI, and only then a patch. Our features live in `lb/`. Patches
only hook them in.

Workflow for a patch:

1. **Get the clean base.** Run `scripts/check_patches.py --keep "$T"`: `$T/base/<file>` is each
   file our series touches, exactly as `fetch.sh` sees it just before our patches (Chromium
   tarball plus ungoogled-chromium's patches). For a file no patch touches yet, the base is the
   file in `build/src` if `grep -c qjz9zk` finds nothing there; otherwise get it from the tarball
   and ungoogled's patches the same way. Never use `build/domsubcache.tar.gz`: its `orig/` copies
   were saved after our series ran, so they already contain our patches.
2. **Edit a copy and diff it against the base.** Write the patch with
   `diff -u --label a/<file> --label b/<file> "$T/base/<file>" <your copy>` (or keep the history
   in a scratch git repo such as `build/minitree`, committing the base as `base: …`).
3. **Write the patch.** Save it as `patches/lb/<name>.patch`, starting with `#` comment lines
   that explain why the patch exists. One concern per patch, and one layer: engine code and view
   code (Views, Cocoa, WebUI) go in separate patches, `<name>.patch` and `<name>-ui.patch`, so
   another platform can take the engine half (`patches/README.md`).
4. **Add it to the series.** List it in `patches/series` with a one-line reason, starting with
   `[mac]` (or another platform's tag) when it is for one platform only; a patch that touches a
   platform's own files (`.mm`, `*_mac.*`, `cocoa/`) must be. Order matters: a patch must come
   after any patch that touches the same file, and is then diffed from the state after that patch
   (`$T/patched` holds every file after the whole series). `scripts/check_series.py` checks the
   layers and tags on every push.
5. **Check it applies to a clean tree:** `scripts/check_patches.py` (`--platform <name>` for
   another platform's series). It fails on any rejected
   hunk, fuzz or offset. `git apply --check -R` against `build/src` is not enough: `build/src`
   already contains every patch, so a patch made from the wrong base passes it.
6. **Mirror the change into `build/src`.** Copy the patched file across, or edit it in place if it
   contains `qjz9zk`, so that you don't undo the substitution. A clean `fetch.sh` remains the
   final test before a release.

A new `.cc` or `.h` file under `lb/` has to be registered. View-free logic and its tests go in
`lb/BUILD.gn` (`//lb:model`, `lb_unittests`), with no patch needed. View code goes in the sources
list in `patches/lb/sidebar-hooks.patch` (the `vertical_tab_strip_region_view` target); regenerate
that patch afterwards. Code that runs in a page's renderer process goes in `lb/BUILD.gn`'s
`//lb:renderer`, which `chrome/renderer` depends on (`patches/lb/element-hiding.patch`), and talks
to the browser over a Mojo interface in `lb/common` (`//lb:mojom`). View-free logic both sides
share goes in `lb/common` too, in a tested target such as `//lb:page_names`.

## Build

```bash
scripts/build.sh --dev            # out/Dev: component build; a one-file edit rebuilds in ~30 s
scripts/build.sh                  # out/Default: release; for packaging and performance numbers
scripts/build.sh x64              # another architecture than this Mac's: out/Default-x64
LB_NINJA_FLAGS="-k 0" scripts/build.sh --dev   # keep going and collect every error in one pass
```

Changes to `BUILD.gn` or `.grd`/`.grdp` files take minutes, not seconds.

**Compiler cache.** `LB_CC_WRAPPER=sccache scripts/build.sh …` sets GN `cc_wrapper`, so
compiles are cached by file content and still hit after `fetch.sh` recreates the tree. Configure
the cache with sccache's own variables (`SCCACHE_DIR`, `SCCACHE_CACHE_SIZE`, `SCCACHE_BUCKET` for
S3). Turning it on or off changes every compile command, so the next build recompiles everything
once. Machines that share a cache need the same checkout path, or the keys won't match.

## Release automation

- `scripts/upstream.sh` says whether ungoogled-chromium has a release newer than `VERSIONS`;
  `--write` updates `VERSIONS`. Safe to run locally.
- `scripts/fetch.sh --patches-only` stops after the patches, on any OS. CI runs it on merge requests
  that touch `VERSIONS` or `patches/`.
- `scripts/release-bot.sh bump|tag` is CI-only (it pushes as the bot); `LB_DRY_RUN=1` prints the
  push instead.
- `scripts/macos-host.sh up` is CI-only: it allocates or reuses the per-release EC2 Mac
  host, starts the builder and, if one is configured, attaches its persistent data volume. The infrastructure behind it lives in
  the infrastructure repository.
- `scripts/package-macos.sh [--arch <arch>] [--release] [<version>]` signs out/Default's app
  (out/Default-<arch> for the other architecture) and makes `LittleBrowser-macos-<arch>.dmg`,
  `<arch>` being `arm64` or `x64`; the app reads its own architecture's update feed,
  `updates/macos-<arch>.xml`; `--release` (CI) uses the Developer ID from Secrets Manager and notarises. `build.sh
  --package` runs it without `--release` (ad-hoc). With a version and an update key it also adds
  the updater (Sparkle) and writes the `.dmg`'s update signature to a `.update.json`; to try an
  update locally, give it `LB_UPDATE_KEY_FILE` and `LB_UPDATE_FEED_URL` (see its header).
- `scripts/release_notes.py notes …` prints a release's notes from the commits' `Release-note:`
  lines, and `markdown …` a release in `releases.json` as Markdown; safe to run locally.
- `scripts/publish-release.sh <version> <LittleBrowser-macos-<arch>.dmg>` and `scripts/deploy-site.sh` are CI-only:
  they put a release on downloads.littlebrowser.app (in `releases.json` and the update feed,
  written by `scripts/update_feed.py`) and upload `site/` to littlebrowser.app.
  `scripts/download-stats.sh` (CI-only, daily) counts each file's downloads and updates from the
  downloads host's logs (`scripts/download_stats.py`) into `stats.json`. `LB_DRY_RUN=1` prints
  what they would do. The Python scripts' tests: `python3 -I -m unittest discover -s scripts`.

## Run and verify

```bash
scripts/run.sh --dev              # dev profile in build/dev-profile-dev
scripts/run.sh --dev --fresh      # move that profile aside first and start clean
```

For scripted checks, launch the binary directly with a throwaway profile:

```bash
APP="build/src/out/Dev/Little Browser.app/Contents/MacOS/Little Browser"
"$APP" --user-data-dir="$T/p" --use-mock-keychain [--no-first-run] [--lb-ui-snapshot=$T/x.png]
```

- **Always pass `--use-mock-keychain`.** Each rebuild gets a new ad-hoc signature, and macOS
  would otherwise ask for keychain access every time.
- **Turn off the quit warning first.** "Warn Before Quitting" is on by default, so a scripted
  quit stops at a dialog. Write `{"browser":{"confirm_to_quit":false}}` to `$T/p/Local State`
  before the first launch.
- **Test both first-run paths.** `--no-first-run` skips first-run code: the welcome tab, the
  first-run profile naming and the default-browser prompt. Run without it as well when a change
  touches startup.
- **AppleScript quit.** Address the app by bundle id,
  `tell application id "app.littlebrowser.LittleBrowser" to quit`, not by path.
- **Snapshots.** `--lb-ui-snapshot*` capture a PNG and then quit the app. `--lb-sidebar-demo=…`
  drives sidebar states for them (see `lb/README.md`).
- **Logs.** `--enable-logging=stderr --vmodule=<file>=1` shows `VLOG(1)` output. The
  `objc[…]: Class … is implemented in both` lines are harmless; they come from the component
  build.
- **Start pages.** Most `chrome://` URLs passed on the command line are ignored. Reach them
  through first run or the UI.
- **No keystrokes.** Keystroke and click automation usually isn't permitted on this machine. Say
  what you couldn't drive instead of assuming it works.

User data lives in `~/Library/Application Support/Little Browser`. Dev runs use
`build/dev-profile-dev` instead.
