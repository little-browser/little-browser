// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of site/downloads.js, the website's choice of build:
// node --test desktop/scripts/test_site_downloads.js

'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const D = require(path.join(__dirname, '../../site/downloads.js'));

const file = (platform, arch, extra = {}) => ({ platform, arch, url: `https://dl/${platform}-${arch}`, ...extra });
const release = (version, ...files) => ({ version, files });
const all = () => true;
const versions = (rows) => rows.map((r) => r.version);
const keys = (builds) => builds.map((b) => `${b.key}@${b.release.version}`);

test('versionParts reads <chromium version>-<revision> only', () => {
  assert.deepEqual(D.versionParts('157.0.8103.78-5'), [157, 0, 8103, 78, 5]);
  assert.equal(D.versionParts('157.0.8103.78'), null);
  assert.equal(D.versionParts('v157.0.8103.78-5'), null);
  assert.equal(D.versionParts(undefined), null);
});

test('releases sort newest first by number, not as text', () => {
  const rows = D.releaseRows([
    release('157.0.8103.78-2', file('macos', 'arm64')),
    release('157.0.8103.78-10', file('macos', 'arm64')),
    release('158.0.8200.1-1', file('macos', 'arm64')),
    release('157.0.8103.9-1', file('macos', 'arm64')),
  ], all);
  assert.deepEqual(versions(rows), ['158.0.8200.1-1', '157.0.8103.78-10', '157.0.8103.78-2', '157.0.8103.9-1']);
});

test('a version of another form goes last, keeping its place among such', () => {
  const rows = D.releaseRows([
    release('nightly-b', file('macos', 'arm64')),
    release('157.0.8103.78-1', file('macos', 'arm64')),
    release('nightly-a', file('macos', 'arm64')),
  ], all);
  assert.deepEqual(versions(rows), ['157.0.8103.78-1', 'nightly-b', 'nightly-a']);
});

test('releaseRows drops bad entries, unusable files and releases left without files', () => {
  const rows = D.releaseRows([
    null,
    { files: [file('macos', 'arm64')] },
    release('157.0.8103.78-3'),
    release('157.0.8103.78-2', file('macos', 'arm64', { url: 'https://elsewhere/x' })),
    release('157.0.8103.78-1', file('macos', 'arm64'), { arch: 'x64' }, null, file('linux', 'x64', { url: 'https://elsewhere/y' })),
  ], (f) => f.url.startsWith('https://dl/'));
  assert.deepEqual(versions(rows), ['157.0.8103.78-1']);
  assert.deepEqual(rows[0].files.map((f) => f.platform), ['macos']);
  assert.deepEqual(D.releaseRows(undefined, all), []);
  assert.deepEqual(D.releaseRows([release('1.2.3.4-1', ...'not files')], all), []);
});

test('releaseRows leaves the list it was given alone', () => {
  const list = [release('157.0.8103.78-1', file('macos', 'arm64')), release('157.0.8103.78-2', file('macos', 'arm64'))];
  D.releaseRows(list, () => true);
  assert.deepEqual(versions(list), ['157.0.8103.78-1', '157.0.8103.78-2']);
});

test('a build is a platform, a chip and a format', () => {
  assert.equal(D.buildKey(file('macos', 'arm64')), 'macos/arm64/');
  assert.equal(D.buildKey({ platform: 'linux', arch: 'x64', format: 'deb' }), 'linux/x64/deb');
  assert.equal(D.buildKey({ platform: 'ios' }), 'ios//');
});

// The case the site has to handle: -5 is out for Apple silicon, Intel is still on -4.
const staggered = () => D.releaseRows([
  release('157.0.8103.78-5', file('macos', 'arm64')),
  release('157.0.8103.78-4', file('macos', 'arm64'), file('macos', 'x64')),
  release('157.0.8103.78-3', file('macos', 'arm64'), file('macos', 'x64')),
], all);

test('each build current file is in the newest release that has it', () => {
  assert.deepEqual(keys(D.currentBuilds(staggered())), ['macos/arm64/@157.0.8103.78-5', 'macos/x64/@157.0.8103.78-4']);
  assert.deepEqual(D.currentBuilds([]), []);
});

test('missingBuilds lists builds a release is ahead of, and nothing for older releases', () => {
  const rows = staggered();
  const builds = D.currentBuilds(rows);
  assert.deepEqual(keys(D.missingBuilds(builds, rows[0])), ['macos/x64/@157.0.8103.78-4']);
  assert.deepEqual(D.missingBuilds(builds, rows[1]), []);
  assert.deepEqual(D.missingBuilds(builds, rows[2]), []);
});

test('a release that skipped a build is not shown as waiting for it', () => {
  const rows = D.releaseRows([
    release('157.0.8103.78-3', file('macos', 'arm64'), file('macos', 'x64')),
    release('157.0.8103.78-2', file('macos', 'arm64')),
    release('157.0.8103.78-1', file('macos', 'arm64'), file('macos', 'x64')),
  ], all);
  const builds = D.currentBuilds(rows);
  assert.deepEqual(rows.map((r) => D.missingBuilds(builds, r).length), [0, 0, 0]);
});

test('platformBuilds lists one platform, its usual chip first', () => {
  const rows = D.releaseRows([
    release('157.0.8103.78-5', file('macos', 'x64'), file('linux', 'x64')),
    release('157.0.8103.78-4', file('macos', 'arm64')),
  ], all);
  const builds = D.currentBuilds(rows);
  assert.deepEqual(keys(D.platformBuilds(builds, 'macos', 'arm64')), ['macos/arm64/@157.0.8103.78-4', 'macos/x64/@157.0.8103.78-5']);
  assert.deepEqual(keys(D.platformBuilds(builds, 'macos', undefined)), ['macos/x64/@157.0.8103.78-5', 'macos/arm64/@157.0.8103.78-4']);
  assert.deepEqual(D.platformBuilds(builds, 'windows', 'x64'), []);
});

test('a reported chip gets its own build, even from an older release', () => {
  const builds = D.currentBuilds(staggered());
  const intel = D.pickBuild(builds, 'macos', 'x64', 'arm64');
  assert.equal(intel.build.key, 'macos/x64/');
  assert.equal(intel.build.release.version, '157.0.8103.78-4');
  assert.deepEqual(intel.others, []);
  assert.equal(D.pickBuild(builds, 'macos', 'arm64', 'arm64').build.release.version, '157.0.8103.78-5');
});

test('a reported chip with no build gets none, not another chip build', () => {
  const builds = D.currentBuilds(D.releaseRows([release('157.0.8103.78-1', file('macos', 'arm64'))], all));
  assert.deepEqual(D.pickBuild(builds, 'macos', 'x64', 'arm64'), { build: null, others: [] });
});

test('an unreported chip gets the usual one, with the other chips offered', () => {
  const builds = D.currentBuilds(staggered());
  const guess = D.pickBuild(builds, 'macos', null, 'arm64');
  assert.equal(guess.build.key, 'macos/arm64/');
  assert.deepEqual(keys(guess.others), ['macos/x64/@157.0.8103.78-4']);
});

test('an unreported chip gets the only build there is, whatever its chip', () => {
  const builds = D.currentBuilds(D.releaseRows([release('157.0.8103.78-1', file('macos', 'x64'))], all));
  const guess = D.pickBuild(builds, 'macos', null, 'arm64');
  assert.equal(guess.build.key, 'macos/x64/');
  assert.deepEqual(guess.others, []);
});

test('others has one build per chip, not one per format', () => {
  const builds = D.currentBuilds(D.releaseRows([release('157.0.8103.78-1',
    { platform: 'linux', arch: 'x64', format: 'deb', url: 'a' },
    { platform: 'linux', arch: 'x64', format: 'rpm', url: 'b' },
    { platform: 'linux', arch: 'arm64', format: 'deb', url: 'c' },
    { platform: 'linux', arch: 'arm64', format: 'rpm', url: 'd' })], all));
  const guess = D.pickBuild(builds, 'linux', null, 'x64');
  assert.equal(guess.build.key, 'linux/x64/deb');
  assert.deepEqual(guess.others.map((b) => b.key), ['linux/arm64/deb']);
});

test('a build without a chip suits every chip', () => {
  const builds = D.currentBuilds(D.releaseRows([release('157.0.8103.78-1', { platform: 'android', url: 'a' })], all));
  assert.equal(D.pickBuild(builds, 'android', 'arm64', 'arm64').build.key, 'android//');
  assert.equal(D.pickBuild(builds, 'android', null, 'arm64').build.key, 'android//');
});

test('no build for the platform, or no platform', () => {
  const builds = D.currentBuilds(staggered());
  assert.deepEqual(D.pickBuild(builds, 'windows', 'x64', 'x64'), { build: null, others: [] });
  assert.deepEqual(D.pickBuild(builds, null, null, undefined), { build: null, others: [] });
  assert.deepEqual(D.pickBuild([], 'macos', null, 'arm64'), { build: null, others: [] });
});

// Chip detection: what the browser's hints (Chromium's getHighEntropyValues) or user agent say.
const LINUX_UA = 'Mozilla/5.0 (X11; Linux x86_64; rv:140.0) Gecko/20100101 Firefox/140.0';

test('the hints name the chip, 64-bit only', () => {
  assert.equal(D.archFromBrowser('macos', { architecture: 'arm', bitness: '64' }, true, ''), 'arm64');
  assert.equal(D.archFromBrowser('windows', { architecture: 'x86', bitness: '64' }, true, ''), 'x64');
  assert.equal(D.archFromBrowser('linux', { architecture: 'arm' }, true, ''), 'arm64');
  assert.equal(D.archFromBrowser('linux', { architecture: 'arm', bitness: '32' }, true, ''), null);
  assert.equal(D.archFromBrowser('windows', { architecture: 'x86', bitness: '32' }, true, ''), null);
  assert.equal(D.archFromBrowser('macos', { architecture: '' }, true, ''), null);
  assert.equal(D.archFromBrowser('macos', { architecture: 'constructor' }, true, ''), null);
});

test('only a Linux browser without hints is read from its user agent', () => {
  assert.equal(D.archFromBrowser('linux', null, false, LINUX_UA), 'x64');
  assert.equal(D.archFromBrowser('linux', null, false, 'Mozilla/5.0 (X11; Linux aarch64; rv:140.0)'), 'arm64');
  assert.equal(D.archFromBrowser('linux', null, false, 'Mozilla/5.0 (X11; Linux armv7l)'), null);
  // A Mac's user agent says Intel on every chip; Windows' isn't reliable.
  assert.equal(D.archFromBrowser('macos', null, false, 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)'), null);
  assert.equal(D.archFromBrowser('windows', null, false, 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'), null);
});

test('a Chromium user agent is frozen to x86_64, so it never counts', () => {
  // 32-bit ARM Chromium (a Raspberry Pi), and Chromium whose hints failed.
  assert.equal(D.archFromBrowser('linux', { architecture: 'arm', bitness: '32' }, true, LINUX_UA), null);
  assert.equal(D.archFromBrowser('linux', null, true, LINUX_UA), null);
});

test('phones and tablets have no chip choice', () => {
  assert.equal(D.archFromBrowser('android', { architecture: 'arm', bitness: '64' }, true, ''), null);
  assert.equal(D.archFromBrowser('ios', null, false, ''), null);
  assert.equal(D.archFromBrowser(null, null, false, LINUX_UA), null);
});

// The download button: a build to offer, or why there is none.
test('offer: the build, and the newer release it lags behind', () => {
  const rows = staggered();
  const builds = D.currentBuilds(rows);
  const intel = D.offer(rows, builds, 'macos', 'x64', 'arm64');
  assert.equal(intel.build.key, 'macos/x64/');
  assert.equal(intel.newer.version, '157.0.8103.78-5');
  const arm = D.offer(rows, builds, 'macos', 'arm64', 'arm64');
  assert.equal(arm.build.release.version, '157.0.8103.78-5');
  assert.equal(arm.newer, null);
  const guess = D.offer(rows, builds, 'macos', null, 'arm64');
  assert.deepEqual(keys(guess.others), ['macos/x64/@157.0.8103.78-4']);
});

test('offer: why there is nothing to offer', () => {
  const rows = D.releaseRows([release('157.0.8103.78-1', file('macos', 'arm64'))], all);
  const builds = D.currentBuilds(rows);
  assert.deepEqual(D.offer([], [], 'macos', 'arm64', 'arm64'), { why: 'first' });
  assert.deepEqual(D.offer(rows, builds, 'macos', 'x64', 'arm64'), { why: 'chip' });
  assert.deepEqual(D.offer(rows, builds, 'windows', 'x64', 'x64'), { why: 'platform' });
  assert.deepEqual(D.offer(rows, builds, 'windows', null, 'x64'), { why: 'platform' });
  assert.deepEqual(D.offer(rows, builds, null, null, undefined), { why: null });
});
