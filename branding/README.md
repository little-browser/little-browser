# Little Browser brand

The logo is **Grow**: a little page opening out of the point you clicked. The white card is the page,
the dot is the Shift+click, and the fading outlines are the Peek card opening (docs/ux.md §7). It
stands for Little Browser's small, short-lived pages, and it does not resemble other browser logos.

## Files

| File | Use |
|---|---|
| `svg/icon.svg` | Master app icon: rounded tile, full bleed, 512 grid. Windows, Linux, docs, web ≥ 48 px |
| `svg/icon-small.svg` | Same icon redrawn for 16–32 px: one heavier outline, bigger dot. Favicons, tab icons |
| `svg/icon-macos.svg`, `svg/icon-macos-small.svg` | macOS: Apple icon grid (824 px tile on a 1024 canvas) with drop shadow |
| `svg/icon-square.svg` | Opaque square with no rounding, for platforms that apply their own mask (iOS, Play Store) |
| `svg/icon-ios-dark.svg`, `svg/icon-ios-tinted.svg` | iOS 18 dark and tinted appearances |
| `svg/android-*.svg` | Android adaptive icon layers (108 dp canvas; the mark sits inside the 66 dp safe zone) |
| `svg/mark.svg`, `svg/mark-small.svg` | The mark alone, white on transparent (full and small-size drawing), for placing on a CSS tile |
| `svg/mark-template.svg` | Monochrome template (black + alpha only), small-size drawing: macOS menu bar, Linux symbolic icon, stamps, print |
| `svg/wordmark.svg`, `svg/wordmark-white.svg` | Word mark "little browser": custom monoline lowercase, drawn as strokes, no font needed |
| `svg/lockup.svg`, `svg/lockup-white.svg` | Icon + word mark, horizontal |
| `svg/social-preview.svg` | 1280×640 social preview: the mark and word mark in white on the tile gradient. GitHub repo settings and link cards |
| `svg/play-feature-graphic.svg` | 1024×500 Google Play feature graphic: the social preview's content, scaled and centred with wide margins (Play crops the edges) |
| `palette.css` | Colour tokens (below) |
| `scripts/export.mjs` | Exports every platform icon set into `exports/` |
| `scripts/sync-site.sh` | Copies the brand assets into the website (`site/assets/brand/`, `site/favicon.ico`) |
| `brand.html`, `brand.css` | Brand review page: the logo story, the icon at every size and on each platform, word mark, colour swatches, space hues, usage rules. Internal, not part of the website |

## Export

```bash
cd branding && npm install && npm run export
```

The script uses only Node and `@resvg/resvg-js`, so it runs on any OS. It wipes `exports/` and
writes:

- `macos/LittleBrowser.icns` (16–1024, small drawing at 16/32), `macos/icon-1024.png`, and
  `macos/MenuBarTemplate.png` + `@2x`.
- `windows/little-browser.ico` (16, 20, 24, 32, 40, 48, 64, 256).
- `linux/hicolor/<n>x<n>/apps/little-browser.png` (16–512), plus the scalable and symbolic SVGs.
- `android/res/mipmap-*/`: adaptive foreground, background and monochrome layers;
  `mipmap-anydpi-v26/ic_launcher(_round).xml`; legacy square and round icons;
  `res/drawable-*/`: the welcome screen's logo (`fre_product_logo`, 130 dp) and the lockup
  (`product_logo_name`, 21 dp tall), white in `drawable-night-*/`, under Chromium's names;
  `android/play-store-512.png`; `android/play-feature-graphic.png` (1024×500, no alpha).
- `ios/AppIcon.appiconset/`: a single 1024 px icon (RGB, no alpha) with dark and tinted appearances.
- `web/`: `favicon.ico`, `favicon.svg`, `apple-touch-icon.png`, `icon-192.png`, `icon-512.png`,
  `social-preview.png` (1280×640, no alpha).

Edit the SVG masters, never the exports, and commit both. The Chromium build picks up its icons
from its own branding directory; when the desktop build is ready, copy `exports/macos/`,
`exports/windows/` and `exports/linux/` there. Android's fetch copies `exports/android/` in
(`android/scripts/brand_assets.sh`). The product-name strings live in
`chromium/branding/BRANDING`.

Not covered yet: a macOS 26 Icon Composer `.icon` (Liquid Glass layers). Until there is one, macOS
26 shows the `.icns` as it is.

## Website

`site/` is the littlebrowser.app website, a plain static site with no build step. Every file in it
is served, so upload the folder as it is. It has a home page, `/downloads` (the button for the
visitor's system and every release, from `releases.json`) and `/privacy`, plus `404.html`,
`robots.txt`, `sitemap.xml` and `site.webmanifest`. There are no cookies or analytics. Pages other
than the home page use root-relative links, so preview the site from its own folder as well.

The brand review page (`branding/brand.html`) borrows the site's styles. It stays out of the
public site. At launch, add a slim `/press` page to the site instead, with logo downloads, a short
description, screenshots and the contact email.

```bash
python3 -m http.server 4173 --bind 127.0.0.1   # from the repo root, then open:
#   http://127.0.0.1:4173/site/                 the website
#   http://127.0.0.1:4173/branding/brand.html   the brand review page
python3 -m http.server 4174 --bind 127.0.0.1 --directory site   # every page, at http://127.0.0.1:4174/
npm run export && scripts/sync-site.sh          # after a logo change (from branding/)
```

Before publishing:

- Create the `hello@littlebrowser.app` mailbox used in the footer.
- Keep every product claim in line with `docs/design.md` (§5 privacy, §7 features), and don't
  promise dates or downloads until there's a build.

## Colours

The app icon always uses **Little Blue**, whatever the space colour. `palette.css` holds the tokens and is
the source of truth; the default space hue is 222, as in `chromium/lb/browser/spaces/space_model.h`.
The desktop sidebar tints itself with HSV (space hue, 0.55, 0.85), not with the tokens below.

| Token | Light | Dark | Use |
|---|---|---|---|
| Tile gradient | `#4783EB` → `#2435CC` (135°) | same | App icon background only |
| Mark | `#FFFFFF` | `#81ADF8` without a tile | The mark |
| Accent | `#245BDB` hsl(222 72% 50%) | `#5581E7` hsl(222 75% 62%) | Links, focus, primary buttons |
| Ink | `#212630` | `#E2E4E9` | Text and the word mark |
| Sidebar | `#DADFEC` | `#1E2129` | Default space sidebar |
| Incognito accent | `#B47EF1` | same | Incognito windows only |

## Usage rules

- **Keep the parts together.** Card, dot and outlines are one mark. Don't rearrange, rotate,
  recolour or animate them separately, except for the Peek "grow" motion the mark is based on.
- **Size.** Use `icon-small.svg` / `mark-template.svg` at 32 px and below and the full drawing
  above that. The smallest size is 16 px; the lockup needs at least 20 px of icon height.
- **Clear space.** Leave at least ¼ of the icon's width clear on every side. For the lockup,
  measure from the icon.
- **Backgrounds.** On light or dark neutrals, use the tile icon. On photos or busy colour, use the
  tile icon or the white mark on a solid Little Blue panel. Never put the bare mark on a busy
  background.
- **Word mark.** It is always lowercase "little browser", in Ink (`#212630`) on light or white on
  dark. Don't retype it in a font, stretch it, or add effects. "Little Bro" is a nickname for
  copy, not a logo.
- **In the app.** The Start page and tab icon may tint the tile with the current space hue so it sits calmly in the sidebar. The installed app icon stays Little Blue.
- **Forks.** The code is MPL-2.0, but the licence grants no rights to the name or logo (§13
  of docs/design.md). Forks must use their own name and icon.
