# Little Browser

A minimal, private, fast browser — Chromium on macOS, Windows, Linux and Android; WebKit on iOS.
Sidebar-first UI inspired by Arc, which is being discontinued: spaces, pinned tabs, tab groups,
Peek link previews and a command bar, and it imports your Arc spaces and tabs. See
[littlebrowser.app/arc-alternative](https://littlebrowser.app/arc-alternative). Open source under
[MPL-2.0](LICENSE).

> **Status:** the macOS desktop build runs: sidebar with spaces, pinned tabs and groups, command bar,
> first-run search-engine and password-manager choices, Chrome Web Store extensions (MV3), the
> built-in ad blocker (network filtering and element hiding), tracking-parameter stripping, Peek,
> the little window, Safe Browsing in standard mode. Android builds Chromium's own app with our
> privacy patches and app ID, with none of our features yet. Windows, Linux and iOS don't exist
> yet. Sync is designed but not built. Little Browser is in beta, for Macs with Apple silicon
> ([downloads](https://littlebrowser.app/downloads)); the one build published so far predates
> extensions, the password-manager choice, the ad blocker, Peek and the little window.

## Documents

- [Design](docs/design.md) — architecture, privacy, sync, build and release pipeline
- [Screen layout & interaction](docs/ux.md) — the UX specification
- [Desktop UI](docs/desktop-ui.md) — how the sidebar is built on Chromium's Views
- [Widevine](docs/legal/widevine.md) and [Safe Browsing](docs/legal/safe-browsing.md) — the Google licences and access we need

## Repository layout

| Path | What |
|---|---|
| `docs/` | Design, UX and legal documents |
| `desktop/` | Chromium fork: version pins, build args, patch series, branding, scripts, our source tree (`lb/`) |
| `core/` | `lb-core`, the shared Rust library (data model, sync transport trait) |
| `branding/` | Logo masters and the icon export |
| `site/` | The static website |
| `android/` | Android build of the Chromium fork: GN args, privacy patches, fetch/build scripts |
| `ios/` | Placeholder |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security issues: [SECURITY.md](SECURITY.md).
