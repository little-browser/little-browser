#!/usr/bin/env node
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Exports every platform icon set from the master SVGs in branding/svg/.
//
//   cd branding && npm install && npm run export
//
// Output goes to branding/exports/ (wiped and regenerated on each run):
//   macos/    LittleBrowser.icns, icon-1024.png, MenuBarTemplate(.png|@2x.png)
//   windows/  little-browser.ico
//   linux/    hicolor/<n>x<n>/apps/little-browser.png + scalable SVG
//   android/  res/mipmap-*/ (adaptive layers + legacy), play-store-512.png, play-feature-graphic.png
//   ios/      AppIcon.appiconset/ (light, dark, tinted)
//   web/      favicon.ico, favicon.svg, apple-touch-icon.png, icon-192/512.png, social-preview.png
//
// Pure Node + @resvg/resvg-js; no ImageMagick or iconutil needed, so it runs on any OS.

import { Resvg } from '@resvg/resvg-js';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const SVG = path.join(ROOT, 'svg');
const OUT = path.join(ROOT, 'exports');
const APP_ID = 'little-browser';

// Masters. Sizes at or below SMALL_MAX use the small-size drawing (heavier strokes, bigger dot).
const SMALL_MAX = 32;
const src = (name) => fs.readFileSync(path.join(SVG, name), 'utf8');
const masters = {
  icon: src('icon.svg'),
  iconSmall: src('icon-small.svg'),
  macos: src('icon-macos.svg'),
  macosSmall: src('icon-macos-small.svg'),
  square: src('icon-square.svg'),
  iosDark: src('icon-ios-dark.svg'),
  iosTinted: src('icon-ios-tinted.svg'),
  template: src('mark-template.svg'),
  fg: src('android-foreground.svg'),
  bg: src('android-background.svg'),
  mono: src('android-monochrome.svg'),
};
const pick = (big, small) => (size) => (size <= SMALL_MAX ? small : big);
const tile = pick(masters.icon, masters.iconSmall);
const macTile = pick(masters.macos, masters.macosSmall);

// ---------------------------------------------------------------- rendering

function rasterize(svg, size, height = size) {
  const r = new Resvg(svg, { fitTo: { mode: 'width', value: size }, font: { loadSystemFonts: false } });
  const img = r.render();
  if (img.width !== size || img.height !== height) throw new Error(`expected ${size}x${height}px, got ${img.width}x${img.height}`);
  return img;
}
const png = (svg, size) => rasterize(svg, size).asPng();

// RGB PNG without an alpha channel (App Store rejects icons that have one).
function pngOpaque(svg, size, h = size) {
  const { pixels, width, height } = rasterize(svg, size, h);
  const raw = Buffer.alloc((width * 3 + 1) * height);
  for (let y = 0; y < height; y++) {
    const row = y * (width * 3 + 1);
    raw[row] = 0; // filter: none
    for (let x = 0; x < width; x++) {
      const i = (y * width + x) * 4;
      const a = pixels[i + 3] / 255; // composite over black just in case
      raw[row + 1 + x * 3] = Math.round(pixels[i] * a);
      raw[row + 2 + x * 3] = Math.round(pixels[i + 1] * a);
      raw[row + 3 + x * 3] = Math.round(pixels[i + 2] * a);
    }
  }
  const chunk = (type, data) => {
    const len = Buffer.alloc(4);
    len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(zlib.crc32(td) >>> 0);
    return Buffer.concat([len, td, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; // bit depth
  ihdr[9] = 2; // colour type: RGB
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

// ---------------------------------------------------------------- containers

// Windows .ico with PNG-compressed entries (Vista+).
function ico(entries) {
  const head = Buffer.alloc(6 + 16 * entries.length);
  head.writeUInt16LE(0, 0);
  head.writeUInt16LE(1, 2);
  head.writeUInt16LE(entries.length, 4);
  let offset = head.length;
  entries.forEach(({ size, data }, i) => {
    const e = 6 + 16 * i;
    head[e] = size >= 256 ? 0 : size;
    head[e + 1] = size >= 256 ? 0 : size;
    head.writeUInt16LE(1, e + 4); // planes
    head.writeUInt16LE(32, e + 6); // bpp
    head.writeUInt32LE(data.length, e + 8);
    head.writeUInt32LE(offset, e + 12);
    offset += data.length;
  });
  return Buffer.concat([head, ...entries.map((e) => e.data)]);
}

// macOS .icns with PNG entries (10.7+).
function icns(entries) {
  const parts = entries.map(({ type, data }) => {
    const h = Buffer.alloc(8);
    h.write(type, 0, 'ascii');
    h.writeUInt32BE(data.length + 8, 4);
    return Buffer.concat([h, data]);
  });
  const body = Buffer.concat(parts);
  const h = Buffer.alloc(8);
  h.write('icns', 0, 'ascii');
  h.writeUInt32BE(body.length + 8, 4);
  return Buffer.concat([h, body]);
}

// ---------------------------------------------------------------- helpers

function write(rel, data) {
  const file = path.join(OUT, rel);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, data);
  written.push(rel);
}
const written = [];

// Inner markup of an SVG document, for composing layers.
const inner = (svg) => svg.slice(svg.indexOf('>', svg.indexOf('<svg')) + 1, svg.lastIndexOf('</svg>'));

// Android legacy icons: the adaptive layers cropped to the 72 dp visible area.
function androidLegacy(round) {
  const shape = round
    ? '<circle cx="54" cy="54" r="36"/>'
    : '<rect x="18" y="18" width="72" height="72" rx="16"/>';
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="18 18 72 72">
  <defs><clipPath id="lb-crop">${shape}</clipPath></defs>
  <g clip-path="url(#lb-crop)">
    <svg width="108" height="108" viewBox="0 0 108 108">${inner(masters.bg)}</svg>
    <svg width="108" height="108" viewBox="0 0 108 108">${inner(masters.fg)}</svg>
  </g>
</svg>`;
}

// ---------------------------------------------------------------- export

fs.rmSync(OUT, { recursive: true, force: true });

// macOS
write(
  'macos/LittleBrowser.icns',
  icns([
    ['icp4', 16],
    ['icp5', 32],
    ['ic11', 32],
    ['ic12', 64],
    ['ic07', 128],
    ['ic08', 256],
    ['ic13', 256],
    ['ic09', 512],
    ['ic14', 512],
    ['ic10', 1024],
  ].map(([type, size]) => ({ type, data: png(macTile(size), size) }))),
);
write('macos/icon-1024.png', png(masters.macos, 1024));
write('macos/MenuBarTemplate.png', png(masters.template, 18));
write('macos/MenuBarTemplate@2x.png', png(masters.template, 36));

// Windows
write('windows/little-browser.ico', ico([16, 20, 24, 32, 40, 48, 64, 256].map((size) => ({ size, data: png(tile(size), size) }))));

// Linux (freedesktop hicolor theme)
for (const size of [16, 22, 24, 32, 48, 64, 128, 256, 512]) {
  write(`linux/hicolor/${size}x${size}/apps/${APP_ID}.png`, png(tile(size), size));
}
write(`linux/hicolor/scalable/apps/${APP_ID}.svg`, masters.icon);
write(`linux/hicolor/symbolic/apps/${APP_ID}-symbolic.svg`, masters.template);

// Android
const densities = { mdpi: 1, hdpi: 1.5, xhdpi: 2, xxhdpi: 3, xxxhdpi: 4 };
for (const [d, k] of Object.entries(densities)) {
  const layer = Math.round(108 * k);
  const legacy = Math.round(48 * k);
  write(`android/res/mipmap-${d}/ic_launcher_foreground.png`, png(masters.fg, layer));
  write(`android/res/mipmap-${d}/ic_launcher_background.png`, png(masters.bg, layer));
  write(`android/res/mipmap-${d}/ic_launcher_monochrome.png`, png(masters.mono, layer));
  write(`android/res/mipmap-${d}/ic_launcher.png`, png(androidLegacy(false), legacy));
  write(`android/res/mipmap-${d}/ic_launcher_round.png`, png(androidLegacy(true), legacy));
}
const adaptive = `<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@mipmap/ic_launcher_background" />
    <foreground android:drawable="@mipmap/ic_launcher_foreground" />
    <monochrome android:drawable="@mipmap/ic_launcher_monochrome" />
</adaptive-icon>
`;
write('android/res/mipmap-anydpi-v26/ic_launcher.xml', adaptive);
write('android/res/mipmap-anydpi-v26/ic_launcher_round.xml', adaptive);
write('android/play-store-512.png', pngOpaque(masters.square, 512));
// Google Play feature graphic: 1024x500, no alpha (Play rejects transparency).
write('android/play-feature-graphic.png', pngOpaque(src('play-feature-graphic.svg'), 1024, 500));

// iOS (single-size app icon, Xcode 14+; dark and tinted appearances, iOS 18+)
const ios = 'ios/AppIcon.appiconset';
write(`${ios}/AppIcon-1024.png`, pngOpaque(masters.square, 1024));
write(`${ios}/AppIcon-1024-dark.png`, png(masters.iosDark, 1024));
write(`${ios}/AppIcon-1024-tinted.png`, pngOpaque(masters.iosTinted, 1024));
const iosImage = (filename, appearance) => ({
  ...(appearance ? { appearances: [{ appearance: 'luminosity', value: appearance }] } : {}),
  filename,
  idiom: 'universal',
  platform: 'ios',
  size: '1024x1024',
});
write(
  `${ios}/Contents.json`,
  JSON.stringify(
    {
      images: [iosImage('AppIcon-1024.png'), iosImage('AppIcon-1024-dark.png', 'dark'), iosImage('AppIcon-1024-tinted.png', 'tinted')],
      info: { author: 'xcode', version: 1 },
    },
    null,
    2,
  ) + '\n',
);

// Web
write('web/favicon.ico', ico([16, 32, 48].map((size) => ({ size, data: png(tile(size), size) }))));
write('web/favicon.svg', masters.iconSmall);
write('web/apple-touch-icon.png', pngOpaque(masters.square, 180));
write('web/icon-192.png', png(masters.icon, 192));
write('web/icon-512.png', png(masters.icon, 512));
// Social preview (GitHub repo settings, link cards): 1280x640, no alpha.
write('web/social-preview.png', pngOpaque(src('social-preview.svg'), 1280, 640));

console.log(`Wrote ${written.length} files to ${path.relative(process.cwd(), OUT) || '.'}/`);
