#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""A release's notes: collected from the commits, and written out as Markdown.

    release_notes.py notes --head v157.0.8103.90-1 --published 157.0.8103.78-3 \\
        --chromium 157.0.8103.90 --previous-chromium 157.0.8103.78 --security true
    release_notes.py markdown releases.json 157.0.8103.90-1

publish-release.sh runs both. `notes` prints a JSON list of sentences, which goes in
releases.json for the website; `markdown` prints a release's entry there as Markdown, for
GitLab's release. A commit that people using the browser would notice says so in a line of its
message, usually next to its sign-off:

    Release-note: Links lose their utm_*, fbclid and other tracking parameters.

A note may wrap onto the following lines; it ends at a blank line or at the next `Key: value`
line (such as Signed-off-by). The notes are those of the commits after the published release
(--published, the newest version in releases.json) up to --head, oldest first, each once. A
commit reverted in the same range ("This reverts commit <sha>.") gives none. The published
release's tag must be in the checkout, which must have full history; when that tag isn't an
ancestor of --head, the notes start after the release tag before --head instead. With no
published release they start at the first commit. A line about Chromium comes first: on a new
Chromium version, the version, and on a security release, that it has security fixes.

LB_RELEASE_NOTES, when set, replaces the commits' notes: one note per line, or notes separated
by "|" (GitLab's form for running a job takes one line), a leading "- " or "* " dropped. It is
for a note whose wording needs fixing after its commit is merged. The Chromium line stays.
Runs on Python 3.7 (the AWS CLI image's).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
NOTE = re.compile(r"release-note:", re.IGNORECASE)
TRAILER = re.compile(r"[A-Za-z0-9][A-Za-z0-9-]*:\s")   # Signed-off-by: …, Co-Authored-By: …
REVERTS = re.compile(r"This reverts commit ([0-9a-f]{7,40})")
RECORD = "\x1e"   # between commits in git log's output
FIELD = "\x00"    # between a commit's hash and its message
MARKDOWN_SPECIAL = re.compile(r"([\\`*_{}\[\]<>()#+!|~@&-])")


class NotesError(Exception):
    pass


def _git(repo: Path, *args: str) -> subprocess.CompletedProcess:
    # The user's log.showSignature would print signature checks into the output.
    return subprocess.run(["git", "-C", str(repo), "-c", "log.showSignature=false", *args],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          universal_newlines=True, check=False)


def _git_out(repo: Path, *args: str) -> str:
    done = _git(repo, *args)
    if done.returncode != 0:
        raise NotesError("git %s failed: %s" % (" ".join(args), done.stderr.strip()))
    return done.stdout


def base_ref(repo: Path, head: str, published: str) -> str | None:
    """Where the notes start: the published release's tag (None: no release published yet)."""
    if _git_out(repo, "rev-parse", "--is-shallow-repository").strip() == "true":
        raise NotesError("the checkout has only part of the history; the notes need all of it")
    if not published:
        return None
    tag = "v" + published
    if _git(repo, "rev-parse", "--verify", "--quiet", tag + "^{commit}").returncode != 0:
        raise NotesError("%s, the published release, isn't in this checkout: fetch the tags" % tag)
    ancestor = _git(repo, "merge-base", "--is-ancestor", tag, head)
    if ancestor.returncode == 0:
        return tag
    if ancestor.returncode != 1:
        raise NotesError("git merge-base %s %s failed: %s" % (tag, head, ancestor.stderr.strip()))
    previous = _git_out(repo, "describe", "--tags", "--abbrev=0", "--match", "v*-*",
                        head + "^").strip()
    print("warning: %s isn't an ancestor of %s; the notes start after %s"
          % (tag, head, previous), file=sys.stderr)
    return previous


def message_notes(message: str) -> list[str]:
    """The Release-note lines of one commit message, each joined with the lines it wraps onto."""
    notes = []
    current = None
    for line in message.splitlines():
        if NOTE.match(line):
            if current:
                notes.append(current)
            current = line[len("release-note:"):].strip()
        elif current is not None:
            text = line.strip()
            if not text or TRAILER.match(line) or text.startswith("(cherry picked from"):
                if current:
                    notes.append(current)
                current = None
            else:
                current = (current + " " + text).strip()
    if current:
        notes.append(current)
    return notes


def commit_notes(repo: Path, base: str | None, head: str) -> list[str]:
    """The notes of the commits in base..head, oldest first, each once, without reverted ones."""
    rev = "%s..%s" % (base, head) if base else head
    out = _git_out(repo, "log", "--format=%H%x00%B%x1e", rev)
    reverted = set()
    kept = []
    for record in out.split(RECORD):   # newest first, so a revert is seen before what it reverts
        if FIELD not in record:
            continue
        sha, message = record.strip("\n").split(FIELD, 1)
        if any(sha.startswith(r) for r in reverted):
            continue   # reverted, and so is whatever this commit itself reverted
        reverted.update(REVERTS.findall(message))
        kept.append(message)
    notes = []
    for message in reversed(kept):
        for note in message_notes(message):
            if note not in notes:
                notes.append(note)
    return notes


def override_notes(text: str) -> list[str]:
    """LB_RELEASE_NOTES: one note per line or per "|", without a leading "- " or "* "."""
    notes = []
    for part in re.split(r"[\n|]", text):
        part = part.strip()
        if part[:2] in ("- ", "* "):
            part = part[2:].strip()
        if part:
            notes.append(part)
    return notes


def chromium_note(chromium: str, previous: str, security: bool) -> str | None:
    if not previous:
        return "Based on Chromium %s." % chromium   # the first release: nothing to compare with
    if chromium != previous:
        return ("Based on Chromium %s, with its security fixes." if security
                else "Based on Chromium %s.") % chromium
    return "Security fixes." if security else None


def release_notes(chromium: str, previous_chromium: str, security: bool,
                  notes: list[str]) -> list[str]:
    first = chromium_note(chromium, previous_chromium, security)
    return ([first] if first else []) + notes


PLATFORMS = {"macos": "macOS", "windows": "Windows", "linux": "Linux", "android": "Android",
             "ios": "iOS"}


def markdown(releases: dict, version: str) -> str:
    """A release's entry in releases.json as Markdown, its notes escaped to read as plain text,
    with the builds published so far: each platform's is published on its own, and the GitLab
    release is updated as each one joins (gitlab_release.py)."""
    release = next((r for r in releases.get("releases", []) if r.get("version") == version), None)
    if release is None:
        raise NotesError("releases.json has no release %s" % version)
    lines = ["Little Browser %s" % version, ""]
    notes = [n for n in release.get("notes") or [] if isinstance(n, str) and n]
    lines += ["- " + MARKDOWN_SPECIAL.sub(r"\\\1", n) for n in notes]
    if notes:
        lines.append("")
    builds = [" ".join([PLATFORMS.get(f.get("platform"), str(f.get("platform")))]
                       + [str(f[k]) for k in ("arch", "format") if f.get(k)])
              for f in release.get("files") or [] if isinstance(f, dict) and f.get("platform")]
    if builds:
        lines += ["Builds: " + MARKDOWN_SPECIAL.sub(r"\\\1", ", ".join(builds)), ""]
    lines.append("Download: https://littlebrowser.app/downloads")
    return "\n".join(lines) + "\n"


def _notes_command(args: argparse.Namespace) -> list[str]:
    override = os.environ.get("LB_RELEASE_NOTES", "").strip()
    notes = override_notes(override) if override else []
    if not notes:
        base = base_ref(args.repo, args.head, args.published)
        notes = commit_notes(args.repo, base, args.head)
        if not notes:
            print("warning: no Release-note lines in the commits since %s"
                  % (base or "the first commit"), file=sys.stderr)
    return release_notes(args.chromium, args.previous_chromium, args.security == "true", notes)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description="A release's notes.")
    sub = p.add_subparsers(dest="command")
    n = sub.add_parser("notes", help="print the notes as a JSON list")
    n.add_argument("--head", required=True, help="the release's tag, e.g. v157.0.8103.90-1")
    n.add_argument("--published", default="", help="the newest version in releases.json")
    n.add_argument("--chromium", required=True)
    n.add_argument("--previous-chromium", default="")
    n.add_argument("--security", choices=("true", "false"), required=True)
    n.add_argument("--repo", type=Path, default=REPO)
    m = sub.add_parser("markdown", help="print a release in releases.json as Markdown")
    m.add_argument("releases", type=Path)
    m.add_argument("version")
    args = p.parse_args(argv)
    try:
        if args.command == "notes":
            json.dump(_notes_command(args), sys.stdout, ensure_ascii=False)
            sys.stdout.write("\n")
        elif args.command == "markdown":
            with open(str(args.releases), encoding="utf-8") as f:
                sys.stdout.write(markdown(json.load(f), args.version))
        else:
            p.error("a command is needed: notes or markdown")
    except NotesError as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
