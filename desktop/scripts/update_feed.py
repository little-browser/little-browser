#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Writes the browser's update feed (a Sparkle appcast) for one platform and architecture from
releases.json.

    update_feed.py releases.json macos arm64 > updates/macos-arm64.xml
    update_feed.py releases.json macos x64 > updates/macos-x64.xml

Each architecture has its own feed, and the app reads the one it was built for
(package-macos.sh). publish-release.sh rewrites a build's feed when it publishes that build. Each
architecture's build is published on its own, so a release can have one before another: a feed
lists only the newest release that has an update file for its platform and architecture, as the
updater only ever installs the newest one. A release's
update file is the same file as its download, under updates/, so the website's download counts
(download_stats.py) can tell new downloads from updates. Its entry in releases.json has:

    "update": { "url", "bundle_version", "minimum_system_version", "ed_signature" }

bundle_version is the app's CFBundleVersion, <Chromium build>.<patch>.<revision>, which the
updater compares with its own; it is the same for every architecture of a release. The update is
marked critical for every version older than the newest security release (`"security": true` on
the release) up to the offered one, so the prompt says it is important. That release may have no
build for this architecture: a later release has its fixes all the same.
Runs on Python 3.7 (the AWS CLI image's).
"""

from __future__ import annotations

import datetime
import json
import re
import sys
from email.utils import format_datetime
from xml.sax.saxutils import escape, quoteattr

SPARKLE_NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"
# The architecture names releases.json and the feeds' file names use (GN's).
ARCHS = ("arm64", "x64")


def _update_file(release: dict, platform: str, arch: str | None) -> dict | None:
    """The release's update file for the platform and arch (None: any arch)."""
    for f in release.get("files", []):
        if f.get("platform") == platform and arch in (None, f.get("arch")) and f.get("update"):
            return f
    return None


def _newest_first(releases: list) -> list:
    """Releases by version, newest first (157.0.8103.78-10 after -9), whatever the list's order:
    a release is listed when its first build is published, which can be after a newer one's."""
    def key(r: dict) -> tuple:
        parts = re.findall(r"[0-9]+", str(r.get("version", "")))
        return tuple(int(p) for p in parts)
    return sorted(releases, key=key, reverse=True)


def _pub_date(day: str) -> str:
    date = datetime.datetime.strptime(day, "%Y-%m-%d").replace(tzinfo=datetime.timezone.utc)
    return format_datetime(date)


def appcast(releases: dict, platform: str, arch: str) -> str:
    """The feed XML."""
    listed = _newest_first(releases.get("releases", []))
    offered = next((i for i, r in enumerate(listed) if _update_file(r, platform, arch)), None)
    items = []
    if offered is not None:
        release = listed[offered]
        f = _update_file(release, platform, arch)
        update = f["update"]
        # The newest security release from the offered one back, whichever architectures it had.
        security = next((u["update"]["bundle_version"] for r in listed[offered:]
                         if r.get("security")
                         for u in [_update_file(r, platform, None)] if u is not None), None)
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
    if argv[2] not in ARCHS:
        sys.exit("update_feed.py: the arch is one of %s, not %r" % (", ".join(ARCHS), argv[2]))
    with open(argv[0], encoding="utf-8") as f:
        releases = json.load(f)
    sys.stdout.write(appcast(releases, argv[1], argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
