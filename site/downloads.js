// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// littlebrowser.app: which build of which release to offer, from releases.json. No page access:
// site.js draws the result, and desktop/scripts/test_site_downloads.js tests this file in Node.
//
// Each platform's builds are published on their own, so one release can have a build for Apple
// silicon hours or days before its Intel build, or Linux's. A build is a platform, a chip ("arch")
// and, where a platform ships several, a file format; its current file is the one in the newest
// release that has that build.

(function (exports) {
  // "157.0.8103.78-5" -> [157, 0, 8103, 78, 5]; null for anything else.
  const versionParts = (v) => {
    const m = /^(\d+)\.(\d+)\.(\d+)\.(\d+)-(\d+)$/.exec(String(v));
    return m ? m.slice(1).map(Number) : null;
  };

  // Sort order for releases, newest first: below 0 when a is newer than b. A version of another
  // form goes after the others.
  const compareReleases = (a, b) => {
    const x = versionParts(a.version);
    const y = versionParts(b.version);
    if (!x || !y) return (x ? 0 : 1) - (y ? 0 : 1);
    for (let i = 0; i < x.length; i++) if (x[i] !== y[i]) return y[i] - x[i];
    return 0;
  };

  // The releases with a version and at least one file that `usable` accepts (the page only links to
  // its own download host), each with only those files, newest first whatever order the list had.
  const releaseRows = (releases, usable) => (Array.isArray(releases) ? releases : [])
    .filter((r) => r && typeof r.version === 'string' && r.version)
    .map((r) => ({
      ...r,
      files: (Array.isArray(r.files) ? r.files : [])
        .filter((f) => f && typeof f.platform === 'string' && f.platform && usable(f)),
    }))
    .filter((r) => r.files.length)
    .sort(compareReleases);

  const buildKey = (f) => [f.platform, f.arch || '', f.format || ''].join('/');

  // Each build's current file, as { key, file, release }, in the order the builds first appear in
  // the rows (newest first).
  const currentBuilds = (rows) => {
    const seen = new Map();
    for (const release of rows) {
      for (const file of release.files) {
        const key = buildKey(file);
        if (!seen.has(key)) seen.set(key, { key, file, release });
      }
    }
    return [...seen.values()];
  };

  // The builds that a release doesn't have yet: their current file is in an older release.
  const missingBuilds = (builds, release) => builds.filter((b) => compareReleases(release, b.release) < 0);

  // A platform's builds, its usual chip (`preferred`) first.
  const platformBuilds = (builds, platform, preferred) => {
    const mine = builds.filter((b) => b.file.platform === platform);
    return [...mine.filter((b) => b.file.arch === preferred), ...mine.filter((b) => b.file.arch !== preferred)];
  };

  // The build to offer a visitor on `os`, as { build, others }. `arch` is the chip the browser
  // reported, or null. Reported: the build for that chip, or none (another chip's build may not
  // run). Not reported: the platform's usual chip, else its first build, with the newest build for
  // each other chip in `others`, so the visitor can pick theirs.
  const pickBuild = (builds, os, arch, preferred) => {
    const mine = platformBuilds(builds, os, preferred);
    if (arch) return { build: mine.find((b) => !b.file.arch || b.file.arch === arch) || null, others: [] };
    const build = mine[0] || null;
    const others = [];
    for (const b of mine) {
      if (b.file.arch === (build && build.file.arch)) continue;
      if (!others.some((o) => o.file.arch === b.file.arch)) others.push(b);
    }
    return { build, others };
  };

  // What the download button offers: { build, others, newer }, newer being the newest release
  // when this build's file is in an older one; or { why } when there is nothing to offer: 'first'
  // (no build yet), 'chip' (this platform has builds, not for this chip), 'platform', or null
  // (system unknown).
  const offer = (rows, builds, os, arch, preferred) => {
    const { build, others } = pickBuild(builds, os, arch, preferred);
    if (build) return { build, others, newer: compareReleases(rows[0], build.release) < 0 ? rows[0] : null };
    if (!rows.length) return { why: 'first' };
    if (arch && builds.some((b) => b.file.platform === os)) return { why: 'chip' };
    return { why: os ? 'platform' : null };
  };

  // The chip a computer's browser runs on, or null. hints: Chromium's high-entropy client hints
  // ({ architecture, bitness }), null when it has none or they failed; hasHints: whether the
  // browser has client hints at all; ua: its user agent. Only 64-bit chips have builds. Without
  // hints, a Linux browser names its chip in the user agent; a Chromium browser's user agent is
  // frozen to x86_64 whatever the chip, a Mac's says Intel on every chip and Windows' isn't
  // reliable, so none of those count.
  const archFromBrowser = (os, hints, hasHints, ua) => {
    if (!['macos', 'windows', 'linux'].includes(os)) return null;
    if (hints) {
      if (hints.bitness === '32') return null;
      return hints.architecture === 'arm' ? 'arm64' : hints.architecture === 'x86' ? 'x64' : null;
    }
    if (os !== 'linux' || hasHints) return null;
    return /aarch64|arm64/i.test(ua) ? 'arm64' : /x86_64|amd64/i.test(ua) ? 'x64' : null;
  };

  Object.assign(exports, { versionParts, compareReleases, releaseRows, buildKey, currentBuilds,
    missingBuilds, platformBuilds, pickBuild, offer, archFromBrowser });
})(typeof module === 'object' && module.exports ? module.exports : (window.LBDownloads = {}));
