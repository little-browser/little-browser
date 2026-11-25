# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import site_i18n as si  # noqa: E402

PAGE = '''<!doctype html>
<html lang="en">
<head>
  <title>Peek — Little Browser</title>
  <meta name="description" content="Preview links.">
  <link rel="canonical" href="https://littlebrowser.app/features/peek">
  <meta property="og:url" content="https://littlebrowser.app/features/peek">
  <meta property="og:site_name" content="Little Browser">
  <script type="application/ld+json">{"@graph": [{"@type": "Question", "name": "What is Peek?",
    "acceptedAnswer": {"@type": "Answer", "text": "A link preview."}},
    {"@type": "ListItem", "name": "Features", "item": "https://littlebrowser.app/features/peek"}]}</script>
  <link rel="stylesheet" href="/styles.css">
</head>
<body>
<header><div class="wrap"><nav><a href="/features/peek">Features</a></nav>
    <div class="theme"><button aria-label="Light">x</button></div></div></header>
<main>
  <h1>Peek</h1>
  <p>Press <kbd>⌘</kbd> to <a href="/downloads">download</a> it.</p>
  <details class="faq"><summary>What is Peek?</summary><p>A link preview.</p></details>
  <a href="/privacy">Privacy</a> <a href="https://example.com/features">x</a>
</main>
<script src="/site.js"></script>
</body>
</html>
'''
SITE_JS = "const a = t('Hello'); plural(v, '{0} thing', '{0} things');"
FR = {
    'strings': {
        'Peek — Little Browser': 'Peek — Little Browser (fr)',
        'Preview links.': 'Aperçu des liens.',
        'Peek': 'Peek',
        'Features': 'Fonctions',
        'Press <kbd>⌘</kbd> to <a href="/downloads">download</a> it.': 'Appuyez sur <kbd>⌘</kbd> pour le <a href="/downloads">télécharger</a>.',
        'What is Peek?': 'Qu’est-ce que Peek ?',
        'A link preview.': 'Un aperçu de lien.',
        'Privacy': 'Confidentialité',
        'Light': 'Clair',
        'Language': 'Langue',
    },
    'js': {'Hello': 'Bonjour', '{0} thing': '{0} chose', '{0} things': '{0} choses'},
}


class Env(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.saved = (si.SITE, si.I18N, si.LANGS, si.UNTRANSLATED)
        si.SITE, si.I18N = root / 'site', root / 'site-i18n'
        si.LANGS = {'fr': ('fr', 'Français')}
        si.UNTRANSLATED = {'privacy/index.html'}
        (si.SITE / 'features/peek').mkdir(parents=True)
        (si.SITE / 'privacy').mkdir()
        (si.SITE / 'features/peek/index.html').write_text(PAGE)
        (si.SITE / 'privacy/index.html').write_text(PAGE.replace('/features/peek', '/privacy'))
        (si.SITE / 'site.js').write_text(SITE_JS)
        si.I18N.mkdir()
        (si.I18N / 'fr.json').write_text(json.dumps(FR))

    def tearDown(self):
        si.SITE, si.I18N, si.LANGS, si.UNTRANSLATED = self.saved
        self.tmp.cleanup()


class Extract(Env):
    def test_keys_cover_text_attributes_meta_and_json_ld(self):
        tr, _ = si.pages()
        keys = si.all_keys(tr)
        for k in ['Peek — Little Browser', 'Preview links.', 'Peek', 'Features', 'What is Peek?', 'A link preview.',
                  'Press <kbd>⌘</kbd> to <a href="/downloads">download</a> it.', 'Light', 'Language']:
            self.assertIn(k, keys)
        self.assertNotIn('⌘', keys)  # no letters: not text
        self.assertEqual(si.js_keys(), ['Hello', '{0} thing', '{0} things'])

    def test_untranslated_pages_are_left_out(self):
        tr, other = si.pages()
        self.assertEqual(tr, ['features/peek/index.html'])
        self.assertEqual(other, ['privacy/index.html'])


class Render(Env):
    def setUp(self):
        super().setUp()
        self.files, _ = si.render()

    def test_translated_page(self):
        out = self.files['fr/features/peek/index.html']
        self.assertIn('<html lang="fr">', out)
        self.assertIn('<title>Peek — Little Browser (fr)</title>', out)
        self.assertIn('content="Aperçu des liens."', out)
        self.assertIn('aria-label="Clair"', out)
        self.assertIn('<h1>Peek</h1>', out)

    def test_links_and_urls_follow_the_language(self):
        out = self.files['fr/features/peek/index.html']
        self.assertIn('<a href="/fr/features/peek">Fonctions</a>', out)
        self.assertIn('<a href="/downloads">télécharger</a>', out)  # no such translated page here
        self.assertIn('<a href="/privacy">', out)          # not translated: stays
        self.assertIn('href="https://example.com/features"', out)
        self.assertIn('<link rel="canonical" href="https://littlebrowser.app/fr/features/peek">', out)
        self.assertIn('<meta property="og:url" content="https://littlebrowser.app/fr/features/peek">', out)
        self.assertIn('href="/styles.css"', out)

    def test_json_ld_is_translated(self):
        out = self.files['fr/features/peek/index.html']
        self.assertIn('Qu’est-ce que Peek ?'.encode('ascii', 'backslashreplace').decode(), out)
        self.assertIn('Un aper', out.replace('\\u00e7', 'ç'))
        self.assertIn('"item": "https://littlebrowser.app/fr/features/peek"', out)

    def test_hreflang_and_switcher_on_every_translated_page(self):
        for rel in ('features/peek/index.html', 'fr/features/peek/index.html'):
            out = self.files[rel]
            self.assertIn('hreflang="x-default" href="https://littlebrowser.app/features/peek"', out)
            self.assertIn('hreflang="fr" href="https://littlebrowser.app/fr/features/peek"', out)
            self.assertIn('hreflang="en" href="https://littlebrowser.app/features/peek"', out)
            self.assertIn('<details class="lang">', out)
        en = self.files['features/peek/index.html']
        self.assertIn('<a href="/fr/features/peek" lang="fr"', en)
        self.assertIn('aria-current="true">English', en)
        self.assertIn('aria-current="true">Français', self.files['fr/features/peek/index.html'])
        self.assertIn('aria-label="Langue"', self.files['fr/features/peek/index.html'])

    def test_untranslated_page_gets_only_the_switcher_to_the_home_pages(self):
        out = self.files['privacy/index.html']
        self.assertNotIn('hreflang="x-default"', out)
        self.assertIn('<a href="/fr/" lang="fr"', out)
        self.assertNotIn('privacy/index.html'.replace('index.html', ''), self.files['sitemap.xml'].replace('/privacy', 'ok', 1))

    def test_strings_for_the_scripts(self):
        out = self.files['fr/features/peek/index.html']
        self.assertIn('window.LB_STRINGS = {"Hello": "Bonjour"', out)

    def test_sitemap_lists_each_language_with_alternates(self):
        sm = self.files['sitemap.xml']
        self.assertIn('<loc>https://littlebrowser.app/fr/features/peek</loc>', sm)
        self.assertIn('<xhtml:link rel="alternate" hreflang="fr" href="https://littlebrowser.app/fr/features/peek"/>', sm)
        self.assertIn('<loc>https://littlebrowser.app/privacy</loc>', sm)

    def test_build_is_idempotent(self):
        for rel, text in self.files.items():
            p = si.SITE / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text)
        again, _ = si.render()
        self.assertEqual(again, self.files)
        self.assertEqual(si.main(['check']), 0)


class Check(Env):
    def run_check(self):
        import io
        import contextlib
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            code = si.main(['check'])
        return code, err.getvalue()

    def test_missing_translation_fails(self):
        d = json.loads((si.I18N / 'fr.json').read_text())
        del d['strings']['Features']
        (si.I18N / 'fr.json').write_text(json.dumps(d))
        code, err = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn("fr: missing translation: 'Features'", err)

    def test_changed_tags_fail(self):
        d = json.loads((si.I18N / 'fr.json').read_text())
        k = 'Press <kbd>⌘</kbd> to <a href="/downloads">download</a> it.'
        d['strings'][k] = 'Appuyez sur ⌘ pour le <a href="/other">télécharger</a>.'
        (si.I18N / 'fr.json').write_text(json.dumps(d))
        self.assertIn('tags or links differ', self.run_check()[1])

    def test_unused_and_script_problems_fail(self):
        d = json.loads((si.I18N / 'fr.json').read_text())
        d['strings']['Gone'] = 'Parti'
        d['js']['{0} thing'] = 'chose'
        (si.I18N / 'fr.json').write_text(json.dumps(d))
        err = self.run_check()[1]
        self.assertIn('unused translation', err)
        self.assertIn('placeholders differ', err)

    def test_unwritten_files_fail(self):
        code, err = self.run_check()  # nothing built yet
        self.assertEqual(code, 1)
        self.assertIn('out of date: site/fr/features/peek/index.html', err)

    def test_raw_quote_in_translation_fails(self):
        d = json.loads((si.I18N / 'fr.json').read_text())
        d['strings']['Preview links.'] = 'Aperçu "des" liens.'
        (si.I18N / 'fr.json').write_text(json.dumps(d))
        self.assertIn('unescaped quote', self.run_check()[1])


class Redirect(unittest.TestCase):
    def test_snippet_is_well_formed(self):
        self.assertIn("document.documentElement.lang !== 'en'", si.REDIRECT)
        self.assertIn("localStorage.getItem('lb-lang')", si.REDIRECT)


if __name__ == '__main__':
    unittest.main()
