#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Translated copies of the website.

The English pages in site/ are the source. Each language in LANGS is a copy of every translated
page under site/<code>/, made by replacing the text of the English page with the translation from
site-i18n/<code>.json (English text -> translation). The English pages and the copies also get, in
marked places, the hreflang links and the language switcher.

  site_i18n.py build     write the copies, the marked places and sitemap.xml
  site_i18n.py check     fail if a translation is missing, unused or broken, or the written files
                         are out of date (CI runs this)
  site_i18n.py strings   print every English string that needs a translation, as JSON
  site_i18n.py todo CODE print the strings CODE has no translation for yet, as JSON
"""
import argparse
import html
import json
import re
import sys
from collections import Counter
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SITE = ROOT / 'site'
I18N = ROOT / 'site-i18n'
ORIGIN = 'https://littlebrowser.app'

# URL directory -> (hreflang, the language's own name). English stays at the root.
LANGS = {
    'es': ('es', 'Español'),
    'fr': ('fr', 'Français'),
    'de': ('de', 'Deutsch'),
    'pt-br': ('pt-BR', 'Português (Brasil)'),
    'ja': ('ja', '日本語'),
    'zh': ('zh-Hans', '简体中文'),
    'ru': ('ru', 'Русский'),
    'uk': ('uk', 'Українська'),
    'be': ('be', 'Беларуская'),
    'el': ('el', 'Ελληνικά'),
    'vi': ('vi', 'Tiếng Việt'),
    'hi': ('hi', 'हिन्दी'),
}
# Pages that stay English only: the legal text and the 404 page (one file, served at any address).
UNTRANSLATED = {'privacy/index.html', '404.html'}
# Texts the pages' markup doesn't carry but the tool writes.
EXTRA_STRINGS = ['Language']

INLINE = {'a', 'b', 'i', 'em', 'strong', 'kbd', 'code', 'span', 'br', 'small', 'mark', 'abbr', 'sub',
          'sup', 'u', 's', 'wbr'}
TEXT_BLOCKS = {'p', 'li', 'td', 'th', 'summary', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'dt', 'dd',
               'figcaption', 'caption', 'label', 'button', 'title', 'blockquote'}
VOID = {'br', 'img', 'meta', 'link', 'input', 'hr', 'wbr', 'source', 'path', 'circle', 'rect', 'use',
        'line', 'polyline', 'polygon', 'stop'}
SKIP = {'script', 'style', 'svg', 'pre'}
ATTRS = ('aria-label', 'title', 'alt', 'placeholder')
MARKERS = {
    'alternates': re.compile(r'([ \t]*)<!-- i18n:alternates -->.*?<!-- /i18n:alternates -->\n?', re.S),
    'switcher': re.compile(r'([ \t]*)<!-- i18n:switcher -->.*?<!-- /i18n:switcher -->\n?', re.S),
}


# ------------------------------------------------------------------------------------ parsing
class Node:
    def __init__(self, tag, attrs, start, inner_start, parent):
        self.tag, self.attrs, self.start, self.inner_start, self.parent = tag, attrs, start, inner_start, parent
        self.inner_end = self.end = None
        self.children = []
        self.text = False  # has a direct text child with a letter in it
        self.start_text = ''


class Tree(HTMLParser):
    def __init__(self, src):
        super().__init__(convert_charrefs=False)
        self.src = src
        self.lines = [0]
        for line in src.splitlines(keepends=True):
            self.lines.append(self.lines[-1] + len(line))
        self.root = Node('#root', {}, 0, 0, None)
        self.cur = self.root
        self.feed(src)
        self.close()

    def abs_pos(self):
        line, col = self.getpos()
        return self.lines[line - 1] + col

    def handle_starttag(self, tag, attrs):
        start = self.abs_pos()
        text = self.get_starttag_text()
        n = Node(tag, dict(attrs), start, start + len(text), self.cur)
        n.start_text = text
        self.cur.children.append(n)
        if tag in VOID or text.endswith('/>'):
            n.inner_end, n.end = n.inner_start, n.inner_start
        else:
            self.cur = n

    def handle_endtag(self, tag):
        n = self.cur
        while n is not self.root and n.tag != tag:
            n = n.parent
        if n is self.root:
            return
        pos = self.abs_pos()
        n.inner_end = pos
        n.end = self.src.index('>', pos) + 1
        self.cur = n.parent

    def handle_data(self, data):
        if re.search(r'[^\W\d_]', data):
            self.cur.text = True

    def handle_entityref(self, name):
        pass


def walk(n):
    yield n
    for c in n.children:
        yield from walk(c)


def inline_only(n):
    return all(c.tag in INLINE and inline_only(c) for c in n.children)


def has_letters(s):
    return len(re.findall(r'[^\W\d_]', html.unescape(re.sub(r'<[^>]+>', '', s)))) >= 2


def segments(tree):
    """[(start, end, key)] of the translatable inner HTML, outermost first, not nested."""
    out = []

    def visit(n):
        if n.tag in SKIP or n.attrs.get('translate') == 'no':
            return
        if n.tag != '#root' and n.inner_end is not None and inline_only(n) and (n.tag in TEXT_BLOCKS or n.text):
            inner = tree.src[n.inner_start:n.inner_end]
            if has_letters(inner):
                out.append((n.inner_start, n.inner_end, inner.strip(), n))
            return
        for c in n.children:
            visit(c)

    visit(tree.root)
    return out


def attr_values(n):
    """[(attr, value)] of the translatable attributes of one element."""
    res = []
    for a in ATTRS:
        v = n.attrs.get(a)
        if v and has_letters(v):
            res.append((a, v))
    if n.tag == 'meta':
        k = n.attrs.get('name') or n.attrs.get('property')
        v = n.attrs.get('content')
        if k in ('description', 'og:title', 'og:description') and v:
            res.append(('content', v))
    return res


def ld_blocks(src):
    return list(re.finditer(r'(<script type="application/ld\+json">)(.*?)(</script>)', src, re.S))


def ld_strings(obj, found=None):
    found = [] if found is None else found
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k in ('name', 'text') and isinstance(v, str):
                if v != 'Little Browser':
                    found.append(v)
            else:
                ld_strings(v, found)
    elif isinstance(obj, list):
        for v in obj:
            ld_strings(v, found)
    return found


def plain(s):
    return html.unescape(re.sub(r'<[^>]+>', '', s)).strip()


def strip_markers(src):
    for name, rx in MARKERS.items():
        src = rx.sub(lambda m, n=name: f'{m.group(1)}<!-- i18n:{n} --><!-- /i18n:{n} -->\n', src)
    return src


def analyse(src):
    """The page's translatable pieces: (tree, segments, attribute edits, all keys in order)."""
    tree = Tree(src)
    segs = segments(tree)
    spans = [(s, e) for s, e, _, _ in segs]
    attrs = []
    for n in walk(tree.root):
        if n.tag == '#root':
            continue
        if any(s <= n.start < e for s, e in spans):
            continue
        for a, v in attr_values(n):
            attrs.append((n, a, v))
    keys = [k for _, _, k, _ in segs] + [v for _, _, v in attrs]
    by_plain = {plain(k): k for k in keys}
    for m in ld_blocks(src):
        for s in ld_strings(json.loads(m.group(2))):
            if plain(s) not in by_plain:
                keys.append(s)
    return tree, segs, attrs, keys


# ------------------------------------------------------------------------------------ pages
def pages():
    """Relative paths of the English pages that are translated, and of those that aren't."""
    tr, other = [], []
    for p in sorted(SITE.rglob('*.html')):
        rel = p.relative_to(SITE).as_posix()
        if rel.split('/')[0] in LANGS:
            continue
        s = p.read_text()
        if 'noindex' in s or rel in UNTRANSLATED:
            other.append(rel)
        else:
            tr.append(rel)
    return tr, other


def route(rel):
    if rel == 'index.html':
        return '/'
    return '/' + rel[:-len('/index.html')] if rel.endswith('/index.html') else '/' + rel


def url_for(rt, code):
    """The address of a route in a language ('' = English)."""
    if not code:
        return rt
    return f'/{code}' + ('' if rt == '/' else rt) if rt != '/' else f'/{code}/'


def all_keys(tr):
    keys = {}
    for rel in tr:
        for k in analyse(strip_markers((SITE / rel).read_text()))[3]:
            keys[k] = None
    for k in EXTRA_STRINGS:
        keys[k] = None
    return list(keys)


def js_keys():
    src = (SITE / 'site.js').read_text()
    keys = []
    for m in re.finditer(r"""\bt\((?:'((?:[^'\\]|\\.)*)'|"((?:[^"\\]|\\.)*)")""", src):
        k = (m.group(1) if m.group(1) is not None else m.group(2)).replace("\\'", "'")
        if k not in keys:
            keys.append(k)
    for m in re.finditer(r"""plural\([^,]+,\s*'([^']+)',\s*'([^']+)'""", src):
        for k in m.groups():
            if k not in keys:
                keys.append(k)
    return keys


def load(code):
    p = I18N / f'{code}.json'
    if not p.exists():
        return {'strings': {}, 'js': {}}
    d = json.loads(p.read_text())
    return {'strings': d.get('strings', {}), 'js': d.get('js', {})}


def skeleton(s):
    c = Counter()
    for m in re.finditer(r'<(/?)([a-zA-Z][a-zA-Z0-9]*)([^>]*)>', s):
        if m.group(1):
            c[('/', m.group(2).lower())] += 1
            continue
        attrs = dict(re.findall(r'([\w-]+)="([^"]*)"', m.group(3)))
        for a in ATTRS:
            attrs.pop(a, None)
        c[(m.group(2).lower(), tuple(sorted(attrs.items())))] += 1
    return c


def problems(code, keys):
    """Errors in one language's file against the keys the English pages need."""
    d = load(code)
    errs = []
    need = set(keys)
    for k in keys:
        v = d['strings'].get(k)
        if not v:
            errs.append(f'{code}: missing translation: {k[:70]!r}')
        elif skeleton(k) != skeleton(v):
            errs.append(f'{code}: tags or links differ from the English: {k[:70]!r}')
        elif '"' in v and '"' not in k:
            errs.append(f'{code}: unescaped quote (use &quot;): {v[:70]!r}')
    for k in d['strings']:
        if k not in need:
            errs.append(f'{code}: unused translation: {k[:70]!r}')
    jk = js_keys()
    for k in jk:
        v = d['js'].get(k)
        if not v:
            errs.append(f'{code}: missing script translation: {k!r}')
        else:
            if Counter(re.findall(r'\{\d\}', k)) != Counter(re.findall(r'\{\d\}', v)):
                errs.append(f'{code}: placeholders differ: {k!r}')
    for k in d['js']:
        if k not in jk:
            errs.append(f'{code}: unused script translation: {k!r}')
    return errs


def translate(src, tree, segs, attrs, strings, code, rts):
    edits = []
    for s, e, key, n in segs:
        raw = tree.src[s:e]
        lead = raw[:len(raw) - len(raw.lstrip())]
        trail = raw[len(raw.rstrip()):]
        edits.append((s, e, lead + strings.get(key, key) + trail))
    for n, a, v in attrs:
        t = strings.get(v, v)
        rx = re.compile(r'(\s' + re.escape(a) + r'=)(["\'])' + re.escape(v) + r'\2')
        m = rx.search(n.start_text)
        if m:
            q = m.group(2)
            edits.append((n.start + m.start(), n.start + m.end(), f'{m.group(1)}{q}{t}{q}'))
    out = src
    for s, e, text in sorted(edits, reverse=True):
        out = out[:s] + text + out[e:]
    # JSON-LD
    by_plain = {plain(k): v for k, v in strings.items()}

    def tr_ld(o, parent=None):
        if isinstance(o, dict):
            r = {}
            for k, v in o.items():
                if k in ('name', 'text') and isinstance(v, str):
                    r[k] = v if v == 'Little Browser' else plain(by_plain.get(plain(v), strings.get(v, v)))
                elif k == 'item' and isinstance(v, str):
                    r[k] = localize_url(v, code, rts)
                elif k == 'url' and isinstance(v, str) and o.get('@type') == 'ListItem':
                    r[k] = localize_url(v, code, rts)
                else:
                    r[k] = tr_ld(v, o)
            return r
        if isinstance(o, list):
            return [tr_ld(v, parent) for v in o]
        return o

    def repl(m):
        data = tr_ld(json.loads(m.group(2)))
        return m.group(1) + json.dumps(data, indent=2, ensure_ascii=True) + m.group(3)

    return re.sub(r'(<script type="application/ld\+json">)(.*?)(</script>)', repl, out, flags=re.S)


def localize_url(u, code, rts):
    if not u.startswith(ORIGIN):
        return u
    path, hashq = re.match(r'([^#?]*)(.*)', u[len(ORIGIN):]).groups()
    rt = '/' + path.strip('/') if path.strip('/') else '/'
    return ORIGIN + url_for(rt, code) + hashq if rt in rts else u


def localize_links(s, code, rts):
    def a(m):
        path, hashq = re.match(r'([^#?]*)(.*)', m.group(2)).groups()
        rt = '/' + path.strip('/') if path.strip('/') else '/'
        if rt not in rts:
            return m.group(0)
        return f'{m.group(1)}"{url_for(rt, code)}{hashq}"'

    return re.sub(r'(<a\b[^>]*?\shref=)"(/[^"]*)"', a, s)


def alternates_block(rt, rts):
    lines = ['<!-- i18n:alternates -->']
    for code, (hl, _) in [('', ('en', 'English'))] + list(LANGS.items()):
        lines.append(f'  <link rel="alternate" hreflang="{hl}" href="{ORIGIN}{url_for(rt, code)}">')
    lines.append(f'  <link rel="alternate" hreflang="x-default" href="{ORIGIN}{rt}">')
    lines.append(REDIRECT)
    lines.append('  <!-- /i18n:alternates -->\n')
    return '  ' + '\n'.join(lines).lstrip()


# A first visit to an English page goes to the visitor's language when it has a translation:
# the choice made in the switcher, else the first language the browser lists that we have (English
# first means stay). Search engines and links keep working: every address is its own page.
REDIRECT = '''  <script>(() => { try {
    if (document.documentElement.lang !== 'en') return;
    const alt = {};
    for (const l of document.querySelectorAll('link[rel=alternate][hreflang]')) alt[l.hreflang.toLowerCase()] = new URL(l.href).pathname;
    const pick = (raw) => {
      const t = raw.toLowerCase(), p = t.split('-')[0];
      if (p === 'en') return 'en';
      if (p === 'zh') return /^zh(-hans|-cn|-sg)?$/.test(t) ? 'zh-hans' : null;
      if (p === 'pt') return 'pt-br';
      return alt[t] ? t : alt[p] ? p : null;
    };
    let c = null;
    try { c = localStorage.getItem('lb-lang'); } catch {}
    if (!c) for (const raw of navigator.languages || [navigator.language]) { c = pick(raw); if (c) break; }
    if (c && c !== 'en' && alt[c]) location.replace(alt[c] + location.hash);
  } catch {} })();</script>'''


def switcher_block(rt, lang_code, rts, label):
    items = []
    for code, (hl, name) in [('', ('en', 'English'))] + list(LANGS.items()):
        href = url_for(rt, code) if rt in rts else url_for('/', code)
        cur = ' aria-current="true"' if code == lang_code else ''
        items.append(f'<li><a href="{href}" lang="{hl}" hreflang="{hl}" data-lang="{hl.lower()}"{cur}>{name}</a></li>')
    globe = ('<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" '
             'aria-hidden="true"><circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c2.6 2.6 4 5.7 4 9s-1.4 6.4-4 9c-2.6-2.6-4-5.7-4-9s1.4-6.4 4-9z"/></svg>')
    return ('<!-- i18n:switcher -->\n    <details class="lang">\n      '
            f'<summary aria-label="{label}" title="{label}">{globe}</summary>\n      <ul>\n        '
            + '\n        '.join(items) + '\n      </ul>\n    </details>\n    <!-- /i18n:switcher -->')


def ensure_markers(src):
    if '<!-- i18n:alternates -->' not in src:
        src = src.replace('  <link rel="stylesheet" href="/styles.css">',
                          '  <!-- i18n:alternates --><!-- /i18n:alternates -->\n  <link rel="stylesheet" href="/styles.css">', 1)
    if '<!-- i18n:switcher -->' not in src:
        src = src.replace('    <div class="theme"', '    <!-- i18n:switcher --><!-- /i18n:switcher -->\n    <div class="theme"', 1)
    return src


def fill(base, rt, lang_code, rts, label, with_alternates):
    out = ensure_markers(base)
    out = re.sub(r'<!-- i18n:alternates -->.*?<!-- /i18n:alternates -->',
                 lambda m: (alternates_block(rt, rts).strip() if with_alternates else m.group(0)), out, flags=re.S)
    out = re.sub(r'<!-- i18n:switcher -->.*?<!-- /i18n:switcher -->',
                 lambda m: switcher_block(rt, lang_code, rts, label), out, flags=re.S)
    return out


def render():
    """{path relative to site/: text} of everything this tool writes."""
    tr, other = pages()
    rts = {route(r) for r in tr}
    keys = all_keys(tr)
    files = {}
    label_en = 'Language'
    for rel in tr + other:
        base = strip_markers((SITE / rel).read_text())
        rt = route(rel)
        files[rel] = fill(base, rt, '', rts, label_en, rel in tr)
    for code, (hl, _) in LANGS.items():
        d = load(code)
        strings = d['strings']
        for rel in tr:
            base = strip_markers((SITE / rel).read_text())
            tree, segs, attrs, _ = analyse(base)
            out = translate(base, tree, segs, attrs, strings, code, rts)
            rt = route(rel)
            out = out.replace('<html lang="en">', f'<html lang="{hl}">', 1)
            url = ORIGIN + url_for(rt, code)
            out = re.sub(r'(<link rel="canonical" href=")[^"]*', lambda m: m.group(1) + url, out)
            out = re.sub(r'(<meta property="og:url" content=")[^"]*', lambda m: m.group(1) + url, out)
            out = out.replace('<meta property="og:site_name"', f'<meta property="og:locale" content="{hl.replace("-", "_")}">\n  <meta property="og:site_name"', 1)
            out = localize_links(out, code, rts)
            out = fill(out, rt, code, rts, strings.get('Language', 'Language'), True)
            js = json.dumps(d['js'], ensure_ascii=False).replace('</', '<\\/')
            out = out.replace('</head>', f'  <script>window.LB_STRINGS = {js};</script>\n</head>', 1)
            files[f'{code}/{rel}'] = out
    files['sitemap.xml'] = sitemap(tr, other)
    return files, keys


def sitemap(tr, other):
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9" xmlns:xhtml="http://www.w3.org/1999/xhtml">']
    order = sorted(tr, key=lambda r: (route(r) != '/', route(r)))
    for rel in order:
        rt = route(rel)
        for code, (hl, _) in [('', ('en', ''))] + list(LANGS.items()):
            out.append(f'  <url><loc>{ORIGIN}{url_for(rt, code)}</loc>')
            for c2, (h2, _) in [('', ('en', ''))] + list(LANGS.items()):
                out.append(f'    <xhtml:link rel="alternate" hreflang="{h2}" href="{ORIGIN}{url_for(rt, c2)}"/>')
            out.append(f'    <xhtml:link rel="alternate" hreflang="x-default" href="{ORIGIN}{rt}"/>')
            out.append('  </url>')
    for rel in other:
        if rel != '404.html':
            out.append(f'  <url><loc>{ORIGIN}{route(rel)}</loc></url>')
    out.append('</urlset>')
    return '\n'.join(out) + '\n'


# ------------------------------------------------------------------------------------ commands
def cmd_build(_):
    files, keys = render()
    for rel, text in files.items():
        p = SITE / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        if not p.exists() or p.read_text() != text:
            p.write_text(text)
    # Copies of pages that no longer exist.
    for code in LANGS:
        for p in (SITE / code).rglob('*.html') if (SITE / code).exists() else []:
            if p.relative_to(SITE).as_posix() not in files:
                p.unlink()
    print(f'wrote {len(files)} files')


def cmd_check(_):
    files, keys = render()
    errs = []
    for code in LANGS:
        errs += problems(code, keys)
    for rel, text in files.items():
        p = SITE / rel
        if not p.exists() or p.read_text() != text:
            errs.append(f'out of date: site/{rel} (run desktop/scripts/site_i18n.py build)')
    for code in LANGS:
        d = SITE / code
        for p in d.rglob('*.html') if d.exists() else []:
            if p.relative_to(SITE).as_posix() not in files:
                errs.append(f'stale: site/{p.relative_to(SITE)}')
    for e in errs:
        print(e, file=sys.stderr)
    return 1 if errs else 0


def cmd_strings(_):
    tr, _ = pages()
    print(json.dumps({'strings': {k: '' for k in all_keys(tr)}, 'js': {k: '' for k in js_keys()}},
                     ensure_ascii=False, indent=1))


def cmd_todo(a):
    tr, _ = pages()
    d = load(a.code)
    print(json.dumps({'strings': {k: '' for k in all_keys(tr) if not d['strings'].get(k)},
                      'js': {k: '' for k in js_keys() if not d['js'].get(k)}}, ensure_ascii=False, indent=1))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('build').set_defaults(f=cmd_build)
    sub.add_parser('check').set_defaults(f=cmd_check)
    sub.add_parser('strings').set_defaults(f=cmd_strings)
    t = sub.add_parser('todo')
    t.add_argument('code', choices=sorted(LANGS))
    t.set_defaults(f=cmd_todo)
    a = ap.parse_args(argv)
    return a.f(a) or 0


if __name__ == '__main__':
    sys.exit(main())
