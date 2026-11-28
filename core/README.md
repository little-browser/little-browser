# core — `lb-core` (Rust)

Shared library for all platforms: data model, the browser's rules, sync and import.
See [docs/design.md §4.4](../docs/design.md#44-rust-core),
[§5](../docs/design.md#5-privacy-and-security-baseline), [§7.6](../docs/design.md#76-import) and
[§8](../docs/design.md#8-sync-without-storing-user-data).

```bash
cd core && cargo test
```

Contents: the data model (profiles, spaces, pinned/today tabs,
auto-archive; see [docs/ux.md §5, §6](../docs/ux.md)) and the sync `Transport` trait with an
in-memory transport; the rules every shell shares: spaces (`spaces.rs`: a new space's defaults,
colours, which space a window opens on, saved tab order), the command bar's ranking
(`command_bar.rs`), the ad blocker's filter lists (`adblock.rs`: which lists, when to download
them, `!#if` blocks, uBlock Origin's rule syntax), their element-hiding rules matched to pages
(`element_hiding.rs`), the built-in list of tracking parameters (`tracking_params.rs`), tabs (`tabs.rs`: what closing a
pinned tab does, the tab to show, restore order, tab names and pinned links), pinned groups
(`pinned_groups.rs`), and the search engine and password manager choices (`search_choice.rs`,
`password_manager.rs`); the import readers for Chrome-family browsers and Arc
([docs/ux.md §13](../docs/ux.md)). One dependency, `serde_json`.

Not built yet: the CRDT layer, a P2P transport, the conversion of filter lists to WebKit's
content-blocker format for iOS, and the UniFFI bindings.
