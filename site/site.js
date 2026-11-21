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

    // Grow the card from the link in the article, wherever the text wrapped it.
    const page = mock.querySelector('.m-page');
    const link = mock.querySelector('[data-peek-link]');
    const card = mock.querySelector('.m-card');
    const aim = () => {
      if (!page || !link || !card) return;
      const p = page.getBoundingClientRect();
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
  //   { "releases": [ { "version": "157.0.8103.78-1", "date": "2026-11-20", "channel": "alpha",
  //       "chromium": "157.0.8103.78", "files": [ { "platform": "macos", "arch": "arm64",
  //       "url": "https://downloads.littlebrowser.app/...dmg", "size": 123, "sha256": "..." } ] } ] }
  // Newest first. Nothing here is sent anywhere: the system is guessed locally from the browser.
  const primary = document.querySelector('[data-dl-primary]');
  const meta = document.querySelector('[data-dl-meta]');
  const list = document.querySelector('[data-releases-list]');
  if (primary || list) {
    const RELEASES = 'https://downloads.littlebrowser.app/releases.json';
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

    const load = async () => {
      let src = RELEASES;
      const override = new URLSearchParams(location.search).get('releases');
      if (local && override) src = override; // preview against a sample file
      try {
        const r = await fetch(src, { cache: 'no-cache' });
        if (!r.ok) return [];
        const j = await r.json();
        return Array.isArray(j.releases) ? j.releases : [];
      } catch { return []; }
    };

    (async () => {
      const [{ os, arch }, releases] = await Promise.all([detect(), load()]);
      const rows = releases.filter((r) => r && r.version && Array.isArray(r.files) && r.files.length);

      // Platform cards: which ones have a build or a store page, and which one is yours.
      for (const card of document.querySelectorAll('[data-platform]')) {
        const id = card.dataset.platform;
        const status = card.querySelector('[data-platform-status]');
        if (STORES[id]) status.replaceChildren(el('a', { href: STORES[id].url }, STORES[id].label));
        else if (rows.some((r) => r.files.some((f) => f.platform === id))) status.textContent = 'Alpha · download below';
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
            if (f.sha256) item.append(el('code', { className: 'sha', title: `SHA-256 ${f.sha256}` }, `SHA-256 ${String(f.sha256).slice(0, 12)}…`));
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
        meta.replaceChildren(`Alpha ${latest.version} · ${label(forMe)}${forMe.size ? ` · ${size(forMe.size)}` : ''} · `, all);
      } else if (!latest) {
        toAll('The first alpha build is on its way.');
      } else if (os === 'macos' && arch === 'x64') {
        toAll('This Mac has an Intel chip; the alpha needs Apple silicon (M1 or later).');
      } else if (os && os !== 'macos') {
        toAll(`Not available for ${PLATFORMS[os]} yet; the alpha is macOS only.`);
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
