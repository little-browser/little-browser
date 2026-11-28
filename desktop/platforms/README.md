# Desktop platforms

`scripts/fetch.sh` and `scripts/build.sh` do the steps every desktop platform shares. Each
platform is one entry here, `<name>.sh`, that they source after `VERSIONS` and
`scripts/lib.sh`; `--platform <name>` picks it, and the default is this machine's (`mac` on
macOS, `linux` on Linux, `win` on Windows). The entries are macOS (`mac.sh`) so far.

## What an entry defines

`load_platform` (`scripts/lib.sh`) stops with the missing name when an entry lacks one of these.

| Variable | What |
|---|---|
| `PLATFORM_LABEL` | Name in messages, e.g. `macOS` |
| `PLATFORM_HOST` | `uname -s` of the machine that builds it (`--patches-only` runs anywhere) |
| `PLATFORM_ARCHS` | Architectures it builds: `arm64`, `x64` (the names used everywhere; `check_arch` takes no other spelling) |
| `PLATFORM_REPO`, `PLATFORM_REPO_URL`, `PLATFORM_TAG` | Its ungoogled-chromium platform repo, cloned into `third_party/<repo>` at the tag pinned in `VERSIONS` |
| `PLATFORM_FLAGS` | That repo's GN flags file, applied after ungoogled's `flags.gn` |
| `PLATFORM_GN`, `PLATFORM_NINJA` | The gn and ninja commands, relative to `build/src` or on `PATH` |
| `PLATFORM_TARGETS` | What `build.sh` builds (`chrome`) |

| Function | Called | What |
|---|---|---|
| `platform_fetch_source <arch>` | fetch, step 2 | Unpack the pinned Chromium tarball into `$SRC` |
| `platform_toolchains` | fetch, step 5, and `fetch.sh --toolchains-only` | The compilers and tools the build uses, for this machine, able to build every architecture in `PLATFORM_ARCHS` (macOS: the host's toolchain and the other architecture's Rust standard library) |
| `platform_branding <src>` | fetch, step 6 | The platform's own icons, after the shared ones (`scripts/brand_assets.sh`) |
| `platform_prebuild <out dir>` | build, in `$SRC`, after `args.gn` | Platform args (macOS: `mac_sdk_path`) and tools built once (macOS: gn, bindgen, Crubit) |
| `platform_report <out dir>` | build, at the end | Print what was built |
| `platform_package [--arch <arch>]` | `build.sh --package [<arch>]` | Package the release build of that architecture (default: this machine's) |

Entries see `DESKTOP`, `UG` (ungoogled-chromium), `PU` (their own repo), `BUILD`, `SRC` and the
helpers in `scripts/lib.sh` (`die`, `log`, …).

## Adding a platform

1. `<name>.sh` here, with everything above.
2. `../args/<name>.gn`, applied after `args/common.gn`.
3. Its repo's tag in `../VERSIONS`. The release bot (`scripts/upstream.sh`, `release-bot.sh`)
   watches the macOS repo's tags only: with a second platform it has to propose a bump once every
   platform's repo has a tag for the new Chromium version, and set all of them.
4. Its own patches tagged `[<name>]` in `../patches/series`; `scripts/check_series.py` then
   requires the tag on any patch that touches its files, and `patch-check` checks its series.
5. A CI build job. A Linux-hosted one extends `.linux-chromium` (x64, merge requests) or
   `.linux-chromium-release` (protected refs), or their `-arm64` twins on the Graviton runners, in
   `.gitlab-ci.yml`: the build runners, a fresh `ubuntu:24.04` with sccache
   (`scripts/linux-build-host.sh`), and the compiler cache in S3, as the Android build uses them.

`scripts/test_lib_sh.py` loads every entry here and checks it is complete and has its args file.
