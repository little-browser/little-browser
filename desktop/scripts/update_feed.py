#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Writes the browser's update feed (a Sparkle appcast) for one platform from releases.json.

    update_feed.py releases.json macos arm64 > updates/macos-arm64.xml

publish-release.sh runs this after every release. The feed lists only the newest release that
has an update file for the platform: the updater only ever installs the newest one. A release's
update file is the same file as its download, under updates/, so the website's download counts
(download_stats.py) can tell new downloads from updates. Its entry in releases.json has:

    "update": { "url", "bundle_version", "minimum_system_version", "ed_signature" }

bundle_version is the app's CFBundleVersion, <Chromium build>.<patch>.<revision>, which the
updater compares with its own. The update is marked critical for every version older than the
newest security release (`"security": true` on the release), so the prompt says it is important.
Runs on Python 3.7 (the AWS CLI image's).
"""

from __future__ import annotations

import datetime
import json
import sys
from email.utils import format_datetime
from xml.sax.saxutils import escape, quoteattr

SPARKLE_NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"


def _update_file(release: dict, platform: str, arch: str) -> dict | None:
    for f in release.get("files", []):
        if f.get("platform") == platform and f.get("arch") == arch and f.get("update"):
            return f
    return None


def _pub_date(day: str) -> str:
    date = datetime.datetime.strptime(day, "%Y-%m-%d").replace(tzinfo=datetime.timezone.utc)
    return format_datetime(date)


def appcast(releases: dict, platform: str, arch: str) -> str:
    """The feed XML. Releases are newest first, as publish-release.sh writes them."""
    with_updates = [
        (r, f) for r in releases.get("releases", [])
        for f in [_update_file(r, platform, arch)] if f is not None
    ]
    items = []
    if with_updates:
        release, f = with_updates[0]
        update = f["update"]
        security = next((u["update"]["bundle_version"] for r, u in with_updates
                         if r.get("security")), None)
        item = [
            "      <title>Little Browser %s</title>" % escape(release["version"]),
            "      <pubDate>%s</pubDate>" % _pub_date(release["date"]),
            "      <sparkle:version>%s</sparkle:version>" % escape(update["bundle_version"]),
            "      <sparkle:shortVersionString>%s</sparkle:shortVersionString>"
            % escape(release["version"]),
        ]
        if update.get("minimum_system_version"):
            item.append("      <sparkle:minimumSystemVersion>%s</sparkle:minimumSystemVersion>"
                        % escape(update["minimum_system_version"]))
        if platform == "macos" and arch == "arm64":
            item.append("      <sparkle:hardwareRequirements>arm64</sparkle:hardwareRequirements>")
        if security is not None:
            # Critical for versions older than the newest security release.
            item.append("      <sparkle:criticalUpdate sparkle:version=%s></sparkle:criticalUpdate>"
                        % quoteattr(security))
        item.append(
            "      <enclosure url=%s length=\"%d\" type=\"application/octet-stream\""
            " sparkle:edSignature=%s/>"
            % (quoteattr(update["url"]), int(f["size"]), quoteattr(update["ed_signature"])))
        items = ["    <item>"] + item + ["    </item>"]
    lines = [
        '<?xml version="1.0" encoding="utf-8"?>',
        '<rss version="2.0" xmlns:sparkle="%s">' % SPARKLE_NS,
        "  <channel>",
        "    <title>Little Browser</title>",
        *items,
        "  </channel>",
        "</rss>",
    ]
    return "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        sys.exit("usage: update_feed.py <releases.json> <platform> <arch>")
    with open(argv[0], encoding="utf-8") as f:
        releases = json.load(f)
    sys.stdout.write(appcast(releases, argv[1], argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
