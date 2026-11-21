# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Tests for download_stats.py: python3 -I -m unittest discover -s desktop/scripts"""

import gzip
import io
import json
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

import download_stats as ds

SIZE = 1000
V1 = "157.0.8103.78-1/LittleBrowser-157.0.8103.78-1-macos-arm64.dmg"
V2 = "157.0.8103.78-2/LittleBrowser-157.0.8103.78-2-macos-arm64.dmg"
RELEASES = {
    "releases": [
        {"version": "157.0.8103.78-2",
         "files": [{"url": "https://downloads.littlebrowser.app/" + V2, "size": SIZE,
                    "update": {"url": "https://downloads.littlebrowser.app/updates/" + V2}}]},
        {"version": "157.0.8103.78-1",
         "files": [{"url": "https://downloads.littlebrowser.app/" + V1, "size": SIZE,
                    "update": {"url": "https://downloads.littlebrowser.app/updates/" + V1}}]},
    ]
}
FIELDS = ["date", "time", "cs-method", "cs-uri-stem", "sc-status", "sc-bytes",
          "sc-range-start", "sc-range-end"]


def rec(path="/" + V1, status="200", sent=SIZE + 300, start="-", end="-", method="GET",
        date="2026-11-20"):
    return {"date": date, "time": "10:00:00", "cs-method": method, "cs-uri-stem": path,
            "sc-status": status, "sc-bytes": str(sent), "sc-range-start": start,
            "sc-range-end": end}


def w3c(records, fields=FIELDS):
    lines = ["#Version: 1.0", "#Fields: " + " ".join(fields)]
    lines += ["\t".join(r[f] for f in fields) for r in records]
    return "\n".join(lines) + "\n"


class DeliversLastByteTest(unittest.TestCase):
    def test_whole_file(self):
        self.assertTrue(ds.delivers_last_byte(rec(), SIZE))

    def test_cut_off_body(self):
        self.assertFalse(ds.delivers_last_byte(rec(sent=SIZE - 1), SIZE))

    def test_head_and_options_never_count(self):
        self.assertFalse(ds.delivers_last_byte(rec(method="HEAD"), SIZE))
        self.assertFalse(ds.delivers_last_byte(rec(method="OPTIONS"), SIZE))

    def test_errors_and_not_modified_never_count(self):
        for status in ("304", "403", "404", "500"):
            self.assertFalse(ds.delivers_last_byte(rec(status=status), SIZE), status)

    def test_range_that_ends_the_file(self):
        self.assertTrue(ds.delivers_last_byte(
            rec(status="206", sent=600, start="500", end=str(SIZE - 1)), SIZE))

    def test_open_ended_range(self):
        self.assertTrue(ds.delivers_last_byte(rec(status="206", sent=600, start="500"), SIZE))

    def test_range_past_the_end_is_clamped(self):
        self.assertTrue(ds.delivers_last_byte(
            rec(status="206", sent=600, start="500", end="99999"), SIZE))

    def test_range_in_the_middle(self):
        self.assertFalse(ds.delivers_last_byte(
            rec(status="206", sent=600, start="0", end="499"), SIZE))

    def test_range_that_ends_the_file_but_was_cut_off(self):
        self.assertFalse(ds.delivers_last_byte(
            rec(status="206", sent=100, start="500", end=str(SIZE - 1)), SIZE))

    def test_suffix_range_without_start(self):
        self.assertFalse(ds.delivers_last_byte(rec(status="206", sent=600, end="499"), SIZE))


class CountTest(unittest.TestCase):
    def count(self, records, first="2026-11-01", last="2026-11-30"):
        return ds.count(records, ds.release_files(RELEASES), first, last)

    def test_downloads_and_updates_are_apart(self):
        downloads, updates = self.count([
            rec(), rec(), rec(path="/updates/" + V1), rec(path="/" + V2),
        ])
        self.assertEqual(downloads, {V1: 2, V2: 1})
        self.assertEqual(updates, {V1: 1})

    def test_update_copy_is_found_by_its_address_in_releases_json(self):
        releases = {"releases": [{"files": [{
            "url": "https://downloads.littlebrowser.app/" + V1, "size": SIZE,
            "update": {"url": "https://downloads.littlebrowser.app/elsewhere/u.dmg"}}]}]}
        downloads, updates = ds.count([rec(path="/elsewhere/u.dmg"), rec(path="/updates/" + V1)],
                                      ds.release_files(releases), "2026-11-01", "2026-11-30")
        self.assertEqual((downloads, updates), ({}, {V1: 1}))

    def test_release_without_an_update_copy(self):
        releases = {"releases": [{"files": [
            {"url": "https://downloads.littlebrowser.app/" + V1, "size": SIZE}]}]}
        downloads, updates = ds.count([rec(), rec(path="/updates/" + V1)],
                                      ds.release_files(releases), "2026-11-01", "2026-11-30")
        self.assertEqual((downloads, updates), ({V1: 1}, {}))

    def test_resumed_download_counts_once(self):
        downloads, _ = self.count([
            rec(status="206", sent=400, start="0", end="399"),
            rec(status="206", sent=600, start="400", end=str(SIZE - 1)),
        ])
        self.assertEqual(downloads, {V1: 1})

    def test_unknown_paths_are_ignored(self):
        downloads, updates = self.count([
            rec(path="/releases.json"), rec(path="/stats.json"),
            rec(path="/updates/macos-arm64.xml"), rec(path="/nope.dmg"),
        ])
        self.assertEqual((downloads, updates), ({}, {}))

    def test_url_encoded_path(self):
        downloads, _ = self.count([rec(path="/" + V1.replace("-", "%2D"))])
        self.assertEqual(downloads, {V1: 1})

    def test_days_outside_the_range_are_ignored(self):
        downloads, _ = self.count([rec(date="2026-11-19"), rec(date="2026-11-20"),
                                   rec(date="2026-11-21")], "2026-11-20", "2026-11-20")
        self.assertEqual(downloads, {V1: 1})


class ReadLogTest(unittest.TestCase):
    def test_gzipped_and_plain_logs(self):
        with tempfile.TemporaryDirectory() as tmp:
            plain = Path(tmp, "a.log")
            plain.write_text(w3c([rec()]))
            zipped = Path(tmp, "b.log.gz")
            with gzip.open(zipped, "wt") as f:
                f.write(w3c([rec(), rec()]))
            self.assertEqual(len(list(ds.read_log(plain))), 1)
            self.assertEqual(len(list(ds.read_log(zipped))), 2)

    def test_columns_follow_the_fields_header(self):
        fields = list(reversed(FIELDS))
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp, "a.log")
            log.write_text(w3c([rec()], fields))
            self.assertEqual(list(ds.read_log(log)), [rec()])

    def test_malformed_lines_are_skipped(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp, "a.log")
            log.write_text(w3c([rec()]) + "too\tfew\n")
            self.assertEqual(len(list(ds.read_log(log))), 1)

    def test_lines_before_a_fields_header_are_skipped(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp, "a.log")
            log.write_text("\t".join(rec().values()) + "\n")
            self.assertEqual(list(ds.read_log(log)), [])


class UpdateStatsTest(unittest.TestCase):
    def run_stats(self, stats, records, today):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp, "x.log")
            log.write_text(w3c(records))
            return ds.update_stats(stats, RELEASES, [log], today)

    def test_first_run_counts_through_two_days_ago(self):
        stats = self.run_stats({}, [rec(date="2026-11-18"), rec(date="2026-11-20"),
                                    rec(date="2026-11-21")], "2026-11-22")
        self.assertEqual(stats, {"counted_through": "2026-11-20",
                                 "downloads": {V1: 2}, "updates": {}})

    def test_adds_to_the_totals_without_counting_a_day_twice(self):
        before = {"counted_through": "2026-11-19", "downloads": {V1: 5}, "updates": {V2: 1}}
        records = [rec(date="2026-11-19"), rec(date="2026-11-20"),
                   rec(path="/updates/" + V2, date="2026-11-20")]
        stats = self.run_stats(before, records, "2026-11-22")
        self.assertEqual(stats, {"counted_through": "2026-11-20",
                                 "downloads": {V1: 6}, "updates": {V2: 2}})

    def test_second_run_on_the_same_day_changes_nothing(self):
        before = {"counted_through": "2026-11-20", "downloads": {V1: 6}, "updates": {}}
        stats = self.run_stats(before, [rec(date="2026-11-20")], "2026-11-22")
        self.assertEqual(stats, before)

    def test_catches_up_after_missed_runs(self):
        before = {"counted_through": "2026-11-15", "downloads": {}, "updates": {}}
        records = [rec(date=d) for d in ("2026-11-15", "2026-11-16", "2026-11-18", "2026-11-20")]
        stats = self.run_stats(before, records, "2026-11-22")
        self.assertEqual(stats["downloads"], {V1: 3})
        self.assertEqual(stats["counted_through"], "2026-11-20")

    def test_keeps_counts_of_files_no_longer_listed(self):
        before = {"counted_through": "2026-11-19", "downloads": {"old/file.dmg": 3}, "updates": {}}
        stats = self.run_stats(before, [], "2026-11-22")
        self.assertEqual(stats["downloads"], {"old/file.dmg": 3})


class MainTest(unittest.TestCase):
    def test_prints_the_new_stats(self):
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, "releases.json").write_text(json.dumps(RELEASES))
            logs = Path(tmp, "logs", "2026", "11", "20")
            logs.mkdir(parents=True)
            with gzip.open(logs / "x.log.gz", "wt") as f:
                f.write(w3c([rec(), rec(path="/updates/" + V2)]))
            out = io.StringIO()
            with redirect_stdout(out):
                ds.main(["--releases", str(Path(tmp, "releases.json")),
                         "--stats", str(Path(tmp, "missing.json")),
                         "--logs", str(Path(tmp, "logs")), "--today", "2026-11-22"])
            self.assertEqual(json.loads(out.getvalue()), {
                "counted_through": "2026-11-20", "downloads": {V1: 1}, "updates": {V2: 1}})


if __name__ == "__main__":
    unittest.main()
