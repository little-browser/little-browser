# chromium/ — shared by every Chromium platform

What the desktop builds (macOS, later Windows and Linux) and Android share. How the layers fit
together and what goes where: [docs/android-ui.md](../docs/android-ui.md) ("What is shared, and
where").

| Path | What |
|---|---|
| `branding/BRANDING` | The product's name, company and copyright in Chromium's own format, which each fetch script copies into its tree and reads with `branding_value` (`scripts/lib.sh`) |
| `VERSIONS` | The pinned Chromium version, and the ungoogled-chromium tags the desktop builds on |
| `lb/` | Our own code, linked into the Chromium tree as `src/lb` (`lb/README.md`, `lb/AGENTS.md`) |
| `patches/series` | Engine patches for features every Chromium platform has, in order; applied before the platform's own series. Engine code only: view halves are in `desktop/patches`, Java in `android/patches` (`desktop/patches/README.md`) |
| `scripts/lib.sh` | Helpers of the desktop and Android fetch and build scripts: clones, the series for a platform, GN args, gn + ninja, sccache, the CI build log, and the desktop platform-entry loader; tested by `test_lib_sh.py` |
| `scripts/rebrand_strings.py` | Renames the product in Chromium's string files and re-keys their translations; the desktop renames "Chromium", Android also "Chrome" (`--chrome`); tested by `test_rebrand_strings.py` |
| `scripts/check_series.py` | CI gate: the shared, desktop and Android series' layers and platform tags |
| `scripts/check_patches.py` | The shared and desktop series apply to a clean desktop tree, no fuzz or offset (about a minute, no re-fetch; `desktop/AGENTS.md`) |
| `scripts/check_lb_tests.py` | CI gate: every source of `lb/`'s tested targets has unit tests, and `lb_core` lists every lb-core file |

Both builds link `lb/` and apply `patches/series` before their own series. Android builds
`lb/`'s targets for every platform (`:engine`, `:prefs`, `:browser`, `:renderer` and lb-core);
`:model` and `lb_unittests` are desktop-only for now (`lb/AGENTS.md`).
