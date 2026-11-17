// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// littlebrowser.app: theme switch, product-mock demo, colour-swatch copy. No tracking, no cookies.

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
      mock.querySelector('.m-ws-name').textContent = b.title;
      for (const s of spaces) s.setAttribute('aria-pressed', String(s === b));
    };
    spaces.forEach((b) => b.addEventListener('click', () => setSpace(b)));

    const peek = () => {
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
