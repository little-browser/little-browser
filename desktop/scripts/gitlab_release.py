#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""GitLab's release for a tag: created by the first build published for it, updated by the rest.

    gitlab_release.py v157.0.8103.78-1 macos-arm64 https://downloads.littlebrowser.app/releases.json

CI only: each platform's `release-<platform>-<arch>` job runs it after that build's publish job,
naming that build. Each build of a release is published on its own, hours apart, so the text is
written from the releases.json the website reads (release_notes.py markdown: the notes and the
builds out so far), not from what one publish job saw: whichever job runs last writes every
build, in whatever order the jobs ran. Until releases.json lists the named build (CloudFront can
serve the previous one for a while after a publish) it reads it again, for up to 15 minutes.
The release is created when the tag has none, and updated when its text differs; a job that
finds it created by another meanwhile updates it. The text is generated: an edit made by hand
on GitLab is replaced by the next build's job, so a note's wording is fixed in its Release-note
line or with LB_RELEASE_NOTES on the release's first publish job.

Needs CI_API_V4_URL, CI_PROJECT_ID and CI_JOB_TOKEN, which GitLab sets in every job.
"""

from __future__ import annotations

import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Callable, Optional, Tuple

# CI runs this with python3 -I, which leaves the script's own folder off the module path.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import release_notes  # noqa: E402

# request(method, path under the API, JSON body or None) -> (HTTP status, decoded JSON or None)
Request = Callable[[str, str, Optional[dict]], Tuple[int, Optional[dict]]]


class ReleaseError(Exception):
    pass


def sync(project: str, tag: str, description: str, request: Request) -> str:
    """Creates or updates the tag's release; says which ("created", "updated", "unchanged")."""
    releases = "/projects/%s/releases" % urllib.parse.quote(project, safe="")
    one = "%s/%s" % (releases, urllib.parse.quote(tag, safe=""))
    for _ in range(2):
        status, body = request("GET", one, None)
        if status == 404:
            status, body = request("POST", releases,
                                   {"tag_name": tag, "name": tag, "description": description})
            if status == 409:   # another job created it in the meantime: update that one
                continue
            if status != 201:
                raise ReleaseError("creating the release for %s: HTTP %d %s" % (tag, status, body))
            return "created"
        if status != 200:
            raise ReleaseError("reading the release for %s: HTTP %d %s" % (tag, status, body))
        if (body or {}).get("description") == description:
            return "unchanged"
        status, body = request("PUT", one, {"description": description})
        if status != 200:
            raise ReleaseError("updating the release for %s: HTTP %d %s" % (tag, status, body))
        return "updated"
    raise ReleaseError("the release for %s kept changing" % tag)


def _api(base: str, token: str) -> Request:
    def request(method: str, path: str, data: Optional[dict]) -> Tuple[int, Optional[dict]]:
        req = urllib.request.Request(
            base.rstrip("/") + path, method=method,
            data=None if data is None else json.dumps(data).encode(),
            headers={"JOB-TOKEN": token, "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return r.status, json.loads(r.read() or b"null")
        except urllib.error.HTTPError as e:
            raw = e.read()
            try:
                return e.code, json.loads(raw or b"null")
            except ValueError:
                return e.code, {"error": raw.decode(errors="replace")}
    return request


def _releases_json(url: str) -> dict:
    req = urllib.request.Request(url, headers={"Cache-Control": "no-cache"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.loads(r.read())


def has_build(releases: dict, version: str, build: str) -> bool:
    """Whether releases.json lists the version with a file for build ("<platform>-<arch>")."""
    for r in releases.get("releases", []):
        if r.get("version") == version:
            return any("%s-%s" % (f.get("platform"), f.get("arch")) == build
                       for f in r.get("files") or [] if isinstance(f, dict))
    return False


def published(url: str, version: str, build: str, fetch: Callable[[str], dict],
              wait: float = 900, pause: float = 30,
              sleep: Callable[[float], None] = time.sleep) -> dict:
    """releases.json once it lists the build, read again every `pause` s for up to `wait` s."""
    waited = 0.0
    while True:
        releases = fetch(url)
        if has_build(releases, version, build):
            return releases
        if waited >= wait:
            raise ReleaseError("%s doesn't list %s's %s build" % (url, version, build))
        sleep(pause)
        waited += pause


def main(argv: list, env=None, request: Optional[Request] = None,
         fetch: Callable[[str], dict] = _releases_json,
         sleep: Callable[[float], None] = time.sleep) -> int:
    env = os.environ if env is None else env
    if len(argv) != 3 or not argv[0].startswith("v"):
        sys.exit("usage: gitlab_release.py v<version> <platform>-<arch> <releases.json URL>")
    tag, build, url = argv
    if not url.startswith(("https://", "http://")):
        sys.exit("gitlab_release.py: %r is not a releases.json address (is LB_DOWNLOADS_URL set?)"
                 % url)
    missing = [k for k in ("CI_API_V4_URL", "CI_PROJECT_ID", "CI_JOB_TOKEN") if not env.get(k)]
    if missing and request is None:
        sys.exit("gitlab_release.py: run in GitLab CI (no %s)" % ", ".join(missing))
    try:
        releases = published(url, tag[1:], build, fetch, sleep=sleep)
        description = release_notes.markdown(releases, tag[1:])
        api = request or _api(env["CI_API_V4_URL"], env["CI_JOB_TOKEN"])
        outcome = sync(env.get("CI_PROJECT_ID", ""), tag, description, api)
    except (release_notes.NotesError, ReleaseError) as e:
        sys.exit("gitlab_release.py: %s" % e)
    except (urllib.error.URLError, OSError, ValueError) as e:   # network, timeout, bad JSON
        sys.exit("gitlab_release.py: %s: %s" % (type(e).__name__, e))
    print("%s the GitLab release %s:\n%s" % (outcome.capitalize(), tag, description))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
