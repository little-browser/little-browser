# core — `lb-core` (Rust)

Shared library for all platforms: data model and sync (the content blocker and importers are not built yet).
See [docs/design.md §4.4](../docs/design.md#44-rust-core) and [§8](../docs/design.md#8-sync-without-storing-user-data).

```bash
cd core && cargo test
```

Contents: the data model (profiles, spaces, favourites per profile, pinned/today tabs,
auto-archive; see [docs/ux.md §5, §6](../docs/ux.md)) and the sync `Transport` trait with an
in-memory transport. No dependencies.

Not built yet: the CRDT layer, a P2P transport, the adblock-rust integration, the content
blocker and importers, and the UniFFI/cxx bindings.
