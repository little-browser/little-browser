# core — `lb-core` (Rust)

Shared library for all platforms: data model, sync and import (the content blocker is not built yet).
See [docs/design.md §4.4](../docs/design.md#44-rust-core), [§7.6](../docs/design.md#76-import) and [§8](../docs/design.md#8-sync-without-storing-user-data).

```bash
cd core && cargo test
```

Contents: the data model (profiles, spaces, pinned/today tabs,
auto-archive; see [docs/ux.md §5, §6](../docs/ux.md)) and the sync `Transport` trait with an
in-memory transport; the import readers for Chrome-family browsers and Arc
([docs/ux.md §13](../docs/ux.md)). One dependency, `serde_json`.

Not built yet: the CRDT layer, a P2P transport, the adblock-rust integration, the content
blocker, and the UniFFI bindings.
