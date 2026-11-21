// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// littlebrowser.app: theme switch, product-mock demo, downloads, colour-swatch copy. No tracking, no cookies.

(() => {
  const root = document.documentElement;
  const store = {
    get: (k) => { try { return localStorage.getItem(k); } catch { return null; } },
    set: (k, v) => { try { v == null ? localStorage.removeItem(k) : localStorage.setItem(k, v); } catch {} },
  };

  // Theme: auto (system) / light / dark.
  const applyTheme = (t) => {
    if (t === 'light' || t === 'dark') root.dataset.theme = t;
    else delete root.dataset.theme;
    for (const b of document.querySelectorAll('.theme button')) b.setAttribute('aria-pressed', String(b.dataset.t === (t || 'auto')));
  };
  applyTheme(store.get('lb-theme'));
  document.addEventListener('click', (e) => {
    const b = e.target.closest('.theme button');
    if (!b) return;
    const t = b.dataset.t === 'auto' ? null : b.dataset.t;
    store.set('lb-theme', t);
    applyTheme(t);
  });

  // Product mock: space colours and the Peek "grow" demo.
  const mock = document.querySelector('.mock');
  if (mock) {
    const spaces = [...mock.querySelectorAll('.m-spaces button')];
    const setSpace = (b) => {
      mock.style.setProperty('--ws-hue', b.dataset.h);
      mock.querySelector('.m-ws-icon').textContent = b.textContent.trim();
      mock.querySelector('.m-ws-label').textContent = b.title;
      for (const s of spaces) s.setAttribute('aria-pressed', String(s === b));
    };
    spaces.forEach((b) => b.addEventListener('click', () => setSpace(b)));

    // Grow the card from the link in the article, wherever the text wrapped it. The card and the
    // dot are placed in the whole mock (the window), which the Peek covers.
    const link = mock.querySelector('[data-peek-link]');
    const card = mock.querySelector('.m-card');
    const aim = () => {
      if (!link || !card) return;
      const p = mock.getBoundingClientRect();
      const l = link.getBoundingClientRect();
      const x = l.left + l.width / 2 - p.left;
      const y = l.top + l.height / 2 - p.top;
      mock.style.setProperty('--peek-x', `${x}px`);
      mock.style.setProperty('--peek-y', `${y}px`);
      mock.style.setProperty('--peek-origin', `${x - card.offsetLeft}px ${y - card.offsetTop}px`);
    };

    const peek = () => {
      aim();
      mock.classList.remove('peeking');
      void mock.offsetWidth; // restart the animation
      mock.classList.add('peeking');
    };
    for (const el of document.querySelectorAll('[data-peek]')) {
      el.addEventListener('click', (e) => {
        e.preventDefault();
        const r = mock.getBoundingClientRect();
        if (r.top < 0 || r.bottom > innerHeight) {
          mock.scrollIntoView({ behavior: 'smooth', block: 'center' });
          setTimeout(peek, 450); // let the scroll land before the card grows
        } else peek();
      });
    }
    const io = new IntersectionObserver((entries) => {
      if (entries.some((x) => x.isIntersecting)) { setTimeout(peek, 700); io.disconnect(); }
    }, { threshold: 0.5 });
    io.observe(mock);
  }

  // Downloads: a button for this visitor's system (home page and /downloads), and the list of
  // every release (/downloads). The list is releases.json on downloads.littlebrowser.app, written
  // by the release pipeline:
  //   { "releases": [ { "version": "157.0.8103.78-1", "date": "2026-11-20", "channel": "beta",
  //       "chromium": "157.0.8103.78", "files": [ { "platform": "macos", "arch": "arm64",
  //       "url": "https://downloads.littlebrowser.app/...dmg", "size": 123, "sha256": "..." } ] } ] }
  // Newest first. Next to it, stats.json (desktop/scripts/download_stats.py) counts each file's
  // completed downloads and the browser's updates to it, keyed by the file's path:
  //   { "counted_through": "2026-11-20", "downloads": { "<version>/<file>": 12 }, "updates": { ... } }
  // Nothing here is sent anywhere: the system is guessed locally from the browser.
  const primary = document.querySelector('[data-dl-primary]');
  const meta = document.querySelector('[data-dl-meta]');
  const list = document.querySelector('[data-releases-list]');
  if (primary || list) {
    const RELEASES = 'https://downloads.littlebrowser.app/releases.json';
    const STATS = 'https://downloads.littlebrowser.app/stats.json';
    const DOWNLOAD_ORIGIN = 'https://downloads.littlebrowser.app';
    const PLATFORMS = {
      macos: 'macOS', windows: 'Windows', linux: 'Linux', ios: 'iPhone and iPad', android: 'Android',
    };
    const ARCHES = { arm64: 'Apple silicon', x64: 'Intel' };
    // Store pages for the platforms that ship through a store. An entry here takes over the
    // button on that platform; none is listed yet.
    const STORES = {
      // ios: { url: 'https://apps.apple.com/app/...', label: 'Download on the App Store' },
      // android: { url: 'https://play.google.com/store/apps/details?id=...', label: 'Get it on Google Play' },
    };

    // Best guess at the OS (and on a Mac, the chip). iPadOS reports itself as a Mac with touch.
    const detect = async () => {
      const ua = navigator.userAgent;
      const uad = navigator.userAgentData;
      const p = (uad && uad.platform) || navigator.platform || '';
      let os = null;
      if (/iPhone|iPad|iPod/.test(ua) || (/Mac/i.test(p) && navigator.maxTouchPoints > 1)) os = 'ios';
      else if (/Android/i.test(ua)) os = 'android';
      else if (/Mac/i.test(p)) os = 'macos';
      else if (/Win/i.test(p)) os = 'windows';
      else if (/Linux|X11|CrOS/i.test(p)) os = 'linux';
      let arch = null; // Safari and Firefox don't say; most Macs sold since 2021 are Apple silicon
      if (os === 'macos' && uad && uad.getHighEntropyValues) {
        try {
          const v = await uad.getHighEntropyValues(['architecture']);
          arch = v.architecture === 'arm' ? 'arm64' : v.architecture === 'x86' ? 'x64' : null;
        } catch {}
      }
      return { os, arch };
    };

    // Only ever link to our own download host (local previews may point at a file next to the page).
    const local = ['localhost', '127.0.0.1'].includes(location.hostname);
    const safeUrl = (u) => {
      try {
        const url = new URL(u, location.href);
        return url.origin === DOWNLOAD_ORIGIN || (local && url.origin === location.origin) ? url.href : null;
      } catch { return null; }
    };
    const size = (b) => (b > 0 ? `${Math.round(b / 1048576)} MB` : '');
    const label = (f) => `${PLATFORMS[f.platform] || f.platform}${ARCHES[f.arch] ? ` (${ARCHES[f.arch]})` : ''}`;
    const el = (tag, props = {}, ...kids) => {
      const n = Object.assign(document.createElement(tag), props);
      for (const k of kids) n.append(k);
      return n;
    };
    // "beta" -> "Beta": the release's channel, as written by the release pipeline.
    const channel = (r) => (r && r.channel ? r.channel[0].toUpperCase() + r.channel.slice(1) : 'Beta');
    const svg = (cls, d) => {
      const n = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
      n.setAttribute('viewBox', '0 0 16 16');
      n.setAttribute('class', cls);
      n.setAttribute('aria-hidden', 'true');
      const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
      path.setAttribute('d', d);
      path.setAttribute('fill', 'none');
      path.setAttribute('stroke', 'currentColor');
      path.setAttribute('stroke-width', '1.5');
      path.setAttribute('stroke-linecap', 'round');
      path.setAttribute('stroke-linejoin', 'round');
      n.append(path);
      return n;
    };
    // The full SHA-256, and a button that copies it.
    const shaRow = (sha) => {
      const button = el('button', { type: 'button', className: 'copy', title: 'Copy SHA-256' },
        svg('icon', 'M5.5 5.5V3.5a1 1 0 0 1 1-1h6a1 1 0 0 1 1 1v6a1 1 0 0 1-1 1h-2M3.5 5.5h6a1 1 0 0 1 1 1v6a1 1 0 0 1-1 1h-6a1 1 0 0 1-1-1v-6a1 1 0 0 1 1-1z'),
        svg('done', 'M3.5 8.5l3 3 6-7'));
      const code = el('code', { className: 'sha' }, sha);
      button.setAttribute('aria-label', 'Copy SHA-256');
      button.addEventListener('click', async () => {
        let copied = false;
        try { await navigator.clipboard.writeText(sha); copied = true; } catch {}
        // Where the Clipboard API is refused, select the checksum: copy it the old way, and if
        // that fails too it stays selected for ⌘C.
        const range = document.createRange();
        range.selectNodeContents(code);
        const sel = getSelection();
        sel.removeAllRanges();
        sel.addRange(range);
        if (!copied) {
          try { copied = document.execCommand('copy'); } catch {}
        }
        if (!copied) return;
        sel.removeAllRanges();
        button.classList.add('copied');
        button.title = 'Copied';
        setTimeout(() => { button.classList.remove('copied'); button.title = 'Copy SHA-256'; }, 1500);
      });
      return el('div', { className: 'sha-row' }, 'SHA-256 ', code, button);
    };

    // A JSON file from the download host, or a sample file next to the page in a local preview
    // (?releases=… or ?stats=…). null if it can't be read.
    const fetchJson = async (src, param) => {
      const override = new URLSearchParams(location.search).get(param);
      if (local && override) src = override;
      try {
        const r = await fetch(src, { cache: 'no-cache' });
        return r.ok ? await r.json() : null;
      } catch { return null; }
    };
    const load = async () => {
      const j = await fetchJson(RELEASES, 'releases');
      return j && Array.isArray(j.releases) ? j.releases : [];
    };
    // "1,234 downloads · 56 updates" for a file, or '' before anything is counted.
    const counts = (stats, f) => {
      if (!stats) return '';
      let key;
      try { key = new URL(f.url, location.href).pathname.replace(/^\//, ''); } catch { return ''; }
      const n = (table) => {
        const v = stats[table] && stats[table][key];
        return Number.isInteger(v) && v > 0 ? v : 0;
      };
      const plural = (v, word) => `${v.toLocaleString('en')} ${word}${v === 1 ? '' : 's'}`;
      const downloads = n('downloads');
      const updates = n('updates');
      return [plural(downloads, 'download'), updates ? plural(updates, 'update') : ''].filter(Boolean).join(' · ');
    };

    (async () => {
      const [{ os, arch }, releases, stats] = await Promise.all([
        detect(), load(), list ? fetchJson(STATS, 'stats') : null,
      ]);
      const rows = releases.filter((r) => r && r.version && Array.isArray(r.files) && r.files.length);

      // Platform cards: which ones have a build or a store page, and which one is yours.
      for (const card of document.querySelectorAll('[data-platform]')) {
        const id = card.dataset.platform;
        const status = card.querySelector('[data-platform-status]');
        if (STORES[id]) status.replaceChildren(el('a', { href: STORES[id].url }, STORES[id].label));
        else {
          const newest = rows.find((r) => r.files.some((f) => f.platform === id));
          if (newest) status.textContent = `${channel(newest)} · download below`;
        }
        if (id === os) card.classList.add('yours');
      }

      // Releases table.
      if (list && rows.length) {
        document.querySelector('[data-releases-empty]').hidden = true;
        list.hidden = false;
        const tbody = list.querySelector('tbody');
        rows.forEach((r, i) => {
          const files = el('td');
          for (const f of r.files) {
            const href = safeUrl(f.url);
            if (!href) continue;
            const a = el('a', { href, className: 'file' }, label(f));
            if (f.size) a.append(el('span', { className: 'dim' }, ` · ${size(f.size)}`));
            const item = el('div', {}, a);
            const counted = counts(stats, f);
            if (counted) item.append(el('div', { className: 'counts dim' }, counted));
            if (f.sha256) item.append(shaRow(String(f.sha256)));
            files.append(item);
          }
          const version = el('td', {}, el('b', {}, r.version));
          if (r.channel) version.append(el('span', { className: 'tag' }, r.channel));
          if (i === 0) version.append(el('span', { className: 'tag latest' }, 'latest'));
          tbody.append(el('tr', {}, version, el('td', {}, r.date || ''), el('td', {}, r.chromium || ''), files));
        });
      }

      // The button: this system's store page, or its newest build. Otherwise it goes to the full
      // list, the link under it says why, and the link itself is dropped as it would repeat the button.
      if (!primary || !meta) return;
      const all = meta.querySelector('a');
      const toAll = (why) => {
        primary.href = all.getAttribute('href');
        primary.textContent = all.textContent;
        if (why) meta.replaceChildren(why);
        else meta.hidden = true;
      };
      const latest = rows[0];
      const forMe = latest && latest.files.find((f) => f.platform === os && (!arch || !f.arch || f.arch === arch));
      const href = forMe && safeUrl(forMe.url);
      if (STORES[os]) {
        primary.href = STORES[os].url;
        primary.textContent = STORES[os].label;
      } else if (href) {
        primary.href = href;
        primary.textContent = `Download for ${PLATFORMS[os]}`;
        meta.replaceChildren(`${channel(latest)} ${latest.version} · ${label(forMe)}${forMe.size ? ` · ${size(forMe.size)}` : ''} · `, all);
      } else if (!latest) {
        toAll('The first build is on its way.');
      } else if (os === 'macos' && arch === 'x64') {
        toAll('This Mac has an Intel chip; Little Browser needs Apple silicon (M1 or later) for now.');
      } else if (os && os !== 'macos') {
        toAll(`Not available for ${PLATFORMS[os]} yet; the beta is macOS only.`);
      } else {
        toAll();
      }
    })();
  }

  // Brand page: click a swatch to copy its hex value.
  for (const sw of document.querySelectorAll('.swatch')) {
    sw.addEventListener('click', async () => {
      const v = sw.querySelector('code')?.textContent.trim();
      try { await navigator.clipboard.writeText(v); } catch { return; }
      sw.classList.add('copied');
      setTimeout(() => sw.classList.remove('copied'), 1200);
    });
  }
})();
