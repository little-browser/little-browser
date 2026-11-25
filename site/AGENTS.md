# site/ — littlebrowser.app

Static files, uploaded as they are by the `site` job (`desktop/scripts/deploy-site.sh`). No build
step, no cookies, no analytics.

## Content architecture

| URL | What |
|---|---|
| `/` | What it is, in brief; links into the sections below |
| `/features` | Hub: every feature by group, and a feature × platform matrix |
| `/features/<slug>` | One page per feature (a feature a person would search for by name) |
| `/arc-alternative` | Landing page for people leaving Arc; links to the feature pages |
| `/downloads` | Pick and install a build, with each file's SHA-256 |
| `/releases` | Release notes, one anchor per version (`#v<version>`), drawn from `releases.json` |
| `/privacy` | The privacy policy |

- **A feature page is platform-neutral.** It describes the feature, not a platform. Its
  *Availability* table (one row per platform) is the only platform-specific part: when a platform
  gets the feature, change that row, and the hub's matrix, in the same commit. Never fork a page
  per platform, so the URL and its search ranking stay when Windows or Linux arrive.
- **Adding a feature:** one directory `features/<slug>/index.html`, a card in the hub, a row in the
  hub's matrix, a `sitemap.xml` entry, and links from related feature pages. Copy an existing
  feature page for the head (title, description, canonical, Open Graph, breadcrumb and FAQ
  JSON-LD), the footer and the *Availability*, *Related* and *Questions* sections.
- **Adding a platform:** a column in the hub's matrix, a row in each feature's table, and a card on
  `/downloads`. Its release files appear in `releases.json` with a `platform` and show on
  `/downloads` and `/releases` without a page change (`site.js` knows the names in `PLATFORMS`).
- **Titles and descriptions** are written for the search someone would make ("vertical tabs",
  "link preview", "import from Arc"), unique per page, and the page's one `<h1>` matches its title.
- Pages state what is true now and what is built; a feature that isn't built yet is listed as
  *Planned* in the table, not described in the text.
- The release notes are the `Release-note:` lines of the commits (`desktop/scripts/release_notes.py`),
  published in `releases.json`. `/releases` and `/downloads` read the same file in the browser.

## Languages

English pages in `site/` are the source. `desktop/scripts/site_i18n.py build` writes a translated
copy of every page under `site/<code>/` (`es`, `fr`, `de`, `pt-br`, `ja`, `zh`, `ru`, `uk`, `be`, `el`, `vi`, `hi`), the hreflang links
and language switcher (the globe in the header) between the `i18n:` comment markers of every page,
and `sitemap.xml` with each page's alternates. Never edit the copies, the marked places or
`sitemap.xml` by hand: edit the English page or a translation, then run `build`.

- **Translations** are `site-i18n/<code>.json`: `strings` maps an English text (the inner HTML of a
  paragraph, heading, list item, link or button, an attribute such as `aria-label`, a meta
  description, a JSON-LD name or text) to its translation; `js` does the same for the texts
  `site.js` writes (`t('…')`). A translation keeps the English text's tags, links and `{0}`
  placeholders, and no raw `"`. `site_i18n.py todo <code>` lists what a language still lacks.
- **Changing English text** changes its key: the old translation becomes unused and the new text
  missing, and `check` (a CI gate) fails until the language files are updated. Translate the new
  text into every language in the same change.
- **Adding a language:** an entry in `LANGS` (URL directory, hreflang code, its own name), a
  `site-i18n/<code>.json`, and the same in `site.js`' `base` pattern.
- `/privacy` and the 404 page stay English (legal text; one 404 file). Translated pages link to the
  English privacy page.
- **Choosing a language:** every address is its own page with `hreflang` alternates, so search
  engines index each one. A first visit to an English page goes to the language the browser asks
  for, if there is one (English first in the browser's list stays English; Traditional Chinese
  stays English); a choice made in the switcher is remembered in `localStorage` and wins.
