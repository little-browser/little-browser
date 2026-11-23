# AGENTS.md

This file guides AI coding agents (Claude Code, Codex, Cursor and others) working in this
repository. Directories with their own rules have their own `AGENTS.md`: `desktop/`,
`desktop/lb/` and `core/`.

## What this is

Little Browser: a minimal, private browser with an Arc-like sidebar. It is a Chromium fork built on
ungoogled-chromium for desktop and Android, and uses WebKit on iOS. The macOS desktop build runs;
Windows, Linux, iOS and Android don't exist yet.

| Path | What |
|---|---|
| `desktop/` | Chromium fork tooling: version pins, GN args, patch series, branding, scripts |
| `desktop/lb/` | Our own C++ code, linked into the Chromium tree as `src/lb` |
| `core/` | `lb-core`, the shared Rust library (data model, sync, import readers; later the blocker) |
| `docs/` | `design.md` (architecture, privacy, sync, release pipeline), `ux.md` (UX spec), `desktop-ui.md` (sidebar internals), `legal/` |
| `branding/`, `site/` | Logo masters and icon export; the static website |
| `ios/`, `android/` | Placeholders |

## Before you change things

- **Read the spec first.** Product behaviour is specified in `docs/ux.md`, and architecture in
  `docs/design.md` and `docs/desktop-ui.md`. If code and docs disagree, find out which is right and
  fix the other in the same change.
- **Docs and code describe the present.** No decision logs, history, "used to", dated notes or
  spike write-ups. When a decision changes, rewrite the text so it states what is true now; git
  history keeps the past.
- **Keep the feature set small.** Check `docs/design.md` and `docs/ux.md` before adding a feature.
  For optional features, the answer is usually "an extension can do it".

## Checks (the same ones CI runs, in `.gitlab-ci.yml`)

```bash
cd core && cargo fmt --check && cargo clippy --all-targets -- -D warnings && cargo test
core/scripts/coverage.sh                  # gate: lb-core line coverage of at least 95%
desktop/scripts/check_lb_tests.py         # gate: unit tests for //lb:model and //lb:page_names
shellcheck desktop/scripts/*.sh core/scripts/*.sh
```

Chromium builds are not part of the per-push checks; see `desktop/AGENTS.md`.

## Commits

- Sign off every commit with `git commit -s` (DCO, see `CONTRIBUTING.md`).
- New source files start with the MPL-2.0 header (text in `CONTRIBUTING.md`).
- One concern per commit. Say what changed and why.
- Don't push unless asked.

## Good practice here

- **Reuse before writing.** In Chromium code, use what Chromium already has, such as prefs, session
  service, `TemplateURLService` and views controls, before adding our own. In our code, check
  `desktop/lb/` and `lb-core` for an existing helper before adding one. Extract a shared helper the
  second time the same pattern appears, not the fourth.
- **Test everything you build.** Every feature and every change, finished or still in progress,
  comes with tests that fail if it breaks: each behaviour and its edge cases, and for a bug fix a
  test that fails without the fix (run it against the old code to be sure). Untested work isn't
  done. The CI gates above check this; meet them with tests, never by lowering a threshold.
- **Testable logic.** Data-model logic belongs in `lb-core` (Rust, unit tested) or in plain C++ in
  `desktop/lb/` with no dependency on views, so it can be tested without a browser window. A
  decision made in a view or a Chromium hook (what a close, click or drag does) moves into
  `//lb:model` as a plain function over a small state struct, with tests for each case, and the
  hook only gathers the state and acts on the answer. Behaviour that only a browser window can
  show gets a `--lb-sidebar-demo` mode whose log says whether it passed (`desktop/lb/AGENTS.md`).
- **Verify, then report honestly.** Say what you actually checked (built, ran, captured a
  screenshot, read a log). Say what you couldn't check. Never claim a UI interaction works
  without having driven it.

## Shell gotchas (macOS, zsh)

- zsh doesn't split unquoted variables into words. Use `${=VAR}` or an array to pass a list of
  files to a command.
- An unmatched glob is an error in zsh, so quote patterns like `'--include=*.cc'`.
- Guard `rm` on variable paths: `rm -rf "${DIR:?}"`.
