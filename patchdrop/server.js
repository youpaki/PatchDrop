// PatchDrop - zero-dependency Node server (Node >= 18)
// Port 3250 localhost, proxied via Apache at /patchdrop/ -> 127.0.0.1:3250/
// Data: ./data/patches/<CODE>.json + ./data/db.json (index)
// API:
//   POST /api/publish {title,author,description,patch,isPublic} -> {code}
//   GET  /api/p/:code -> {title,author,description,patch,createdAt,downloads}
//   GET  /api/p/:code/download -> fichier .patchlink
//   GET  /api/list?q= -> [{code,title,author,description,plugins,createdAt,downloads}]
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
let db = loadDb();
function saveDb() {
  const tmp = DB_FILE + '.tmp';
  fs.writeFileSync(tmp, JSON.stringify(db, null, 2));
  fs.renameSync(tmp, DB_FILE);
}
(() => {
  let changed = false;
  for (const [code, p] of Object.entries(db.patches || {})) {
    if (Array.isArray(p.plugins) && p.plugins.includes('?')) {
      try {
        const doc = JSON.parse(fs.readFileSync(path.join(PATCHES, code + '.json'), 'utf8'));
        const next = pluginNames(doc.patch);
        if (next.length && JSON.stringify(next) !== JSON.stringify(p.plugins)) {
          p.plugins = next;
          changed = true;
        }
      } catch {}
    }
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
    const code = newCode();
    fs.writeFileSync(path.join(PATCHES, code + '.json'), JSON.stringify({ code, title, author, description, isPublic, patch, createdAt: new Date().toISOString(), downloads: 0 }, null, 2));
    db.patches[code] = { code, title, author, description, isPublic, plugins: pluginNames(patch), createdAt: new Date().toISOString(), downloads: 0 };
    saveDb();
    return send(res, 201, { code });
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
    const q = (url.searchParams.get('q') || '').toLowerCase();
    let arr = Object.values(db.patches).filter((p) => p.isPublic);
    if (q) arr = arr.filter((p) => (p.title + ' ' + p.author + ' ' + (p.description || '') + ' ' + (p.plugins || []).join(' ')).toLowerCase().includes(q));
    arr.sort((a, b) => (b.createdAt || '').localeCompare(a.createdAt || ''));
    return send(res, 200, { presets: arr.slice(0, 100), total: arr.length });
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
