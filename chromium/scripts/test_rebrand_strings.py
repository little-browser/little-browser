# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Tests for rebrand_strings.py's renaming rules (the grit re-keying needs a Chromium tree)."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from rebrand_strings import make_rebrand  # noqa: E402


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


if __name__ == '__main__':
    unittest.main()
