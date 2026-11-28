# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Tests for rebrand_strings.py: the renaming rules, and the re-keying of translations run against
a small stand-in for Chromium's grit."""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from rebrand_strings import drop_duplicate_translations, make_rebrand  # noqa: E402


def message(text):
    return f'<message name="IDS_X" desc="d">{text}</message>'


class ChromiumTest(unittest.TestCase):
    def setUp(self):
        self.rebrand = make_rebrand('Little Browser', 'The Team', always_in_message=False)

    def test_renames_the_product_inside_messages(self):
        self.assertEqual(self.rebrand(message('Welcome to Chromium')),
                         (message('Welcome to Little Browser'), 1))

    def test_keeps_chromium_os_identifiers_and_license_headers(self):
        text = ('<!-- Copyright The Chromium Authors -->'
                + message('Chromium OS and ChromiumOS and ChromiumFoo'))
        self.assertEqual(self.rebrand(text), (text, 0))

    def test_renames_the_authors_inside_messages_only(self):
        self.assertEqual(self.rebrand(message('© The Chromium Authors')),
                         (message('© The Team'), 1))

    def test_keeps_the_link_crediting_the_project(self):
        text = message('made possible by the <ph name="BEGIN_LINK_CHROMIUM">&lt;a&gt;</ph>'
                       'Chromium<ph name="END_LINK_CHROMIUM">&lt;/a&gt;</ph> project')
        self.assertEqual(self.rebrand(text), (text, 0))

    def test_the_product_is_not_a_browser_browser(self):
        self.assertEqual(self.rebrand(message('the Chromium browser and Chromium Browser')),
                         (message('the Little Browser and Little Browser'), 2))
        text = message('other Chromium browsers')
        self.assertEqual(self.rebrand(text), (message('other Little Browser browsers'), 1))

    def test_leaves_chrome_alone_without_the_option(self):
        text = message('Welcome to Chrome')
        self.assertEqual(self.rebrand(text), (text, 0))


class ChromeTest(unittest.TestCase):
    def setUp(self):
        self.rebrand = make_rebrand('Little Browser', 'The Team', always_in_message=False,
                                    chrome=True)

    def renamed(self, text):
        return self.rebrand(message(text))[0]

    def test_renames_chrome_and_google_chrome(self):
        self.assertEqual(self.renamed('Welcome to Chrome'), message('Welcome to Little Browser'))
        self.assertEqual(self.renamed('Customize and control Google Chrome'),
                         message('Customize and control Little Browser'))
        self.assertEqual(self.renamed("Chrome's tips"), message("Little Browser's tips"))
        self.assertEqual(self.renamed('Open pages in the Chrome browser'),
                         message('Open pages in the Little Browser'))
        self.assertEqual(self.renamed('Chromium and Chrome'),
                         message('Little Browser and Little Browser'))

    def test_keeps_names_that_only_start_with_chrome(self):
        for text in ('ChromeOS', 'Chrome OS', 'Chromebook', 'Chrome Web Store', 'Chrome Webstore',
                     'Chrome web store', 'Chrome Enterprise', 'Chrome Apps', 'a Chrome App',
                     'Chrome Root Store', 'Chrome Remote Desktop', 'Google Chrome Beta',
                     'Chrome Dev', 'Chrome Canary', 'chrome://settings'):
            self.assertEqual(self.renamed(text), message(text), text)

    def test_keeps_googles_terms_of_service(self):
        text = 'the Google Chrome and ChromeOS Additional Terms of Service'
        self.assertEqual(self.renamed(text), message(text))

    def test_translations_are_renamed_whole(self):
        rebrand = make_rebrand('Little Browser', 'The Team', always_in_message=True, chrome=True)
        self.assertEqual(rebrand('Willkommen bei Chrome'), ('Willkommen bei Little Browser', 1))


class DuplicateTranslationsTest(unittest.TestCase):
    def test_keeps_the_first_translation_of_each_id(self):
        xtb = ('<translationbundle lang="de">\n'
               '<translation id="1">Willkommen bei Little Browser</translation>\n'
               '<translation id="2">Andere</translation>\n'
               '<translation id="1">Willkommen in Little Browser</translation>\n'
               '</translationbundle>')
        self.assertEqual(drop_duplicate_translations(xtb),
                         ('<translationbundle lang="de">\n'
                          '<translation id="1">Willkommen bei Little Browser</translation>\n'
                          '<translation id="2">Andere</translation>\n'
                          '</translationbundle>', 1))

    def test_a_gendered_translation_is_its_own(self):
        xtb = ('<translation id="1">a</translation>\n'
               '<translation id="1" gender="FEMININE">b</translation>\n'
               '<translation id="1">\nmulti\nline</translation>\n')
        self.assertEqual(drop_duplicate_translations(xtb),
                         ('<translation id="1">a</translation>\n'
                          '<translation id="1" gender="FEMININE">b</translation>\n', 1))


# A stand-in for Chromium's tools/grit: parses <message> elements (and <part> files) in document
# order and fingerprints each message by its text, as grit does.
FAKE_GRIT = {
    'grit/__init__.py': '',
    'grit/node/__init__.py': '',
    'grit/node/message.py': 'class MessageNode:\n    pass\n',
    'grit/grd_reader.py': """
import hashlib, re
from pathlib import Path
from grit.node.message import MessageNode

class Node(MessageNode):
    def __init__(self, name, text):
        self.attrs = {'name': name}
        self.text = text
    def GetCliques(self):
        return [self]
    def GetMessage(self):
        return self
    def GetId(self):
        return str(int(hashlib.sha1(self.text.encode()).hexdigest()[:12], 16))

class Root:
    def __init__(self, nodes):
        self.nodes = nodes
    def GetChildrenOfType(self, kind):
        return [n for n in self.nodes if isinstance(n, kind)]

def Parse(path, dir, skip_validation_checks):
    nodes = []
    def read(f):
        body = f.read_text()
        for m in re.finditer(r'<part file="([^"]+)"/>|<message name="([^"]+)"[^>]*>(.*?)</message>',
                             body, re.S):
            if m.group(1):
                read(f.parent / m.group(1))
            else:
                nodes.append(Node(m.group(2), m.group(3)))
    read(Path(path))
    return Root(nodes)
""",
}


class RekeyTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.src = Path(self.tmp.name)
        for rel, text in FAKE_GRIT.items():
            f = self.src / 'tools' / 'grit' / rel
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_text(text)
        sys.path.insert(0, str(self.src / 'tools' / 'grit'))
        from grit import grd_reader
        self.grd_reader = grd_reader
        sys.path.pop(0)

    def tearDown(self):
        for name in [m for m in sys.modules if m == 'grit' or m.startswith('grit.')]:
            del sys.modules[name]
        self.tmp.cleanup()

    def fingerprint(self, text):
        return self.grd_reader.Node('', text).GetId()

    def test_every_variant_of_a_name_keeps_its_translations(self):
        # One name in two <if> branches, both rebranded, the first in an included part.
        mac, other = 'Your Search Engine in Chromium', 'Your search engine in Chromium'
        (self.src / 'part.grdp').write_text(
            f'<grit-part><if expr="is_mac"><message name="IDS_T">{mac}</message></if></grit-part>')
        (self.src / 's.grd').write_text(
            '<grit><translations><file path="s_de.xtb" lang="de"/></translations><messages>'
            '<part file="part.grdp"/>'
            f'<if expr="not is_mac"><message name="IDS_T">{other}</message></if>'
            '<message name="IDS_U">Unchanged</message></messages></grit>')
        def xtb(*rows):
            return ('<translationbundle lang="de">\n'
                    + ''.join(f'<translation id="{self.fingerprint(english)}">{text}'
                              '</translation>\n' for english, text in rows)
                    + '</translationbundle>')

        (self.src / 's_de.xtb').write_text(xtb((mac, 'Ihre Suchmaschine in Chromium'),
                                               (other, 'Ihre Suchmaschine in Chromium.'),
                                               ('Unchanged', 'Unverändert')))
        subprocess.run([sys.executable, '-I', str(Path(__file__).with_name('rebrand_strings.py')),
                        str(self.src), 'Little Browser', 'The Team', 's.grd'],
                       check=True, capture_output=True)
        renamed = [t.replace('Chromium', 'Little Browser') for t in (mac, other)]
        self.assertEqual((self.src / 's_de.xtb').read_text(),
                         xtb((renamed[0], 'Ihre Suchmaschine in Little Browser'),
                             (renamed[1], 'Ihre Suchmaschine in Little Browser.'),
                             ('Unchanged', 'Unverändert')))


if __name__ == '__main__':
    unittest.main()
