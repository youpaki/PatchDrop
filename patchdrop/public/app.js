// PatchDrop front: community presets explorer, browser .fst drag-in/out, and .patchlink/.fst publisher
const CATEGORY_ORDER = ['vocals', 'master', 'drums', 'synths', 'inst', 'fx'];
const CATEGORY_LABELS = {
  vocals: 'Vocals',
  master: 'Mix Bus & Master',
  drums: 'Drums & 808',
  synths: 'Synths & Pads',
  inst: 'Guitars & Keys',
  fx: 'FX & Creative',
};

const JUCE_B64 = '.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+';

function juceEncodeBlock(bytes) {
  const size = bytes.length;
  const numChars = ((size << 3) + 5) / 6 | 0;
  let out = size + '.';
  for (let i = 0; i < numChars; i++) {
    let bitStart = i * 6;
    let numBits = 6;
    let res = 0;
    let byte = bitStart >> 3;
    let off = bitStart & 7;
    let bitsSoFar = 0;
    while (numBits > 0 && byte < size) {
      const take = Math.min(numBits, 8 - off);
      const mask = (0xff >> (8 - take)) << off;
      res |= (((bytes[byte] & mask) >> off) << bitsSoFar);
      bitsSoFar += take;
      numBits -= take;
      byte++;
      off = 0;
    }
    out += JUCE_B64[res];
  }
  return out;
}

function bytesToBase64(bytes) {
  let bin = '';
  const step = 0x8000;
  for (let i = 0; i < bytes.length; i += step) {
    bin += String.fromCharCode.apply(null, bytes.subarray(i, i + step));
  }
  return btoa(bin);
}

function bytesToHex(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i++) s += bytes[i].toString(16).padStart(2, '0');
  return s;
}

function computeJuceUniqueIdFromTuid(raw16) {
  let allZero = true;
  for (let i = 0; i < 16; i++) if (raw16[i] !== 0) { allZero = false; break; }
  if (allZero) return 0;
  const dv = new DataView(raw16.buffer, raw16.byteOffset, 16);
  const d1 = dv.getUint32(0, true);
  const d2 = dv.getUint16(4, true);
  const d3 = dv.getUint16(6, true);
  const l1 = d1 >>> 0;
  const l2 = (((d2 << 16) | d3) >>> 0);
  const l3 = dv.getUint32(8, false);
  const l4 = dv.getUint32(12, false);
  let h = 0n;
  for (const item of [l1, l2, l3, l4]) {
    h = ((h * 31n) + BigInt(item)) & 0xffffffffn;
  }
  const n = Number(h);
  return n >= 0x80000000 ? n - 0x100000000 : n;
}

function decodeFlText(bytes) {
  if (!bytes || !bytes.length) return '';
  if (bytes.length >= 2 && (bytes.length % 2) === 0) {
    let zeros = 0;
    const units = bytes.length >> 1;
    for (let i = 0; i < units; i++) if (bytes[i * 2 + 1] === 0) zeros++;
    if (zeros * 2 >= units) {
      return new TextDecoder('utf-16le').decode(bytes).replace(/\0+$/g, '').trim();
    }
  }
  return new TextDecoder('utf-8').decode(bytes).replace(/\0+$/g, '').trim();
}

function extractVst3StateBase64(subData) {
  if (!subData || subData.length < 4) return '';
  const dv = new DataView(subData.buffer, subData.byteOffset, subData.byteLength);
  if (dv.getUint32(0, true) !== 1) return '';
  let comp = new Uint8Array(0);
  let cont = new Uint8Array(0);
  let pos = 4;
  while (pos + 12 <= subData.length) {
    const cid = dv.getUint32(pos, true);
    const clen = Number(dv.getBigInt64(pos + 4, true));
    if (clen < 0 || pos + 12 + clen > subData.length) break;
    const cdata = subData.subarray(pos + 12, pos + 12 + clen);
    pos += 12 + clen;
    if (cid === 3 && clen > 0) comp = cdata;
    else if (cid === 2 && clen > 0) cont = cdata;
  }
  if (!comp.length && !cont.length) return '';
  let xml = '<VST3PluginState>';
  if (comp.length) xml += `<IComponent>${juceEncodeBlock(comp)}</IComponent>`;
  if (cont.length) xml += `<IEditController>${juceEncodeBlock(cont)}</IEditController>`;
  xml += '</VST3PluginState>';
  const xmlBytes = new TextEncoder().encode(xml);
  const block = new Uint8Array(9 + xmlBytes.length);
  const bdv = new DataView(block.buffer);
  bdv.setUint32(0, 0x21324356, true); // JUCE magicXmlNumber
  bdv.setUint32(4, xmlBytes.length, true);
  block.set(xmlBytes, 8);
  block[8 + xmlBytes.length] = 0;
  return bytesToBase64(block);
}

function parseFstBufferInBrowser(arrayBuffer, fileName = 'FL Chain') {
  const bytes = new Uint8Array(arrayBuffer);
  if (bytes.length < 16) throw new Error('File too small to be an FL Studio .fst preset.');
  const ascii4 = (off) => String.fromCharCode(bytes[off], bytes[off + 1], bytes[off + 2], bytes[off + 3]);
  if (ascii4(0) !== 'FLhd') throw new Error('Invalid .fst file (missing FLhd header).');
  const dv = new DataView(arrayBuffer);
  const hdLen = dv.getUint32(4, true);
  let pos = 8 + hdLen;
  if (pos + 8 > bytes.length || ascii4(pos) !== 'FLdt') throw new Error('Invalid .fst structure (missing FLdt chunk).');
  const dtLen = dv.getUint32(pos + 4, true);
  pos += 8;
  const end = Math.min(bytes.length, pos + dtLen);

  let trackName = '';
  let curPlugin = '';
  let curSlotName = '';
  let hasD4 = false, d4_2 = 0, d4_4 = 0;
  const plugins = [];
  const skipped = [];
  const utf8 = new TextDecoder('utf-8');

  const resetSlot = () => { curPlugin = ''; curSlotName = ''; hasD4 = false; d4_2 = 0; d4_4 = 0; };

  while (pos < end) {
    const ev = bytes[pos++];
    if (ev < 64) { if (pos + 1 > end) break; pos += 1; }
    else if (ev < 128) {
      if (pos + 2 > end) break;
      pos += 2;
      if (ev === 98) resetSlot();
    } else if (ev < 192) { if (pos + 4 > end) break; pos += 4; }
    else {
      let len = 0, shift = 0, ok = true;
      while (pos < end) {
        const b = bytes[pos++];
        len |= (b & 0x7f) << shift;
        if ((b & 0x80) === 0) break;
        shift += 7;
        if (shift > 28) { ok = false; break; }
      }
      if (!ok || pos + len > end) break;
      const payload = bytes.subarray(pos, pos + len);
      pos += len;

      if (ev === 204) {
        const t = decodeFlText(payload);
        if (t && !trackName) trackName = t;
      } else if (ev === 201) {
        curPlugin = decodeFlText(payload);
      } else if (ev === 203) {
        curSlotName = decodeFlText(payload);
      } else if (ev === 212 && len === 52) {
        const pdv = new DataView(payload.buffer, payload.byteOffset, 52);
        hasD4 = true;
        d4_2 = pdv.getUint32(8, true);
        d4_4 = pdv.getUint32(16, true);
      } else if (ev === 213) {
        const pdv = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
        const wver = len >= 4 ? pdv.getUint32(0, true) : 0;
        const isWrapper = !curPlugin || curPlugin.toLowerCase() === 'fruity wrapper';
        if (isWrapper && wver >= 4 && wver <= 32) {
          const entry = {
            slotName: curSlotName,
            pluginName: '',
            vendor: '',
            filePath: '',
            tuidHex: '',
            uniqueId: 0,
            bypass: hasD4 && d4_4 !== 0 && d4_2 === 0,
            isVst2: false,
            stateBase64: '',
          };
          let spos = 4;
          while (spos + 12 <= len) {
            const sid = pdv.getUint32(spos, true);
            const slen = Number(pdv.getBigInt64(spos + 4, true));
            if (slen < 0 || spos + 12 + slen > len) break;
            const sdata = payload.subarray(spos + 12, spos + 12 + slen);
            spos += 12 + slen;
            if (sid === 52 && slen >= 16) {
              const tuid = sdata.subarray(0, 16);
              entry.tuidHex = bytesToHex(tuid);
              entry.uniqueId = computeJuceUniqueIdFromTuid(tuid);
            } else if (sid === 54) entry.pluginName = utf8.decode(sdata).replace(/\0+$/g, '').trim();
            else if (sid === 55) entry.filePath = utf8.decode(sdata).replace(/\0+$/g, '').trim();
            else if (sid === 56) entry.vendor = utf8.decode(sdata).replace(/\0+$/g, '').trim();
            else if (sid === 53 && slen >= 4) {
              const sdv = new DataView(sdata.buffer, sdata.byteOffset, sdata.byteLength);
              if (sdv.getUint32(0, true) === 1) entry.stateBase64 = extractVst3StateBase64(sdata);
              else entry.isVst2 = true;
            }
          }
          if (entry.filePath.toLowerCase().endsWith('.dll')) entry.isVst2 = true;
          if (entry.pluginName || entry.slotName || entry.filePath) plugins.push(entry);
        } else if (curPlugin && curPlugin.toLowerCase() !== 'fruity wrapper') {
          skipped.push(curPlugin + ' (FL native)');
        }
      }
    }
  }

  const baseTitle = trackName || fileName.replace(/\.[^/.]+$/, '');
  const nodes = plugins.map((e, idx) => {
    const displayName = e.slotName || e.pluginName || `Plugin ${idx + 1}`;
    const uidHex = (e.uniqueId >>> 0).toString(16).padStart(8, '0');
    const ids = {
      name: displayName,
      manufacturer: e.vendor,
      version: '',
      format: e.isVst2 ? 'VST' : 'VST3',
      fileOrId: e.filePath,
      identifier: (!e.isVst2 && e.uniqueId !== 0) ? `VST3-${displayName}-0-${uidHex}` : '',
    };
    if (e.uniqueId !== 0) ids.uniqueId = e.uniqueId;
    if (e.tuidHex) ids.tuidHex = e.tuidHex;
    if (e.pluginName) ids.flPluginName = e.pluginName;
    return {
      uid: 100 + idx,
      x: 280 + (idx % 4) * 230,
      y: 200 + Math.floor(idx / 4) * 140,
      order: idx,
      bypass: e.bypass,
      wet: 1.0,
      gainDb: 0.0,
      ids,
      stateBase64: e.isVst2 ? '' : e.stateBase64,
    };
  });

  if (!nodes.length) {
    throw new Error(skipped.length
      ? `No VST3 plugins found (only FL native: ${skipped.join(', ')})`
      : 'No VST3 plugins found inside this .fst file.');
  }

  return {
    patch: { app: 'PatchLink', chainFormat: 2, name: baseTitle || 'FL Chain', nodes, connections: [] },
    skipped,
  };
}

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
    copyBtn.textContent = 'Copied ✓';
    setTimeout(() => { copyBtn.textContent = 'Copy code'; }, 1600);
  };
  const dlBtn = document.createElement('button');
  dlBtn.type = 'button';
  dlBtn.textContent = '.patchlink';
  dlBtn.onclick = () => { window.location.href = `api/p/${p.code}/download`; };

  const fstLink = document.createElement('a');
  fstLink.className = 'btn-fst';
  fstLink.draggable = true;
  fstLink.href = `api/p/${p.code}/fst`;
  fstLink.setAttribute('download', `patchdrop-${p.code}.fst`);
  fstLink.title = 'Drag directly into FL Studio Mixer (Chrome/Edge) or click to download .fst';
  fstLink.textContent = 'FL .fst';
  fstLink.addEventListener('dragstart', (ev) => {
    const absUrl = new URL(`api/p/${p.code}/fst`, window.location.href).href;
    if (ev.dataTransfer) {
      ev.dataTransfer.effectAllowed = 'copy';
      ev.dataTransfer.setData('DownloadURL', `application/octet-stream:patchdrop-${p.code}.fst:${absUrl}`);
      ev.dataTransfer.setData('text/uri-list', absUrl);
      ev.dataTransfer.setData('text/plain', absUrl);
    }
  });

  row.append(copyBtn, dlBtn, fstLink);
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

// 3. Publish & Browser .fst / .patchlink Drag & Drop
const pubForm = document.getElementById('pubform');
if (pubForm) {
  let stagedFilePatch = null;
  const fileInput = document.getElementById('f-file');
  const titleInput = document.getElementById('f-title');
  const dropBox = document.getElementById('drop-box');
  const dropSummary = document.getElementById('drop-summary');

  const veil = document.createElement('div');
  veil.className = 'page-drop-veil';
  veil.textContent = 'Drop FL Studio .fst or .patchlink file to publish a code';
  document.body.appendChild(veil);

  const loadLocalPresetFile = async (f) => {
    if (!f) return;
    if (dropSummary) dropSummary.textContent = 'Parsing ' + f.name + '…';
    try {
      let patch = null;
      let note = '';
      if (f.name.toLowerCase().endsWith('.fst')) {
        const res = parseFstBufferInBrowser(await f.arrayBuffer(), f.name);
        patch = res.patch;
        if (res.skipped.length) note = ` (skipped FL native: ${res.skipped.join(', ')})`;
      } else {
        patch = JSON.parse(await f.text());
        if (!patch || !Array.isArray(patch.nodes)) throw new Error('Invalid .patchlink JSON.');
      }
      stagedFilePatch = patch;
      if (fileInput) fileInput.required = false;
      if (titleInput && !titleInput.value.trim()) {
        titleInput.value = (patch.name && patch.name !== 'Untitled chain') ? patch.name : f.name.replace(/\.[^/.]+$/, '');
      }
      const names = (patch.nodes || []).map((n) => (n.ids && n.ids.name) || n.name || '?').filter(Boolean);
      if (dropSummary) {
        dropSummary.textContent = `Loaded ${f.name}: ${names.length} plugin(s) [${names.slice(0, 6).join(', ')}]${note}`;
      }
    } catch (err) {
      stagedFilePatch = null;
      if (dropSummary) dropSummary.textContent = 'Error reading ' + f.name + ': ' + err.message;
    }
  };

  if (fileInput) {
    fileInput.addEventListener('change', () => {
      if (fileInput.files && fileInput.files[0]) loadLocalPresetFile(fileInput.files[0]);
    });
  }

  let dragDepth = 0;
  window.addEventListener('dragenter', (e) => {
    if (e.dataTransfer && Array.from(e.dataTransfer.types || []).includes('Files')) {
      dragDepth++;
      veil.classList.add('active');
      if (dropBox) dropBox.classList.add('dragover');
    }
  });
  window.addEventListener('dragover', (e) => {
    if (e.dataTransfer && Array.from(e.dataTransfer.types || []).includes('Files')) {
      e.preventDefault();
      e.dataTransfer.dropEffect = 'copy';
    }
  });
  window.addEventListener('dragleave', () => {
    dragDepth = Math.max(0, dragDepth - 1);
    if (dragDepth === 0) {
      veil.classList.remove('active');
      if (dropBox) dropBox.classList.remove('dragover');
    }
  });
  window.addEventListener('drop', (e) => {
    dragDepth = 0;
    veil.classList.remove('active');
    if (dropBox) dropBox.classList.remove('dragover');
    const files = e.dataTransfer && e.dataTransfer.files;
    if (files && files.length) {
      const f = files[0];
      if (/\.(fst|patchlink|json)$/i.test(f.name)) {
        e.preventDefault();
        loadLocalPresetFile(f);
        pubForm.scrollIntoView({ behavior: 'smooth', block: 'center' });
      }
    }
  });

  pubForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    const box = document.getElementById('pubres');
    const f = fileInput && fileInput.files[0];
    if (!stagedFilePatch && !f) { box.textContent = 'Drop or pick a .fst / .patchlink file.'; return; }
    box.textContent = 'Uploading…';
    try {
      let patch = stagedFilePatch;
      if (!patch && f) {
        if (f.name.toLowerCase().endsWith('.fst')) patch = parseFstBufferInBrowser(await f.arrayBuffer(), f.name).patch;
        else patch = JSON.parse(await f.text());
      }
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
      box.innerHTML = `<div class="ok">Code ready: <b>${j.code}</b><br>Category: ${(CATEGORY_LABELS[j.category] || j.category || 'FX')}${tagsTxt ? ' · ' + tagsTxt : ''}<br>Paste this code in PatchLink → <i>Open code</i>, or drag <b>FL .fst</b> directly from its card below.</div>`;
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
