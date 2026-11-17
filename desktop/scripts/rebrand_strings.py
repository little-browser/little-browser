#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Replace the product name "Chromium" in Chromium's branded UI strings, keeping translations.

    rebrand_strings.py <chromium src dir> "<Product Name>" "<Company>" <grd> [<grd> ...]

What changes: "Chromium" as the product name -> <Product Name>; "The Chromium Authors" inside UI
messages (the About page copyright line) -> <Company>. What stays: the link that credits the
Chromium project ("made possible by the <a>Chromium</a> open source project"), "Chromium OS",
license headers in comments, URLs.

Translations (.xtb) are keyed by a fingerprint of the English message text, so changing the English
text alone would orphan every translation (all locales would fall back to English). For each .grd
we therefore:
  1. parse it with grit and record every message's fingerprint,
  2. replace the product name in the .grd,
  3. parse again; messages whose fingerprint changed get a mapping old id -> new id,
  4. in each translation file, rename the product inside those messages and re-key them.
Idempotent: once no "Chromium" is left, nothing changes.
"""

import re
import sys
from pathlib import Path

# "Chromium" as the product name — not "Chromium OS"/"ChromiumOS", chromium.org URLs (lowercase)
# or identifiers like ChromiumFoo — and "The Chromium Authors" (handled separately, see rebrand()).
PATTERN = re.compile(r'The Chromium Authors|\bChromium\b(?! ?OS\b)(?![A-Za-z])')


def make_rebrand(product: str, company: str, always_in_message: bool):
    """Returns a function that rebrands a .grd/.grdp/.xtb text (or one translation's text)."""

    def inside_message(text: str, pos: int) -> bool:
        if always_in_message:
            return True
        opened = max(text.rfind('<message', 0, pos), text.rfind('<translation', 0, pos))
        closed = max(text.rfind('</message>', 0, pos), text.rfind('</translation>', 0, pos))
        return opened > closed

    def credits_chromium(text: str, pos: int) -> bool:
        # Inside <ph name="BEGIN_LINK_CHROMIUM">...</ph>Chromium<ph name="END_LINK_CHROMIUM">
        begin = text.rfind('BEGIN_LINK_CHROMIUM', max(0, pos - 300), pos)
        return begin != -1 and text.rfind('END_LINK_CHROMIUM', begin, pos) == -1

    def rebrand(text: str) -> tuple[str, int]:
        count = 0

        def sub(m):
            nonlocal count
            if m.group(0) == 'The Chromium Authors':
                if not inside_message(text, m.start()):
                    return m.group(0)  # license headers
                count += 1
                return company
            if credits_chromium(text, m.start()):
                return m.group(0)
            count += 1
            return product

        return PATTERN.sub(sub, text), count

    return rebrand


def message_ids(grit, grd: Path) -> dict:
    root = grit.grd_reader.Parse(str(grd), dir=str(grd.parent), skip_validation_checks=True)
    ids = {}
    for node in root.GetChildrenOfType(grit.node.message.MessageNode):
        clique = node.GetCliques()[0]
        ids[node.attrs['name']] = clique.GetMessage().GetId()
    return ids


def main() -> int:
    src, product, company, grds = Path(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4:]
    rebrand_file = make_rebrand(product, company, always_in_message=False)
    rebrand_text = make_rebrand(product, company, always_in_message=True)
    sys.path.insert(0, str(src / 'tools' / 'grit'))
    import grit.grd_reader  # noqa: E402  (from the Chromium tree)
    import grit.node.message  # noqa: E402
    grit_mod = sys.modules['grit']

    for rel in grds:
        grd = src / rel
        new_text = grd.read_text(encoding='utf-8')
        before = message_ids(grit_mod, grd)
        # The .grd and every .grdp part it includes (parts can include parts).
        files, n, i = [grd], 0, 0
        while i < len(files):
            f = files[i]
            i += 1
            body = f.read_text(encoding='utf-8')
            files += [f.parent / p for p in re.findall(r'<part\s+file="([^"]+)"', body)]
            body, k = rebrand_file(body)
            if k:
                f.write_text(body, encoding='utf-8')
                n += k
        if not n:
            print(f'{rel}: nothing to rebrand')
            continue
        after = message_ids(grit_mod, grd)
        remap = {before[k]: after[k] for k in before if k in after and before[k] != after[k]}

        xtbs = re.findall(r'<file\s+path="([^"]+\.xtb)"', new_text)
        rekeyed = 0
        for x in xtbs:
            xtb = grd.parent / x
            if not xtb.exists():
                continue
            body = xtb.read_text(encoding='utf-8')

            def fix(m):
                nonlocal rekeyed
                old = m.group(2)
                if old not in remap:
                    return m.group(0)
                rekeyed += 1
                return m.group(1) + remap[old] + m.group(3) + rebrand_text(m.group(4))[0] + m.group(5)

            body = re.sub(r'(<translation id=")(\d+)("[^>]*>)(.*?)(</translation>)', fix, body, flags=re.S)
            xtb.write_text(body, encoding='utf-8')
        print(f'{rel}: {n} replacements, {len(remap)} messages re-keyed, '
              f'{rekeyed} translations updated across {len(xtbs)} languages')
    return 0


if __name__ == '__main__':
    sys.exit(main())
