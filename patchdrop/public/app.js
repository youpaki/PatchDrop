// PatchDrop front: community presets explorer (full directory + homepage preview) and .patchlink publisher
const CATEGORY_ORDER = ['vocals', 'master', 'drums', 'synths', 'inst', 'fx'];
const CATEGORY_LABELS = {
  vocals: 'Vocals',
  master: 'Mix Bus & Master',
  drums: 'Drums & 808',
  synths: 'Synths & Pads',
  inst: 'Guitars & Keys',
  fx: 'FX & Creative',
};

async function api(path, opts) {
  const r = await fetch(path, opts);
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(j.error || ('HTTP ' + r.status));
  return j;
}

function buildPresetCard(p, { isPreview = false, activeTag = '', onTagSelect = null } = {}) {
  const el = document.createElement('div');
  el.className = 'card';

  const head = document.createElement('div');
  head.className = 'card-head';
  const code = document.createElement('div');
  code.className = 'code';
  code.textContent = p.code || '';
  const badge = document.createElement('span');
  badge.className = 'cat-badge';
  badge.textContent = p.categoryLabel || CATEGORY_LABELS[p.category] || 'FX & Creative';
  head.append(code, badge);

  const title = document.createElement('h3');
  title.textContent = p.title || 'Untitled preset';

  el.append(head, title);

  if (p.description) {
    const desc = document.createElement('p');
    desc.className = 'card-desc';
    desc.textContent = p.description;
    el.appendChild(desc);
  }

  const meta = document.createElement('div');
  meta.className = 'meta';
  const dateText = p.createdAt ? new Date(p.createdAt).toLocaleDateString() : '';
  meta.textContent = `by ${p.author || 'Anonymous'} · ${dateText} · ${p.downloads || 0} dl`;
  el.appendChild(meta);

  if (Array.isArray(p.tags) && p.tags.length) {
    const tagsEl = document.createElement('div');
    tagsEl.className = 'card-tags';
    for (const rawTag of p.tags.slice(0, 6)) {
      const slug = rawTag.replace(/^#+/, '').toLowerCase();
      if (isPreview) {
        const a = document.createElement('a');
        a.className = 'tag-pill';
        a.href = `presets.html?tag=${encodeURIComponent(slug)}`;
        a.textContent = rawTag;
        tagsEl.appendChild(a);
      } else {
        const b = document.createElement('button');
        b.type = 'button';
        b.className = 'tag-pill' + (activeTag === slug ? ' active' : '');
        b.textContent = rawTag;
        b.onclick = () => { if (onTagSelect) onTagSelect(slug); };
        tagsEl.appendChild(b);
      }
    }
    el.appendChild(tagsEl);
  }

  const pl = document.createElement('div');
  pl.className = 'plugins';
  const pluginList = (p.plugins || []).slice(0, 6);
  if (pluginList.length) {
    for (const n of pluginList) {
      const s = document.createElement('span');
      s.textContent = n;
      pl.appendChild(s);
    }
  } else {
    const s = document.createElement('span');
    s.textContent = '0 plugins';
    pl.appendChild(s);
  }
  el.appendChild(pl);

  const row = document.createElement('div');
  row.className = 'row';
  const copyBtn = document.createElement('button');
  copyBtn.type = 'button';
  copyBtn.textContent = 'Copy code';
  copyBtn.onclick = async () => {
    await navigator.clipboard.writeText(p.code);
    copyBtn.textContent = 'Copied ✓ paste in PatchLink';
    setTimeout(() => { copyBtn.textContent = 'Copy code'; }, 1600);
  };
  const dlBtn = document.createElement('button');
  dlBtn.type = 'button';
  dlBtn.textContent = 'Download';
  dlBtn.onclick = () => { window.location.href = `api/p/${p.code}/download`; };
  row.append(copyBtn, dlBtn);
  el.appendChild(row);

  return el;
}

// 1. Dedicated Community Presets Directory (presets.html)
let reloadActiveView = null;
const fullGrid = document.getElementById('grid');
if (fullGrid) {
  const params = new URLSearchParams(window.location.search);
  const state = {
    category: (params.get('category') || 'all').toLowerCase(),
    tag: (params.get('tag') || '').toLowerCase().replace(/^#+/, ''),
    q: (params.get('q') || '').trim(),
    sort: (params.get('sort') || 'newest').toLowerCase(),
  };

  const qInput = document.getElementById('q');
  const sortSelect = document.getElementById('sort');
  const qBtn = document.getElementById('qbtn');
  const clearBtn = document.getElementById('clear-filters');
  const catTabsEl = document.getElementById('cat-tabs');
  const tagBarEl = document.getElementById('tag-bar');
  const statusEl = document.getElementById('filter-status');

  if (qInput) qInput.value = state.q;
  if (sortSelect && (state.sort === 'popular' || state.sort === 'newest')) sortSelect.value = state.sort;

  const syncUrl = () => {
    const next = new URLSearchParams();
    if (state.category && state.category !== 'all') next.set('category', state.category);
    if (state.tag) next.set('tag', state.tag);
    if (state.q) next.set('q', state.q);
    if (state.sort && state.sort !== 'newest') next.set('sort', state.sort);
    const qs = next.toString();
    window.history.replaceState(null, '', window.location.pathname + (qs ? '?' + qs : '') + window.location.hash);
  };

  const renderCategoryTabs = (categories = {}, labels = CATEGORY_LABELS, allTotal = 0) => {
    catTabsEl.innerHTML = '';
    const mkTab = (key, label, count) => {
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'cat-tab' + (state.category === key ? ' active' : '');
      btn.append(document.createTextNode(label + ' '));
      const cnt = document.createElement('span');
      cnt.className = 'cnt';
      cnt.textContent = String(count || 0);
      btn.appendChild(cnt);
      btn.onclick = () => {
        state.category = key;
        syncUrl();
        loadDirectory();
      };
      catTabsEl.appendChild(btn);
    };
    mkTab('all', 'All', allTotal);
    for (const key of CATEGORY_ORDER) {
      mkTab(key, labels[key] || CATEGORY_LABELS[key] || key, categories[key] || 0);
    }
  };

  const renderTagBar = (popularTags = []) => {
    tagBarEl.innerHTML = '';
    const list = popularTags.slice();
    if (state.tag && !list.some((item) => item.tag.replace(/^#/, '') === state.tag)) {
      list.unshift({ tag: '#' + state.tag, count: 0 });
    }
    if (!list.length) return;
    const lbl = document.createElement('span');
    lbl.className = 'tag-label';
    lbl.textContent = 'HASHTAGS';
    tagBarEl.appendChild(lbl);
    for (const item of list) {
      const slug = item.tag.replace(/^#+/, '').toLowerCase();
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'tag-chip' + (state.tag === slug ? ' active' : '');
      btn.append(document.createTextNode(item.tag + ' '));
      if (item.count > 0) {
        const sp = document.createElement('span');
        sp.textContent = String(item.count);
        btn.appendChild(sp);
      }
      btn.onclick = () => {
        state.tag = (state.tag === slug) ? '' : slug;
        syncUrl();
        loadDirectory();
      };
      tagBarEl.appendChild(btn);
    }
  };

  const loadDirectory = async () => {
    fullGrid.innerHTML = '<div class="empty">Loading…</div>';
    const qs = new URLSearchParams();
    if (state.q) qs.set('q', state.q);
    if (state.category && state.category !== 'all') qs.set('category', state.category);
    if (state.tag) qs.set('tag', state.tag);
    if (state.sort) qs.set('sort', state.sort);

    const hasFilters = state.category !== 'all' || Boolean(state.tag) || Boolean(state.q);
    if (clearBtn) clearBtn.hidden = !hasFilters;

    try {
      const j = await api('api/list?' + qs.toString());
      const labels = j.categoryLabels || CATEGORY_LABELS;
      renderCategoryTabs(j.categories, labels, j.allTotal ?? j.total ?? 0);
      renderTagBar(j.popularTags || []);

      const activeParts = [];
      if (state.category !== 'all') activeParts.push(`Category: ${labels[state.category] || state.category}`);
      if (state.tag) activeParts.push(`#${state.tag}`);
      if (state.q) activeParts.push(`Query: "${state.q}"`);
      statusEl.textContent = `Showing ${j.presets.length} of ${j.allTotal ?? j.total} presets`
        + (activeParts.length ? ' · ' + activeParts.join(' · ') : '');

      if (!j.presets.length) {
        fullGrid.innerHTML = '<div class="empty">No presets match the active filters.</div>';
        return;
      }
      fullGrid.innerHTML = '';
      for (const p of j.presets) {
        fullGrid.appendChild(buildPresetCard(p, {
          isPreview: false,
          activeTag: state.tag,
          onTagSelect: (slug) => {
            state.tag = (state.tag === slug) ? '' : slug;
            syncUrl();
            loadDirectory();
          },
        }));
      }
    } catch (e) {
      statusEl.textContent = 'API error';
      fullGrid.innerHTML = `<div class="empty">API unreachable: ${e.message}</div>`;
    }
  };

  if (qBtn) qBtn.onclick = () => { state.q = qInput.value.trim(); syncUrl(); loadDirectory(); };
  if (qInput) qInput.addEventListener('keydown', (e) => { if (e.key === 'Enter') { state.q = qInput.value.trim(); syncUrl(); loadDirectory(); } });
  if (sortSelect) sortSelect.onchange = () => { state.sort = sortSelect.value; syncUrl(); loadDirectory(); };
  if (clearBtn) clearBtn.onclick = () => {
    state.category = 'all';
    state.tag = '';
    state.q = '';
    if (qInput) qInput.value = '';
    syncUrl();
    loadDirectory();
  };

  reloadActiveView = loadDirectory;
  loadDirectory();
}

// 2. Main Site Community Preview (index.html)
const previewGrid = document.getElementById('preview-grid');
if (previewGrid) {
  const previewCatsEl = document.getElementById('preview-cats');
  const previewTagsEl = document.getElementById('preview-tags');
  const previewCountEl = document.getElementById('preview-count');

  const loadPreview = async () => {
    previewGrid.innerHTML = '<div class="empty">Loading preview…</div>';
    try {
      const j = await api('api/list?limit=3');
      const labels = j.categoryLabels || CATEGORY_LABELS;

      if (previewCatsEl) {
        previewCatsEl.innerHTML = '';
        const allLink = document.createElement('a');
        allLink.className = 'cat-tab active';
        allLink.href = 'presets.html';
        allLink.append(document.createTextNode('All '));
        const allCnt = document.createElement('span');
        allCnt.className = 'cnt';
        allCnt.textContent = String(j.allTotal ?? j.total ?? 0);
        allLink.appendChild(allCnt);
        previewCatsEl.appendChild(allLink);

        for (const key of CATEGORY_ORDER) {
          const a = document.createElement('a');
          a.className = 'cat-tab';
          a.href = `presets.html?category=${encodeURIComponent(key)}`;
          a.append(document.createTextNode((labels[key] || CATEGORY_LABELS[key] || key) + ' '));
          const cnt = document.createElement('span');
          cnt.className = 'cnt';
          cnt.textContent = String((j.categories && j.categories[key]) || 0);
          a.appendChild(cnt);
          previewCatsEl.appendChild(a);
        }
      }

      if (previewTagsEl) {
        previewTagsEl.innerHTML = '';
        const tags = (j.popularTags || []).slice(0, 10);
        if (tags.length) {
          const lbl = document.createElement('span');
          lbl.className = 'tag-label';
          lbl.textContent = 'HASHTAGS';
          previewTagsEl.appendChild(lbl);
          for (const item of tags) {
            const slug = item.tag.replace(/^#+/, '').toLowerCase();
            const a = document.createElement('a');
            a.className = 'tag-chip';
            a.href = `presets.html?tag=${encodeURIComponent(slug)}`;
            a.append(document.createTextNode(item.tag + ' '));
            const sp = document.createElement('span');
            sp.textContent = String(item.count);
            a.appendChild(sp);
            previewTagsEl.appendChild(a);
          }
        }
      }

      if (previewCountEl) {
        const total = j.allTotal ?? j.total ?? j.presets.length;
        previewCountEl.textContent = `Showing latest ${j.presets.length} of ${total} public presets`;
      }

      if (!j.presets.length) {
        previewGrid.innerHTML = '<div class="empty">No presets found.</div>';
        return;
      }
      previewGrid.innerHTML = '';
      for (const p of j.presets) {
        previewGrid.appendChild(buildPresetCard(p, { isPreview: true }));
      }
    } catch (e) {
      previewGrid.innerHTML = `<div class="empty">API unreachable: ${e.message}</div>`;
    }
  };

  reloadActiveView = loadPreview;
  loadPreview();
}

// 3. Publish: read the .patchlink locally, POST api/publish
const pubForm = document.getElementById('pubform');
if (pubForm) {
  pubForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    const box = document.getElementById('pubres');
    const f = document.getElementById('f-file').files[0];
    if (!f) { box.textContent = 'Pick a .patchlink file.'; return; }
    box.textContent = 'Uploading…';
    try {
      const patch = JSON.parse(await f.text());
      const catEl = document.getElementById('f-category');
      const tagsEl = document.getElementById('f-tags');
      const body = {
        title: document.getElementById('f-title').value,
        author: document.getElementById('f-author').value || 'Anonymous',
        description: document.getElementById('f-desc').value,
        category: catEl ? catEl.value : '',
        tags: tagsEl ? tagsEl.value : '',
        isPublic: document.getElementById('f-public').checked,
        patch,
      };
      const j = await api('api/publish', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      });
      const tagsTxt = (j.tags || []).join(' ');
      box.innerHTML = `<div class="ok">Code ready: <b>${j.code}</b><br>Category: ${(CATEGORY_LABELS[j.category] || j.category || 'FX')}${tagsTxt ? ' · ' + tagsTxt : ''}<br>Paste this code in PatchLink → <i>Open code</i>.</div>`;
      if (reloadActiveView) reloadActiveView();
    } catch (err) {
      box.textContent = 'Error: ' + err.message;
    }
  });
}

// 4. Download links: if a binary isn't uploaded yet, show a clean message
document.querySelectorAll('[data-dl]').forEach((a) => {
  a.addEventListener('click', async (ev) => {
    try {
      const r = await fetch(a.getAttribute('href'), { method: 'HEAD' });
      if (!r.ok) {
        ev.preventDefault();
        const note = document.getElementById('dl-note');
        if (note) {
          note.innerHTML = 'That file is not published yet. Source + build instructions: <a href="https://github.com/youpaki/PatchDrop">youpaki/PatchDrop</a>.';
        }
      }
    } catch { /* let the browser handle it */ }
  });
});
