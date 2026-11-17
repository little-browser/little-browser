# Little Browser

A minimal, private, fast browser — Chromium on macOS, Windows, Linux and Android; WebKit on iOS.
Sidebar-first UI inspired by Arc. Open source under [MPL-2.0](LICENSE).

> **Status:** the macOS desktop build runs: sidebar with spaces and favourites, command bar,
> first-run search-engine choice, Safe Browsing in standard mode. Windows, Linux, iOS and Android
> don't exist yet. Sync, the native content blocker, Peek and the little window are designed but
> not built. There are no public releases yet.

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
| `ios/`, `android/` | Placeholders |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security issues: [SECURITY.md](SECURITY.md).
