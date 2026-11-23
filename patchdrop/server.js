// PatchDrop - zero-dependency Node server (Node >= 18)
// Port 3250 localhost, proxied via Apache at /patchdrop/ -> 127.0.0.1:3250/
// Data: ./data/patches/<CODE>.json + ./data/db.json (index)
// API:
//   POST /api/publish {title,author,description,patch,isPublic,category,tags} -> {code}
//   GET  /api/p/:code -> {title,author,description,patch,createdAt,downloads}
//   GET  /api/p/:code/download -> fichier .patchlink
//   GET  /api/p/:code/fst -> fichier .fst FL Studio
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
const TUID_FILE = path.join(ROOT, 'vst3_tuids.json');
let TUID_DB = { byUniqueId: {}, byName: {} };
try { TUID_DB = JSON.parse(fs.readFileSync(TUID_FILE, 'utf8')); } catch {}
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

const JUCE_B64 = '.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+';
function juceDecodeBlock(str) {
  const dot = String(str || '').indexOf('.');
  if (dot <= 0) return Buffer.alloc(0);
  const size = parseInt(str.slice(0, dot), 10);
  if (!Number.isFinite(size) || size <= 0) return Buffer.alloc(0);
  const data = Buffer.alloc(size);
  const src = str.slice(dot + 1);
  let pos = 0;
  for (let i = 0; i < src.length; i++) {
    const val = JUCE_B64.indexOf(src[i]);
    if (val < 0) continue;
    let bitStart = pos, numBits = 6, bits = val, byte = bitStart >> 3, off = bitStart & 7, mask = 0x3f;
    while (numBits > 0 && byte < size) {
      const take = Math.min(numBits, 8 - off);
      const tempMask = (mask << off) | (~((0xffffffff >>> off) << off));
      const tempBits = (bits & ((1 << take) - 1)) << off;
      data[byte] = ((data[byte] & tempMask) | tempBits) & 0xff;
      byte++;
      numBits -= take;
      bits >>= take;
      mask >>= take;
      off = 0;
    }
    pos += 6;
  }
  return data;
}

function extractRawVst3Chunks(stateBase64) {
  if (!stateBase64) return { comp: Buffer.alloc(0), cont: Buffer.alloc(0) };
  try {
    const raw = Buffer.from(stateBase64, 'base64');
    if (raw.length <= 9 || raw.readUInt32LE(0) !== 0x21324356) return { comp: Buffer.alloc(0), cont: Buffer.alloc(0) };
    const xmlLen = raw.readUInt32LE(4);
    const xml = raw.slice(8, 8 + Math.min(raw.length - 8, xmlLen)).toString('utf8');
    const compM = xml.match(/<IComponent>([^<]*)<\/IComponent>/);
    const contM = xml.match(/<IEditController>([^<]*)<\/IEditController>/);
    return {
      comp: compM ? juceDecodeBlock(compM[1].trim()) : Buffer.alloc(0),
      cont: contM ? juceDecodeBlock(contM[1].trim()) : Buffer.alloc(0),
    };
  } catch { return { comp: Buffer.alloc(0), cont: Buffer.alloc(0) }; }
}

function resolveTuidEntry(node) {
  const ids = (node && node.ids) || {};
  const name = ids.name || (node && node.name) || '';
  let flPlugin = ids.flPluginName || name;
  let vendor = ids.manufacturer || (node && node.manufacturer) || '';
  let filePath = ids.fileOrId || (node && node.fileOrId) || '';
  let tuid = Buffer.alloc(16);

  const rawTuidHex = String(ids.tuidHex || '').replace(/[^0-9a-fA-F]/g, '');
  if (rawTuidHex.length === 32) {
    tuid = Buffer.from(rawTuidHex, 'hex');
  } else {
    let uidHex = '';
    if (typeof ids.uniqueId === 'number' && ids.uniqueId !== 0) uidHex = (ids.uniqueId >>> 0).toString(16).padStart(8, '0');
    else if (typeof ids.identifier === 'string' && ids.identifier.includes('-')) {
      const tail = ids.identifier.split('-').pop().toLowerCase();
      if (/^[0-9a-f]{1,8}$/.test(tail)) uidHex = tail.padStart(8, '0');
    }
    let rec = uidHex && TUID_DB.byUniqueId ? TUID_DB.byUniqueId[uidHex] : null;
    if (!rec && name && TUID_DB.byName) {
      const nk = name.toLowerCase().replace(/[^a-z0-9]/g, '');
      const u = TUID_DB.byName[nk];
      if (u && TUID_DB.byUniqueId) rec = TUID_DB.byUniqueId[u];
    }
    if (rec) {
      if (rec.tuid && rec.tuid.length === 32) tuid = Buffer.from(rec.tuid, 'hex');
      if (!ids.flPluginName && rec.flPlugin) flPlugin = rec.flPlugin;
      if (!vendor && rec.vendor) vendor = rec.vendor;
      if (!filePath && rec.file) filePath = rec.file;
    }
  }
  if (!filePath && name) filePath = `C:\\Program Files\\Common Files\\VST3\\${name}.vst3`;
  return { name, flPlugin, vendor, filePath, tuid, bypass: Boolean(node && node.bypass), stateBase64: (node && node.stateBase64) || '' };
}

function writeVarInt(chunks, len) {
  const bytes = [];
  let v = len >>> 0;
  do {
    let b = v & 0x7f;
    v >>>= 7;
    if (v) b |= 0x80;
    bytes.push(b);
  } while (v);
  chunks.push(Buffer.from(bytes));
}
function writeFlVar(chunks, ev, buf) {
  chunks.push(Buffer.from([ev]));
  writeVarInt(chunks, buf.length);
  if (buf.length) chunks.push(buf);
}
function writeFlUtf16(chunks, ev, str) {
  writeFlVar(chunks, ev, Buffer.from(String(str || '') + '\0', 'utf16le'));
}
function appendSub(parts, id, buf) {
  const h = Buffer.alloc(12);
  h.writeUInt32LE(id >>> 0, 0);
  h.writeBigInt64LE(BigInt(buf.length), 4);
  parts.push(h);
  if (buf.length) parts.push(buf);
}
function buildWrapperSub53(stateBase64) {
  const { comp, cont } = extractRawVst3Chunks(stateBase64);
  if (!comp.length && !cont.length) return Buffer.alloc(0);
  const parts = [];
  const ver = Buffer.alloc(4); ver.writeUInt32LE(1, 0); parts.push(ver);
  const cid1 = Buffer.alloc(64); cid1.writeUInt32LE(1, 0);
  appendSub(parts, 1, cid1);
  appendSub(parts, 3, comp);
  appendSub(parts, 2, cont);
  return Buffer.concat(parts);
}
function buildWrapperChunk(info, mixerStyle) {
  const parts = [];
  const ver = Buffer.alloc(4); ver.writeUInt32LE(10, 0); parts.push(ver);
  if (mixerStyle) appendSub(parts, 1, Buffer.from('ffffffffffffffff0c0000000000000000000000', 'hex'));
  appendSub(parts, 2, Buffer.from('00ffffffffffffffff087d5000000000000100000000000000', 'hex'));
  if (mixerStyle) {
    appendSub(parts, 30, Buffer.from('02000000010000000000000000000000', 'hex'));
    appendSub(parts, 31, Buffer.from('000000000100000000000000000000000100000000000000', 'hex'));
    appendSub(parts, 32, Buffer.from('000000000100000000000000', 'hex'));
  }
  appendSub(parts, 50, Buffer.from('07000000000000000000000000000000', 'hex'));
  appendSub(parts, 52, info.tuid);
  if (info.flPlugin) appendSub(parts, 54, Buffer.from(info.flPlugin, 'utf8'));
  if (info.filePath) appendSub(parts, 55, Buffer.from(info.filePath, 'utf8'));
  if (info.vendor) appendSub(parts, 56, Buffer.from(info.vendor, 'utf8'));
  const s53 = buildWrapperSub53(info.stateBase64);
  if (s53.length) appendSub(parts, 53, s53);
  return Buffer.concat(parts);
}
function finalizeFl(hdType, hdTracks, hdPpq, dtChunks) {
  const dt = Buffer.concat(dtChunks);
  const hdr = Buffer.alloc(22);
  hdr.write('FLhd', 0, 'ascii');
  hdr.writeUInt32LE(6, 4);
  hdr.writeUInt16LE(hdType, 8);
  hdr.writeUInt16LE(hdTracks, 10);
  hdr.writeUInt16LE(hdPpq, 12);
  hdr.write('FLdt', 14, 'ascii');
  hdr.writeUInt32LE(dt.length, 18);
  return Buffer.concat([hdr, dt]);
}
function buildFlStudioFstBuffer(doc) {
  const patch = (doc && doc.patch) || {};
  const rawNodes = Array.isArray(patch.nodes) ? patch.nodes.slice() : [];
  rawNodes.sort((a, b) => (a.order ?? 0) - (b.order ?? 0));
  const vstNodes = rawNodes.filter((n) => {
    const fmt = (n && n.ids && n.ids.format) || (n && n.format) || 'VST3';
    return String(fmt).toUpperCase() === 'VST3';
  });
  if (!vstNodes.length) return null;

  if (vstNodes.length === 1) {
    const info = resolveTuidEntry(vstNodes[0]);
    const chunks = [];
    writeFlVar(chunks, 199, Buffer.from('20.8.4.2576\0', 'ascii'));
    chunks.push(Buffer.from([28, 3]));
    writeFlUtf16(chunks, 201, 'Fruity Wrapper');
    const d4 = Buffer.alloc(52);
    d4.writeUInt32LE(info.bypass ? 0 : 2, 8);
    writeFlVar(chunks, 212, d4);
    writeFlUtf16(chunks, 203, info.name || info.flPlugin);
    writeFlVar(chunks, 213, buildWrapperChunk(info, false));
    return finalizeFl(0x0030, 0x0002, 0x0030, chunks);
  }

  const trackName = (doc && doc.title) || patch.name || 'PatchLink';
  const chunks = [];
  writeFlVar(chunks, 199, Buffer.from('20.8.4.2576\0', 'ascii'));
  const evHead = Buffer.alloc(14);
  evHead[0] = 159; evHead.writeUInt32LE(0x0a10, 1);
  evHead[5] = 28; evHead[6] = 1; evHead[7] = 37; evHead[8] = 1;
  evHead[9] = 149; evHead.writeUInt32LE(0x00c04184, 10);
  chunks.push(evHead);
  writeFlUtf16(chunks, 204, trackName);
  writeFlVar(chunks, 236, Buffer.from('000000004c00000000000000', 'hex'));

  for (let slot = 0; slot < 10; slot++) {
    if (slot < vstNodes.length) {
      const info = resolveTuidEntry(vstNodes[slot]);
      writeFlUtf16(chunks, 201, 'Fruity Wrapper');
      const d4 = Buffer.alloc(52);
      d4.writeUInt32LE(11, 0);
      d4.writeUInt32LE(slot, 4);
      d4.writeUInt32LE(info.bypass ? 0 : 2, 8);
      d4.writeUInt32LE(320, 16);
      writeFlVar(chunks, 212, d4);
      writeFlUtf16(chunks, 203, info.name || info.flPlugin);
      const meta = Buffer.alloc(10);
      meta[0] = 155; meta.writeUInt32LE(0, 1);
      meta[5] = 128; meta.writeUInt32LE(0x00565148, 6);
      chunks.push(meta);
      writeFlVar(chunks, 213, buildWrapperChunk(info, true));
    }
    const endSlot = Buffer.alloc(3);
    endSlot[0] = 98; endSlot.writeUInt16LE(slot, 1);
    chunks.push(endSlot);
  }
  return finalizeFl(0x0040, 0x0006, 0x0060, chunks);
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

  const fstM = pathname.match(/^\/api\/p\/([A-Za-z0-9]+)\/(?:fst|[^/]+\.fst)$/);
  if (fstM && (method === 'GET' || method === 'HEAD')) {
    const code = fstM[1].toUpperCase();
    if (!CODE_RE.test(code)) return send(res, 400, { error: 'Code invalide.' });
    const f = path.join(PATCHES, code + '.json');
    if (!fs.existsSync(f)) return send(res, 404, { error: 'Code introuvable.' });
    const doc = JSON.parse(fs.readFileSync(f, 'utf8'));
    const fstBuf = buildFlStudioFstBuffer(doc);
    if (!fstBuf) return send(res, 400, { error: 'Aucun plugin VST3 exportable en .fst dans ce preset.' });
    res.writeHead(200, {
      'Content-Type': 'application/octet-stream',
      'Content-Length': String(fstBuf.length),
      'Content-Disposition': `attachment; filename="patchdrop-${code}.fst"`,
      'Cache-Control': 'no-store',
      'X-Content-Type-Options': 'nosniff',
    });
    return res.end(method === 'HEAD' ? undefined : fstBuf);
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
