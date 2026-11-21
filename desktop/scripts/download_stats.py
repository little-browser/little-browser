#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Counts downloads and updates of each release file from downloads.littlebrowser.app's logs.

    download_stats.py --releases releases.json --stats stats.json --logs <dir> --today YYYY-MM-DD

Prints the new stats.json. CloudFront logs only the time, method, path, status, bytes sent and
the byte range of each request (no IP address or user agent), in W3C format, gzipped or not; the
infrastructure repository sets this up. `download-stats.sh` fetches the logs, runs this and
publishes the result.

A file counts once each time a response delivers its last byte: a whole-file 200, or the 206
range that ends the file, so a resumed or parallel-chunk download counts once and HEAD requests,
aborted downloads and link previews don't count. A file's own address (/<version>/<file>) is a
new download; its update copy (the "update" url in releases.json, /updates/<version>/<file>) is the
browser's updater fetching it.

stats.json:
    { "counted_through": "YYYY-MM-DD",
      "downloads": { "<version>/<file>": n },
      "updates":   { "<version>/<file>": n } }

Each run counts the days after counted_through up to two days before --today, so a log that
arrives late is still counted and nothing is counted twice. Logs are kept for a week, so a few
failed runs lose nothing. Runs on Python 3.7 (the AWS CLI image's).
"""

from __future__ import annotations

import argparse
import datetime
import gzip
import json
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit

# Days before today that are not counted yet: CloudFront can deliver a log hours after the hour
# it covers.
LAG_DAYS = 2


def _path(url: str) -> str:
    return unquote(urlsplit(url).path).lstrip("/")


def release_files(releases: dict) -> dict[str, tuple[str, str, int]]:
    """Each published path under the downloads host (no leading /): which table it counts in,
    under which key (the file's own path) and the file's size."""
    files = {}
    for release in releases.get("releases", []):
        for f in release.get("files", []):
            key, size = _path(f["url"]), int(f["size"])
            files[key] = ("downloads", key, size)
            update = f.get("update") or {}
            if update.get("url"):
                files[_path(update["url"])] = ("updates", key, size)
    return files


def read_log(path: Path):
    """The records of one W3C log file as dicts keyed by the #Fields header."""
    opener = gzip.open if path.suffix == ".gz" else open
    fields: list[str] = []
    with opener(path, "rt", encoding="utf-8", errors="replace") as log:
        for line in log:
            line = line.rstrip("\r\n")
            if line.startswith("#Fields:"):
                fields = line[len("#Fields:"):].split()
            elif line and not line.startswith("#") and fields:
                values = line.split("\t") if "\t" in line else line.split()
                if len(values) == len(fields):
                    yield dict(zip(fields, values))


def _int(value: str | None) -> int | None:
    try:
        return int(value) if value not in (None, "", "-") else None
    except ValueError:
        return None


def delivers_last_byte(record: dict, size: int) -> bool:
    """Whether this response sent the file through its last byte."""
    if record.get("cs-method") != "GET":
        return False
    status = record.get("sc-status")
    sent = _int(record.get("sc-bytes")) or 0
    if status == "200":
        return sent >= size  # sc-bytes includes the headers, so a cut-off body falls short
    if status == "206":
        start = _int(record.get("sc-range-start"))
        end = _int(record.get("sc-range-end"))
        if start is None:
            return False
        if end is None or end >= size:
            end = size - 1  # "bytes=N-" runs to the end
        return end == size - 1 and sent >= end - start + 1
    return False


def count(records, files: dict[str, tuple[str, str, int]], first_day: str, last_day: str):
    """Completed downloads and updates per file, for records dated first_day..last_day."""
    tallies: dict[str, dict[str, int]] = {"downloads": {}, "updates": {}}
    for record in records:
        day = record.get("date", "")
        if not first_day <= day <= last_day:
            continue
        found = files.get(unquote(record.get("cs-uri-stem", "")).lstrip("/"))
        if found is not None and delivers_last_byte(record, found[2]):
            tally = tallies[found[0]]
            tally[found[1]] = tally.get(found[1], 0) + 1
    return tallies["downloads"], tallies["updates"]


def _add(total: dict[str, int], more: dict[str, int]) -> dict[str, int]:
    merged = dict(total)
    for key, n in more.items():
        merged[key] = merged.get(key, 0) + n
    return dict(sorted(merged.items()))


def _day(text: str) -> datetime.date:
    return datetime.datetime.strptime(text, "%Y-%m-%d").date()


def update_stats(stats: dict, releases: dict, log_files, today: str) -> dict:
    """stats with the days after its counted_through, up to today - LAG_DAYS, added."""
    last = _day(today) - datetime.timedelta(days=LAG_DAYS)
    counted = stats.get("counted_through")
    first = _day(counted) + datetime.timedelta(days=1) if counted else None
    result = {
        "counted_through": counted,
        "downloads": dict(stats.get("downloads", {})),
        "updates": dict(stats.get("updates", {})),
    }
    if first is not None and first > last:
        return result  # already counted through the last complete day

    def records():
        for path in log_files:
            yield from read_log(path)

    downloads, updates = count(
        records(), release_files(releases), first.isoformat() if first else "", last.isoformat()
    )
    result["counted_through"] = last.isoformat()
    result["downloads"] = _add(result["downloads"], downloads)
    result["updates"] = _add(result["updates"], updates)
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--releases", type=Path, required=True)
    parser.add_argument("--stats", type=Path, help="the current stats.json, if there is one")
    parser.add_argument("--logs", type=Path, required=True, help="folder of log files")
    parser.add_argument("--today", required=True, help="YYYY-MM-DD (UTC)")
    args = parser.parse_args(argv)

    releases = json.loads(args.releases.read_text(encoding="utf-8"))
    stats = {}
    if args.stats and args.stats.exists():
        stats = json.loads(args.stats.read_text(encoding="utf-8"))
    logs = sorted(p for p in args.logs.rglob("*") if p.is_file())
    json.dump(update_stats(stats, releases, logs, args.today), sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
