# core/ — lb-core (Rust)

The shared library for every platform: data model and sync (the content blocker and
importers are not built yet). Background: `docs/design.md` §4.4 and §8.

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
- **Dependencies.** There are none, on purpose. The crates not built yet (uniffi, cxx,
  adblock-rust, rusqlite, a CRDT library, iroh) each need a justification in `docs/design.md`
  before they go into `Cargo.toml`.
- **Bindings.** The desktop C++ code isn't linked to `lb-core`. Keep the public API small and
  easy to bind: plain data types, no lifetimes in public signatures.
