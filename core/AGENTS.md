# core/ — lb-core (Rust)

The shared library for every platform: data model, sync, the readers for importing from other
browsers, and the element-hiding and tracking-parameter rules. Background: `docs/design.md` §4.4,
§5, §7.6 and §8.

```bash
cargo fmt --check && cargo clippy --all-targets -- -D warnings && cargo test   # what CI runs
cargo test -p lb-core model::tests::<name>                                     # one test
scripts/coverage.sh       # CI gate: line coverage of at least 95% (LB_MIN_LINE_COVERAGE)
```

`coverage.sh` needs rustup's `llvm-tools` component, or `LB_LLVM_BIN` set to a directory with
`llvm-cov` and `llvm-profdata` of rustc's LLVM version (the Chromium tree's
`third_party/llvm-build/Release+Asserts/bin` works).

- **Tests.** They sit in a `#[cfg(test)] mod tests` block at the end of each module. Every
  behaviour change comes with a test, including edge cases (empty lists, unknown ids,
  ordering).
- **Model.** `model.rs` mirrors `docs/ux.md` §5 and §6: profiles, browser-wide spaces,
  pinned and today tabs, and auto-archive. Change the spec and the model
  together.
- **Sync.** `sync.rs` has the `Transport` trait plus an in-memory transport for tests. Real
  transports implement the trait; they don't change callers.
- **Import.** `import/` mirrors `docs/ux.md` §13: it parses other browsers' files from their text
  and their SQLite rows as plain structs, and decides what comes along and where it goes. It does
  no file or SQLite access, so tests use inline strings; never put real browser data in a test.
- **Element hiding.** `element_hiding.rs` parses the filter lists' CSS hiding rules and their
  exceptions and answers which selectors a page on a site gets: its site's rules and the generic
  rules with no class or id first up front, the other generic rules by the names the page uses.
  Only plain CSS: never accept a rule that needs script or could break out of its CSS rule.
- **Tracking parameters.** `tracking_params.rs` is the built-in list of parameters that only
  track and strips them from an address (`docs/design.md` §5). Add a parameter only if it never
  changes what a page shows: there is no per-site exception.
- **Dependencies.** Only `serde_json`, pinned to the version and features Chromium vendors,
  because the desktop build compiles the core with Chromium's crates. Any other crate (uniffi,
  adblock-rust, rusqlite, a CRDT library, iroh) needs a justification in `docs/design.md` before
  it goes into `Cargo.toml`, and for desktop it must be one Chromium vendors.
- **Bindings.** Desktop links lb-core through `desktop/lb/core_bridge` (a `cxx` bridge, built with
  Chromium's toolchain, which also runs these tests as `lb_core_unittests`). A new `.rs` file
  goes in the `lb_core` target's sources in `desktop/lb/BUILD.gn`. Keep the public API small and
  easy to bind: plain data types, no lifetimes in public signatures.
