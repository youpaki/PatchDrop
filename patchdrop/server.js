// PatchDrop - zero-dependency Node server (Node >= 18)
// Port 3250 localhost, proxied via Apache at /patchdrop/ -> 127.0.0.1:3250/
// Data: ./data/patches/<CODE>.json + ./data/db.json (index)
// API:
//   POST /api/publish {title,author,description,patch,isPublic,category,tags} -> {code}
//   GET  /api/p/:code -> {title,author,description,patch,createdAt,downloads}
//   GET  /api/p/:code/download -> fichier .patchlink
//   GET  /api/list?q=&category=&tag=&sort=&limit= -> {presets,total,allTotal,categories,categoryLabels,popularTags}
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const PORT = Number(process.env.PORT) || 3250;
const HOST = process.env.HOST || '127.0.0.1';
const ROOT = __dirname;
const PUBLIC = path.join(ROOT, 'public');
const DATA = path.join(ROOT, 'data');
const PATCHES = path.join(DATA, 'patches');
const DB_FILE = path.join(DATA, 'db.json');
// Quand Apache proxy /patchdrop/ -> /, le serveur doit générer des URLs relatives.
// BASE_PATH sert uniquement au site (liens absolus). Laisse '' en local.
const BASE_PATH = (process.env.BASE_PATH || '').replace(/\/+$/, '');

fs.mkdirSync(PATCHES, { recursive: true });
function loadDb() {
  try { return JSON.parse(fs.readFileSync(DB_FILE, 'utf8')); }
  catch { return { patches: {} }; }
}
const CATEGORIES = {
  vocals: 'Vocals',
  master: 'Mix Bus & Master',
  drums: 'Drums & 808',
  synths: 'Synths & Pads',
  inst: 'Guitars & Keys',
  fx: 'FX & Creative',
};

function sanitizeTag(raw) {
  const clean = String(raw || '').trim().toLowerCase().replace(/^#+/, '').replace(/[^a-z0-9_-]/g, '');
  return clean.length >= 2 && clean.length <= 24 ? '#' + clean : '';
}

function parseExplicitTags(rawTags, title, description) {
  const out = [];
  const push = (t) => {
    const s = sanitizeTag(t);
    if (s && !out.includes(s)) out.push(s);
  };
  if (Array.isArray(rawTags)) {
    for (const item of rawTags) push(item);
  } else if (typeof rawTags === 'string' && rawTags.trim()) {
    for (const token of rawTags.split(/[\s,;]+/)) push(token);
  }
  const text = `${title || ''} ${description || ''}`;
  for (const m of text.matchAll(/#([a-zA-Z0-9_-]{2,24})/g)) push(m[1]);
  return out;
}

function inferCategoryAndTags({ title, description, plugins, category, tags }) {
  const hay = [title || '', description || '', ...(plugins || [])].join(' ').toLowerCase();
  const explicitCat = typeof category === 'string' ? category.trim().toLowerCase() : '';
  let resolvedCat = CATEGORIES[explicitCat] ? explicitCat : '';

  if (!resolvedCat) {
    if (/\b(drum|drums|808|kick|snare|perc|hihat|hat|transient)\b/.test(hay)) resolvedCat = 'drums';
    else if (/\b(nappe|pad|synth|atmos|ambient|supermassive|shimmer)\b/.test(hay)) resolvedCat = 'synths';
    else if (/\b(master|mastering|mix\s*bus|bus|glue|limiter|kotelnikov)\b/.test(hay)) resolvedCat = 'master';
    else if (/\b(voix|vocal|vocals|couplet|adlib|auto-?tune|de-?esser|pro-ds)\b/.test(hay)) resolvedCat = 'vocals';
    else if (/\b(guitar|gtr|amp|piano|keys|rhodes|bass)\b/.test(hay)) resolvedCat = 'inst';
    else resolvedCat = 'fx';
  }

  const resolvedTags = parseExplicitTags(tags, title, description);
  if (resolvedTags.length < 2) {
    const rules = [
      [/\b(voix|vocal|vocals|couplet|adlib)\b/, '#vocals'],
      [/\b(rap|couplet|adlib|trap|drill)\b/, '#rap'],
      [/\b(auto-?tune|pitch)\b/, '#autotune'],
      [/\b(de-?esser|pro-ds)\b/, '#deesser'],
      [/\b(warm|warmth|chaleur|tube|la-2a|saturn|drive)\b/, '#warmth'],
      [/\b(comp|compressor|rcompressor|kotelnikov|la-2a|1176|pro-c)\b/, '#compression'],
      [/\b(eq|pro-q|nova|q8)\b/, '#eq'],
      [/\b(reverb|pro-r|vintageverb|supermassive|espace|nappe|space)\b/, '#space'],
      [/\b(nappe|pad|synth)\b/, '#pads'],
      [/\b(mix|master|bus)\b/, '#mix'],
      [/\b(fabfilter|pro-q|pro-ds|pro-c|pro-r|saturn|pro-mb)\b/, '#fabfilter'],
      [/\b(uad|uadx|teletronix|la-2a)\b/, '#uad'],
      [/\b(waves|waveshell|rcompressor|q8|deesser)\b/, '#waves'],
    ];
    for (const [re, tag] of rules) {
      if (resolvedTags.length >= 6) break;
      if (re.test(hay) && !resolvedTags.includes(tag)) resolvedTags.push(tag);
    }
    const catTag = '#' + resolvedCat;
    if (resolvedTags.length === 0 || (resolvedTags.length < 2 && !resolvedTags.includes(catTag))) {
      if (!resolvedTags.includes(catTag)) resolvedTags.unshift(catTag);
    }
  }

  return { category: resolvedCat, tags: resolvedTags.slice(0, 6) };
}

let db = loadDb();
function saveDb() {
  const tmp = DB_FILE + '.tmp';
  fs.writeFileSync(tmp, JSON.stringify(db, null, 2));
  fs.renameSync(tmp, DB_FILE);
}
(() => {
  let changed = false;
  for (const [code, p] of Object.entries(db.patches || {})) {
    const needPlugins = !Array.isArray(p.plugins) || p.plugins.includes('?');
    const needMeta = !p.category || !CATEGORIES[p.category] || !Array.isArray(p.tags) || p.tags.length === 0;
    if (!needPlugins && !needMeta) continue;
    try {
      const doc = JSON.parse(fs.readFileSync(path.join(PATCHES, code + '.json'), 'utf8'));
      if (needPlugins) {
        const next = pluginNames(doc.patch);
        if (next.length && JSON.stringify(next) !== JSON.stringify(p.plugins)) {
          p.plugins = next;
          changed = true;
        }
      }
      if (needMeta) {
        const meta = inferCategoryAndTags({
          title: p.title,
          description: p.description,
          plugins: p.plugins || [],
          category: p.category || doc.category,
          tags: p.tags || doc.tags,
        });
        p.category = meta.category;
        p.tags = meta.tags;
        changed = true;
      }
    } catch {}
  }
  if (changed) saveDb();
})();

function send(res, status, body, headers = {}) {
  const isJson = typeof body !== 'string' && !Buffer.isBuffer(body);
  const payload = isJson ? JSON.stringify(body) : body;
  res.writeHead(status, {
    'Content-Type': isJson ? 'application/json; charset=utf-8' : headers['Content-Type'] || 'text/plain; charset=utf-8',
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff',
    ...headers,
  });
  res.end(payload);
}
function readBody(req, limit = 12 * 1024 * 1024) {
  return new Promise((resolve, reject) => {
    let size = 0; const chunks = [];
    req.on('data', (c) => { size += c.length; if (size > limit) { req.destroy(); reject(Object.assign(new Error('Patch trop lourd (max 12 Mo)'), { status: 413 })); } else chunks.push(c); });
    req.on('end', () => { if (!chunks.length) return resolve({}); try { resolve(JSON.parse(Buffer.concat(chunks).toString('utf8'))); } catch { reject(Object.assign(new Error('JSON invalide'), { status: 400 })); } });
    req.on('error', reject);
  });
}
const CODE_RE = /^[A-Z2-9]{4,8}$/;
function newCode() {
  const alpha = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';
  for (let i = 0; i < 50; i++) {
    let s = '';
    for (let j = 0; j < 6; j++) s += alpha[crypto.randomInt(alpha.length)];
    if (!db.patches[s]) return s;
  }
  return Date.now().toString(36).toUpperCase().slice(-6);
}
function pluginNames(patch) {
  try {
    const nodes = patch.nodes || [];
    return nodes.map((n) => (n && n.ids && (n.ids.name || n.ids.fileOrId)) || (n && (n.name || n.fileOrId)) || '?').filter(Boolean).slice(0, 12);
  } catch { return []; }
}

const MIME = { '.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8', '.js': 'application/javascript; charset=utf-8', '.svg': 'image/svg+xml', '.png': 'image/png', '.json': 'application/json; charset=utf-8' };
function serveStatic(req, res, pathname) {
  let rel = decodeURIComponent(pathname);
  if (rel === '/' || rel === '') rel = '/index.html';
  const file = path.normalize(path.join(PUBLIC, rel));
  if (!file.startsWith(PUBLIC + path.sep)) return send(res, 403, 'Forbidden');
  fs.stat(file, (err, st) => {
    if (err || !st.isFile()) return send(res, 404, 'Not found');
    res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] || 'application/octet-stream', 'Cache-Control': 'public, max-age=300', 'X-Content-Type-Options': 'nosniff' });
    fs.createReadStream(file).pipe(res);
  });
}

async function handleApi(req, res, pathname) {
  const method = req.method;

  if (pathname === '/api/health' && method === 'GET')
    return send(res, 200, { ok: true, service: 'patchdrop', base: BASE_PATH || '/' });

  if (pathname === '/api/publish' && method === 'POST') {
    const b = await readBody(req);
    const patch = b.patch;
    if (!patch || typeof patch !== 'object' || !Array.isArray(patch.nodes))
      return send(res, 400, { error: 'Champ "patch" invalide (JSON PatchLink attendu).' });
    const title = String(b.title || 'Sans titre').slice(0, 80);
    const author = String(b.author || 'Anonyme').slice(0, 40);
    const description = String(b.description || '').slice(0, 500);
    const isPublic = b.isPublic !== false;
    const plugins = pluginNames(patch);
    const { category, tags } = inferCategoryAndTags({
      title, description, plugins, category: b.category, tags: b.tags,
    });
    const code = newCode();
    const createdAt = new Date().toISOString();
    fs.writeFileSync(
      path.join(PATCHES, code + '.json'),
      JSON.stringify({ code, title, author, description, category, tags, isPublic, patch, createdAt, downloads: 0 }, null, 2)
    );
    db.patches[code] = { code, title, author, description, category, tags, isPublic, plugins, createdAt, downloads: 0 };
    saveDb();
    return send(res, 201, { code, category, tags });
  }

  const getM = pathname.match(/^\/api\/p\/([A-Za-z0-9]+)$/);
  if (getM && method === 'GET') {
    const code = getM[1].toUpperCase();
    if (!CODE_RE.test(code)) return send(res, 400, { error: 'Code invalide.' });
    const f = path.join(PATCHES, code + '.json');
    if (!fs.existsSync(f)) return send(res, 404, { error: 'Code introuvable. Demande a ton pote de re-publier.' });
    const doc = JSON.parse(fs.readFileSync(f, 'utf8'));
    return send(res, 200, { code: doc.code, title: doc.title, author: doc.author, description: doc.description, patch: doc.patch, createdAt: doc.createdAt, downloads: doc.downloads || 0 });
  }

  const dlM = pathname.match(/^\/api\/p\/([A-Za-z0-9]+)\/download$/);
  if (dlM && method === 'GET') {
    const code = dlM[1].toUpperCase();
    const f = path.join(PATCHES, code + '.json');
    if (!fs.existsSync(f)) return send(res, 404, { error: 'Code introuvable.' });
    const doc = JSON.parse(fs.readFileSync(f, 'utf8'));
    if (db.patches[code]) { db.patches[code].downloads = (db.patches[code].downloads || 0) + 1; saveDb(); }
    doc.downloads = (doc.downloads || 0) + 1;
    fs.writeFileSync(f, JSON.stringify(doc, null, 2));
    return send(res, 200, JSON.stringify(doc.patch, null, 2), { 'Content-Type': 'application/json; charset=utf-8', 'Content-Disposition': `attachment; filename="patchdrop-${code}.patchlink"` });
  }

  if (pathname === '/api/list' && method === 'GET') {
    const url = new URL(req.url, 'http://x');
    const q = (url.searchParams.get('q') || '').trim().toLowerCase();
    const cat = (url.searchParams.get('category') || '').trim().toLowerCase();
    const tagRaw = (url.searchParams.get('tag') || '').trim().toLowerCase().replace(/^#+/, '');
    const tagFilter = tagRaw ? '#' + tagRaw : '';
    const sort = (url.searchParams.get('sort') || 'newest').trim().toLowerCase();
    const limitParam = parseInt(url.searchParams.get('limit') || '100', 10);
    const limit = Number.isFinite(limitParam) && limitParam > 0 ? Math.min(limitParam, 100) : 100;

    const publicAll = Object.values(db.patches).filter((p) => p.isPublic);
    const catCounts = {};
    for (const key of Object.keys(CATEGORIES)) catCounts[key] = 0;
    const tagCounts = {};
    for (const p of publicAll) {
      const c = p.category && CATEGORIES[p.category] ? p.category : 'fx';
      catCounts[c] = (catCounts[c] || 0) + 1;
      for (const t of (p.tags || [])) tagCounts[t] = (tagCounts[t] || 0) + 1;
    }
    const popularTags = Object.entries(tagCounts)
      .map(([tag, count]) => ({ tag, count }))
      .sort((a, b) => b.count - a.count || a.tag.localeCompare(b.tag))
      .slice(0, 24);

    let arr = publicAll.slice();
    if (cat && cat !== 'all') arr = arr.filter((p) => (p.category || 'fx') === cat);
    if (tagFilter) arr = arr.filter((p) => (p.tags || []).includes(tagFilter));
    if (q) {
      arr = arr.filter((p) => [
        p.code,
        p.title,
        p.author,
        p.description || '',
        CATEGORIES[p.category] || p.category || '',
        ...(p.plugins || []),
        ...(p.tags || []),
      ].join(' ').toLowerCase().includes(q));
    }

    if (sort === 'popular') {
      arr.sort((a, b) => (b.downloads || 0) - (a.downloads || 0) || (b.createdAt || '').localeCompare(a.createdAt || ''));
    } else {
      arr.sort((a, b) => (b.createdAt || '').localeCompare(a.createdAt || ''));
    }

    const presets = arr.slice(0, limit).map((p) => ({
      ...p,
      category: p.category && CATEGORIES[p.category] ? p.category : 'fx',
      categoryLabel: CATEGORIES[p.category] || 'FX & Creative',
      tags: p.tags || [],
    }));

    return send(res, 200, {
      presets,
      total: arr.length,
      allTotal: publicAll.length,
      categories: catCounts,
      categoryLabels: CATEGORIES,
      popularTags,
    });
  }

  return send(res, 404, { error: 'Not found' });
}

const server = http.createServer(async (req, res) => {
  try {
    const url = new URL(req.url, 'http://localhost');
    // Accepte d'être servi sous /patchdrop/ (proxy Apache) ou à la racine.
    let pathname = url.pathname;
    if (pathname.startsWith('/patchdrop/')) pathname = pathname.slice('/patchdrop'.length) || '/';
    else if (pathname === '/patchdrop') pathname = '/';
    if (pathname.startsWith('/api/')) return await handleApi(req, res, pathname);
    if (req.method !== 'GET' && req.method !== 'HEAD') return send(res, 405, 'Method not allowed');
    return serveStatic(req, res, pathname);
  } catch (err) {
    const status = err.status || 500;
    if (status === 500) console.error(err);
    send(res, status, { error: status === 500 ? 'Server error' : err.message });
  }
});
server.listen(PORT, HOST, () => console.log(`patchdrop on http://${HOST}:${PORT} (base ${BASE_PATH || '/'})`));
