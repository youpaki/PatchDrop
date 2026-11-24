// PatchDrop front: community presets explorer, multi-DAW browser drag-in/out, and publisher
const TR = (k, vars) => (window.PD_I18N ? window.PD_I18N.t(k, vars) : k);
const CATEGORY_ORDER = ['vocals', 'master', 'drums', 'synths', 'inst', 'fx'];
function catLabel(key) {
  return TR('cat.' + (key || 'fx'));
}
const CATEGORY_LABELS = new Proxy({}, {
  get: (_, prop) => catLabel(String(prop)),
});

const JUCE_B64 = '.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+';

let cachedTuidDb = null;
async function ensureTuidDb() {
  if (cachedTuidDb) return cachedTuidDb;
  try {
    const r = await fetch('api/tuids');
    if (r.ok) cachedTuidDb = await r.json();
  } catch {}
  if (!cachedTuidDb) cachedTuidDb = { byUniqueId: {}, byFuid: {}, byName: {} };
  return cachedTuidDb;
}

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

function base64ToBytes(b64) {
  const clean = String(b64 || '').replace(/[^A-Za-z0-9+/=]/g, '');
  if (!clean) return new Uint8Array(0);
  const bin = atob(clean);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

function hexToBytes(hexStr) {
  const clean = String(hexStr || '').replace(/[^0-9a-fA-F]/g, '');
  const len = clean.length >> 1;
  const out = new Uint8Array(len);
  for (let i = 0; i < len; i++) out[i] = parseInt(clean.slice(i * 2, i * 2 + 2), 16) & 0xff;
  return out;
}

function bytesToHex(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i++) s += bytes[i].toString(16).padStart(2, '0');
  return s;
}

function tuidToFuidHex(raw16) {
  const dv = new DataView(raw16.buffer, raw16.byteOffset, 16);
  const d1 = dv.getUint32(0, true).toString(16).padStart(8, '0').toUpperCase();
  const d2 = dv.getUint16(4, true).toString(16).padStart(4, '0').toUpperCase();
  const d3 = dv.getUint16(6, true).toString(16).padStart(4, '0').toUpperCase();
  return d1 + d2 + d3 + bytesToHex(raw16.subarray(8, 16)).toUpperCase();
}

function fuidToTuidBytes(fuidHex) {
  const clean = String(fuidHex || '').replace(/[^0-9a-fA-F]/g, '');
  if (clean.length !== 32) return new Uint8Array(0);
  const out = new Uint8Array(16);
  const dv = new DataView(out.buffer);
  dv.setUint32(0, parseInt(clean.slice(0, 8), 16) >>> 0, true);
  dv.setUint16(4, parseInt(clean.slice(8, 12), 16) & 0xffff, true);
  dv.setUint16(6, parseInt(clean.slice(12, 16), 16) & 0xffff, true);
  out.set(hexToBytes(clean.slice(16, 32)), 8);
  return out;
}

function uidFromFuidHex(fuidHex) {
  const clean = String(fuidHex || '').replace(/[^0-9a-fA-F]/g, '').toUpperCase();
  if (clean.length !== 32 || /^0+$/.test(clean)) return 0;
  let h = 0n;
  for (let i = 0; i < 4; i++) {
    h = ((h * 31n) + BigInt(parseInt(clean.slice(i * 8, i * 8 + 8), 16) >>> 0)) & 0xffffffffn;
  }
  const n = Number(h);
  return n >= 0x80000000 ? n - 0x100000000 : n;
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

function packVst3ChunksToBase64(comp, cont) {
  const hasComp = comp && comp.length > 0;
  const hasCont = cont && cont.length > 0;
  if (!hasComp && !hasCont) return '';
  let xml = '<VST3PluginState>';
  if (hasComp) xml += `<IComponent>${juceEncodeBlock(comp)}</IComponent>`;
  if (hasCont) xml += `<IEditController>${juceEncodeBlock(cont)}</IEditController>`;
  xml += '</VST3PluginState>';
  const xmlBytes = new TextEncoder().encode(xml);
  const block = new Uint8Array(9 + xmlBytes.length);
  const bdv = new DataView(block.buffer);
  bdv.setUint32(0, 0x21324356, true);
  bdv.setUint32(4, xmlBytes.length, true);
  block.set(xmlBytes, 8);
  block[8 + xmlBytes.length] = 0;
  return bytesToBase64(block);
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
  return packVst3ChunksToBase64(comp, cont);
}

function buildNodesFromPlugins(plugins, skipped, baseTitle, dawLabel, tuidDb = null) {
  const nodes = plugins.map((e, idx) => {
    let fuid = (e.fuidHex || '').toUpperCase();
    let tuidHex = e.tuidHex || '';
    if (!fuid && tuidHex.length === 32) fuid = tuidToFuidHex(hexToBytes(tuidHex));
    if (!tuidHex && fuid.length === 32) tuidHex = bytesToHex(fuidToTuidBytes(fuid));
    let uniqueId = e.uniqueId || (fuid.length === 32 ? uidFromFuidHex(fuid) : 0);
    const uidHex = (uniqueId >>> 0).toString(16).padStart(8, '0');

    let pluginName = e.pluginName || '';
    let slotName = e.slotName || '';
    let vendor = e.vendor || '';
    let filePath = e.filePath || '';
    let reaperHash = e.reaperHash || 0;

    if (tuidDb && tuidDb.byUniqueId) {
      let rec = (fuid && tuidDb.byFuid && tuidDb.byFuid[fuid]) ? tuidDb.byUniqueId[tuidDb.byFuid[fuid]] : null;
      if (!rec && uniqueId) rec = tuidDb.byUniqueId[uidHex];
      if (!rec && tuidDb.byName && pluginName) {
        const nk = pluginName.toLowerCase().replace(/[^a-z0-9]/g, '');
        if (nk && tuidDb.byName[nk]) rec = tuidDb.byUniqueId[tuidDb.byName[nk]];
      }
      if (rec) {
        if (!pluginName && rec.flPlugin) pluginName = rec.flPlugin;
        if (!slotName && rec.flPlugin) slotName = rec.flPlugin;
        if (!vendor && rec.vendor) vendor = rec.vendor;
        if (!filePath && rec.file) filePath = rec.file;
        if (!reaperHash && rec.reaperHash) reaperHash = rec.reaperHash;
        if (!fuid && rec.fuid) fuid = rec.fuid;
        if (!tuidHex && rec.tuid) tuidHex = rec.tuid;
      }
    }

    const displayName = slotName || pluginName || (fuid ? `VST3 ${fuid.slice(0, 8)}` : `Plugin ${idx + 1}`);
    const ids = {
      name: displayName,
      manufacturer: vendor,
      version: '',
      format: 'VST3',
      fileOrId: filePath,
      identifier: (!e.isVst2 && uniqueId !== 0) ? `VST3-${displayName}-0-${uidHex}` : '',
    };
    if (uniqueId !== 0) ids.uniqueId = uniqueId;
    if (tuidHex) ids.tuidHex = tuidHex;
    if (fuid) ids.fuidHex = fuid;
    if (reaperHash) ids.reaperHash = reaperHash;
    if (pluginName) ids.flPluginName = pluginName;
    return {
      uid: 100 + idx,
      x: 280 + (idx % 4) * 230,
      y: 200 + Math.floor(idx / 4) * 140,
      order: idx,
      bypass: Boolean(e.bypass),
      wet: 1.0,
      gainDb: 0.0,
      ids,
      stateBase64: e.isVst2 ? '' : (e.stateBase64 || ''),
    };
  });

  if (!nodes.length) {
    throw new Error(skipped.length
      ? `No VST3 plugins found (skipped: ${skipped.join(', ')})`
      : `No VST3 plugins found inside this ${dawLabel} file.`);
  }

  return {
    patch: { app: 'PatchLink', chainFormat: 2, name: baseTitle || `${dawLabel} Chain`, nodes, connections: [] },
    skipped,
  };
}

function parseFstBufferInBrowser(arrayBuffer, fileName = 'FL Chain', tuidDb = null) {
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
            slotName: curSlotName, pluginName: '', vendor: '', filePath: '',
            tuidHex: '', uniqueId: 0, bypass: hasD4 && d4_4 !== 0 && d4_2 === 0,
            isVst2: false, stateBase64: '',
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
          if (entry.pluginName || entry.slotName || entry.filePath || entry.tuidHex || entry.uniqueId) plugins.push(entry);
        } else if (curPlugin && curPlugin.toLowerCase() !== 'fruity wrapper') {
          skipped.push(curPlugin + ' (FL native)');
        }
      }
    }
  }

  return buildNodesFromPlugins(plugins, skipped, trackName || fileName.replace(/\.[^/.]+$/, ''), 'FL Studio', tuidDb);
}

function tokenizeReaperLine(line) {
  const out = [];
  let i = 0;
  while (i < line.length) {
    while (i < line.length && /\s/.test(line[i])) i++;
    if (i >= line.length) break;
    const ch = line[i];
    if (ch === '"' || ch === "'") {
      i++;
      const s = i;
      while (i < line.length && line[i] !== ch) i++;
      out.push(line.slice(s, i));
      if (i < line.length) i++;
    } else {
      const s = i;
      while (i < line.length && !/\s/.test(line[i])) i++;
      out.push(line.slice(s, i));
    }
  }
  return out;
}

function parseRfxChainInBrowser(text, fileName = 'REAPER Chain', tuidDb = null) {
  const lines = String(text || '').split(/\r?\n/);
  const plugins = [];
  const skipped = [];
  let pendingBypass = false;

  for (let i = 0; i < lines.length; i++) {
    const trimmed = lines[i].trim();
    if (!trimmed) continue;
    if (trimmed.startsWith('BYPASS ')) {
      const parts = tokenizeReaperLine(trimmed);
      pendingBypass = parts.length > 1 && parseInt(parts[1], 10) !== 0;
      continue;
    }
    if (/^<(JS|AU|DX)\s/i.test(trimmed)) {
      const parts = tokenizeReaperLine(trimmed.slice(1));
      skipped.push((parts[1] || trimmed) + ' (REAPER FX)');
      while (++i < lines.length && lines[i].trim() !== '>') {}
      pendingBypass = false;
      continue;
    }
    if (/^<VST\s/i.test(trimmed)) {
      const args = tokenizeReaperLine(trimmed.slice(4));
      const rawDisplay = args[0] || 'Plugin';
      const fileToken = args[1] || '';
      let idToken = '';
      for (let t = args.length - 1; t >= 2; t--) {
        if (args[t].includes('{') || args[t].includes('<')) { idToken = args[t]; break; }
      }
      const isVst3 = /^VST3:/i.test(rawDisplay) || fileToken.toLowerCase().endsWith('.vst3') || idToken.includes('{');
      let cleanName = rawDisplay.replace(/^VST3:\s*/i, '').replace(/^VST:\s*/i, '').trim();
      let vendor = '';
      const vm = cleanName.match(/^(.*?)\s*\(([^()]+)\)$/);
      if (vm) { cleanName = vm[1].trim(); vendor = vm[2].trim(); }

      const b64Lines = [];
      while (++i < lines.length) {
        const inner = lines[i].trim();
        if (inner === '>') break;
        if (inner && !inner.startsWith('<')) b64Lines.push(inner);
      }

      if (!isVst3) {
        skipped.push(cleanName + ' (VST2)');
        pendingBypass = false;
        continue;
      }

      const idM = idToken.match(/^(-?\d+)?\{([0-9a-fA-F]{32})\}/);
      const reaperHash = idM && idM[1] ? (parseInt(idM[1], 10) | 0) : 0;
      const fuidHex = idM ? idM[2].toUpperCase() : '';

      const chunks = [];
      let curB64 = '';
      let expChunk1Len = 0;
      const flushChunk = (isFirst) => {
        if (!curB64) return;
        try {
          const mb = base64ToBytes(curB64);
          if (mb.length) {
            if (isFirst && mb.length >= 16) {
              const dv = new DataView(mb.buffer, mb.byteOffset, mb.byteLength);
              if (dv.getUint32(4, true) === 0xfeed5eee) expChunk1Len = dv.getUint32(mb.length - 12, true);
            }
            chunks.push(mb);
          }
        } catch {}
        curB64 = '';
      };

      for (const l of b64Lines) {
        curB64 += l;
        const isFirst = chunks.length === 0;
        const endedByLen = l.length < 128 || l.endsWith('=');
        const endedBySize = chunks.length === 1 && expChunk1Len > 0
          && ((curB64.length * 3) >> 2) >= expChunk1Len && (curB64.length % 4) === 0;
        if (endedByLen || endedBySize) flushChunk(isFirst);
      }
      if (curB64) flushChunk(chunks.length === 0);

      let stateBase64 = '';
      if (chunks.length >= 2 && chunks[1].length >= 8) {
        const c1 = chunks[1];
        const dv = new DataView(c1.buffer, c1.byteOffset, c1.byteLength);
        const compLen = dv.getUint32(0, true);
        if (8 + compLen <= c1.length) {
          const comp = c1.subarray(8, 8 + compLen);
          const remOff = 8 + compLen;
          let cont = new Uint8Array(0);
          if (remOff + 8 <= c1.length) {
            const contLen = dv.getUint32(remOff, true);
            if (remOff + 8 + contLen <= c1.length) cont = c1.subarray(remOff + 8, remOff + 8 + contLen);
          }
          stateBase64 = packVst3ChunksToBase64(comp, cont);
        }
      }

      plugins.push({
        slotName: cleanName, pluginName: cleanName, vendor, filePath: fileToken,
        fuidHex, reaperHash, bypass: pendingBypass, isVst2: false, stateBase64,
      });
      pendingBypass = false;
    }
  }

  return buildNodesFromPlugins(plugins, skipped, fileName.replace(/\.[^/.]+$/, ''), 'REAPER', tuidDb);
}

async function gunzipIfNeededBytes(arrayBuffer) {
  const bytes = new Uint8Array(arrayBuffer);
  if (bytes.length >= 2 && bytes[0] === 0x1f && bytes[1] === 0x8b && typeof DecompressionStream !== 'undefined') {
    const ds = new DecompressionStream('gzip');
    const stream = new Blob([bytes]).stream().pipeThrough(ds);
    return new Uint8Array(await new Response(stream).arrayBuffer());
  }
  return bytes;
}

function xmlChild(parent, tagName) {
  if (!parent || !parent.children) return null;
  for (let i = 0; i < parent.children.length; i++) {
    if (parent.children[i].tagName === tagName) return parent.children[i];
  }
  return null;
}

function xmlAttrVal(parent, tagName) {
  const el = xmlChild(parent, tagName);
  return el ? (el.getAttribute('Value') || '').trim() : '';
}

function inferVst3NameFromFuidAndState(fuidHex, comp, cont) {
  const u = String(fuidHex || '').toUpperCase();
  const contHead = cont && cont.length >= 4 ? String.fromCharCode(cont[0], cont[1], cont[2], cont[3]) : '';
  if (u === 'ED57BD725C60467EA64DD2F400758B6F' || contHead === 'FQ4p') return 'FabFilter Pro-Q 4';
  if (u === '72C4DB717A4D459AB97E51745D84B39D' || contHead === 'FQ3p') return 'FabFilter Pro-Q 3';
  if (u === 'ABCDEF019182FAEB416E747256727662') return 'Vocal Reverb';
  if (u === '565354415438314175746F2D54756E65') return 'Auto-Tune';
  if (u.length === 32 && u.startsWith('565354')) {
    const b = hexToBytes(u);
    let s = '';
    for (let i = 7; i < 16 && b[i] !== 0; i++) {
      if (b[i] >= 32 && b[i] < 127) s += String.fromCharCode(b[i]);
      else break;
    }
    if (s.trim().length >= 3) return s.trim();
  }
  if (u.length === 32 && u.startsWith('ABCDEF019182FAEB')) {
    const b = hexToBytes(u);
    const ascii = (from, to) => Array.from(b.subarray(from, to)).every((c) => c >= 32 && c < 127)
      ? String.fromCharCode(...b.subarray(from, to)) : '';
    const v = ascii(8, 12), p = ascii(12, 16);
    if (v && p) return `${v} ${p}`;
  }
  return '';
}

function inferVst2NameFromUniqueId(uidStr) {
  const u = (parseInt(uidStr || '0', 10) >>> 0);
  if (!u) return '';
  const bytes = [(u >>> 24) & 255, (u >>> 16) & 255, (u >>> 8) & 255, u & 255];
  if (!bytes.every((c) => c >= 32 && c < 127)) return '';
  const code = String.fromCharCode(...bytes);
  const map = {
    FQ22: 'FabFilter Pro-Q 2', FQ33: 'FabFilter Pro-Q 3', FQ44: 'FabFilter Pro-Q 4',
    FC22: 'FabFilter Pro-C 2', FL22: 'FabFilter Pro-L 2', FR11: 'FabFilter Pro-R',
    FD22: 'FabFilter Pro-DS', FM22: 'FabFilter Pro-MB', FS22: 'FabFilter Saturn 2',
  };
  return map[code] || '';
}

async function parseAbletonAdgInBrowser(arrayBuffer, fileName = 'Ableton Rack', tuidDb = null) {
  const xmlBytes = await gunzipIfNeededBytes(arrayBuffer);
  const xmlText = new TextDecoder('utf-8').decode(xmlBytes);
  const doc = new DOMParser().parseFromString(xmlText, 'text/xml');
  if (!doc || doc.querySelector('parsererror')) throw new Error('Invalid Ableton preset XML.');

  const groupNameEl = doc.querySelector('GroupDevicePreset > Device > AudioEffectGroupDevice > UserName');
  const rackName = (groupNameEl && groupNameEl.getAttribute('Value')) || fileName.replace(/\.[^/.]+$/, '');
  const plugins = [];
  const skipped = [];

  for (const pd of doc.getElementsByTagName('PluginDevice')) {
    const onEl = xmlChild(pd, 'On');
    const bypass = onEl ? xmlAttrVal(onEl, 'Manual').toLowerCase() === 'false' : false;
    const descEl = xmlChild(pd, 'PluginDesc');
    const v3 = xmlChild(descEl, 'Vst3PluginInfo');
    if (v3) {
      const presetWrap = xmlChild(v3, 'Preset');
      const vp = xmlChild(presetWrap, 'Vst3Preset');
      const plugName = xmlAttrVal(v3, 'PlugName') || xmlAttrVal(v3, 'Name')
        || xmlAttrVal(vp, 'PlugName') || xmlAttrVal(vp, 'Name');
      const uidEl = xmlChild(v3, 'Uid') || xmlChild(vp, 'Uid');
      let fuidHex = '';
      if (uidEl) {
        fuidHex = [0, 1, 2, 3].map((k) => {
          const val = xmlAttrVal(uidEl, `Fields.${k}`);
          const v = val ? (parseInt(val, 10) >>> 0) : 0;
          return v.toString(16).padStart(8, '0').toUpperCase();
        }).join('');
      }
      const psEl = xmlChild(vp, 'ProcessorState');
      const csEl = xmlChild(vp, 'ControllerState');
      const comp = psEl ? hexToBytes(psEl.textContent) : new Uint8Array(0);
      const cont = csEl ? hexToBytes(csEl.textContent) : new Uint8Array(0);
      const hasTuidMatch = !plugName && tuidDb && tuidDb.byFuid && fuidHex && tuidDb.byFuid[fuidHex];
      const resolvedName = plugName || (hasTuidMatch ? '' : inferVst3NameFromFuidAndState(fuidHex, comp, cont));
      plugins.push({
        slotName: resolvedName, pluginName: resolvedName, vendor: '', filePath: '',
        fuidHex, bypass, isVst2: false, stateBase64: packVst3ChunksToBase64(comp, cont),
      });
      continue;
    }
    const v2 = xmlChild(descEl, 'VstPluginInfo');
    if (v2) {
      const rawName = xmlAttrVal(v2, 'PlugName') || xmlAttrVal(v2, 'Name')
        || inferVst2NameFromUniqueId(xmlAttrVal(v2, 'UniqueId'));
      if (rawName) {
        const cleanName = rawName.replace(/_(x64|x86)$/i, '').trim();
        plugins.push({
          slotName: rawName, pluginName: cleanName, vendor: '', filePath: '',
          fuidHex: '', bypass, isVst2: true, stateBase64: '',
        });
      } else {
        skipped.push('VST2 Plugin');
      }
      continue;
    }
    const au = xmlChild(descEl, 'AuPluginInfo');
    if (au) {
      const rawName = xmlAttrVal(au, 'Name') || xmlAttrVal(au, 'PlugName');
      if (rawName) {
        plugins.push({
          slotName: rawName, pluginName: rawName, vendor: '', filePath: '',
          fuidHex: '', bypass, isVst2: true, stateBase64: '',
        });
      } else {
        skipped.push('Audio Unit');
      }
    }
  }

  for (const vp of doc.getElementsByTagName('Vst3Preset')) {
    if (vp.closest('PluginDevice')) continue;
    const bypass = xmlAttrVal(vp, 'IsOn').toLowerCase() === 'false';
    const plugName = xmlAttrVal(vp, 'PlugName') || xmlAttrVal(vp, 'Name');
    const uidEl = xmlChild(vp, 'Uid');
    let fuidHex = '';
    if (uidEl) {
      fuidHex = [0, 1, 2, 3].map((k) => {
        const val = xmlAttrVal(uidEl, `Fields.${k}`);
        const v = val ? (parseInt(val, 10) >>> 0) : 0;
        return v.toString(16).padStart(8, '0').toUpperCase();
      }).join('');
    }
    const psEl = xmlChild(vp, 'ProcessorState');
    const csEl = xmlChild(vp, 'ControllerState');
    const comp = psEl ? hexToBytes(psEl.textContent) : new Uint8Array(0);
    const cont = csEl ? hexToBytes(csEl.textContent) : new Uint8Array(0);
    const hasTuidMatch = !plugName && tuidDb && tuidDb.byFuid && fuidHex && tuidDb.byFuid[fuidHex];
    const resolvedName = plugName || (hasTuidMatch ? '' : inferVst3NameFromFuidAndState(fuidHex, comp, cont));
    if (fuidHex.length === 32 || resolvedName) {
      plugins.push({
        slotName: resolvedName, pluginName: resolvedName, vendor: '', filePath: '',
        fuidHex, bypass, isVst2: false, stateBase64: packVst3ChunksToBase64(comp, cont),
      });
    }
  }

  for (const v2 of doc.getElementsByTagName('VstPreset')) {
    if (v2.closest('PluginDevice')) continue;
    const bypass = xmlAttrVal(v2, 'IsOn').toLowerCase() === 'false';
    const uidVal = xmlAttrVal(v2, 'UniqueId');
    const rawName = xmlAttrVal(v2, 'PlugName') || xmlAttrVal(v2, 'Name') || inferVst2NameFromUniqueId(uidVal);
    if (rawName) {
      const cleanName = rawName.replace(/_(x64|x86)$/i, '').trim();
      plugins.push({
        slotName: rawName, pluginName: cleanName, vendor: '', filePath: '',
        fuidHex: '', bypass, isVst2: true, stateBase64: '',
      });
    } else {
      skipped.push(uidVal ? `VST2 Plugin (${uidVal})` : 'VST2 Plugin');
    }
  }

  return buildNodesFromPlugins(plugins, skipped, rackName, 'Ableton Live', tuidDb);
}

function scanBytesForVstPresets(bytes, fallbackName, outPlugins) {
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let pos = 0;
  while (pos + 48 <= bytes.length) {
    if (bytes[pos] === 0x56 && bytes[pos + 1] === 0x53 && bytes[pos + 2] === 0x54 && bytes[pos + 3] === 0x33
        && dv.getUint32(pos + 4, true) === 1) {
      const fuid = new TextDecoder('ascii').decode(bytes.subarray(pos + 8, pos + 40));
      if (/^[0-9a-fA-F]{32}$/.test(fuid)) {
        const listOffset = Number(dv.getBigInt64(pos + 40, true));
        const absList = pos + listOffset;
        if (listOffset >= 48 && absList + 8 <= bytes.length
            && bytes[absList] === 0x4c && bytes[absList + 1] === 0x69
            && bytes[absList + 2] === 0x73 && bytes[absList + 3] === 0x74) {
          const count = dv.getInt32(absList + 4, true);
          if (count >= 0 && count <= 64 && absList + 8 + count * 20 <= bytes.length) {
            let comp = new Uint8Array(0);
            let cont = new Uint8Array(0);
            let maxEnd = absList + 8 + count * 20;
            for (let i = 0; i < count; i++) {
              const row = absList + 8 + i * 20;
              const tag = String.fromCharCode(bytes[row], bytes[row + 1], bytes[row + 2], bytes[row + 3]);
              const off = Number(dv.getBigInt64(row + 4, true));
              const sz = Number(dv.getBigInt64(row + 12, true));
              if (off >= 48 && sz >= 0 && pos + off + sz <= bytes.length) {
                maxEnd = Math.max(maxEnd, pos + off + sz);
                if (tag === 'Comp' && sz > 0) comp = bytes.subarray(pos + off, pos + off + sz);
                else if (tag === 'Cont' && sz > 0) cont = bytes.subarray(pos + off, pos + off + sz);
              }
            }
            outPlugins.push({
              slotName: fallbackName, pluginName: '', vendor: '', filePath: '',
              fuidHex: fuid.toUpperCase(), bypass: false, isVst2: false,
              stateBase64: packVst3ChunksToBase64(comp, cont),
            });
            pos = maxEnd;
            continue;
          }
        }
      }
    }
    pos++;
  }
}

async function parseVstPresetInBrowser(arrayBuffer, fileName = 'VST3 Preset', tuidDb = null) {
  let bytes = await gunzipIfNeededBytes(arrayBuffer);
  const plugins = [];
  const baseName = fileName.replace(/\.[^/.]+$/, '');

  // Handle ZIP archives (e.g. Studio One .fxchain / .multipreset) via central directory + deflate-raw
  if (bytes.length >= 4 && bytes[0] === 0x50 && bytes[1] === 0x4b && bytes[2] === 0x03 && bytes[3] === 0x04) {
    const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    let p = 0;
    while (p + 30 <= bytes.length && dv.getUint32(p, true) === 0x04034b50) {
      const method = dv.getUint16(p + 8, true);
      const compSz = dv.getUint32(p + 18, true);
      const fnLen = dv.getUint16(p + 26, true);
      const exLen = dv.getUint16(p + 28, true);
      const dataStart = p + 30 + fnLen + exLen;
      if (compSz > 0 && dataStart + compSz <= bytes.length) {
        const chunk = bytes.subarray(dataStart, dataStart + compSz);
        let inflated = null;
        if (method === 0) inflated = chunk;
        else if (method === 8 && typeof DecompressionStream !== 'undefined') {
          try {
            const ds = new DecompressionStream('deflate-raw');
            inflated = new Uint8Array(await new Response(new Blob([chunk]).stream().pipeThrough(ds)).arrayBuffer());
          } catch {}
        }
        if (inflated) {
          scanBytesForVstPresets(inflated, '', plugins);
          const text = new TextDecoder('utf-8', { fatal: false }).decode(inflated);
          for (const m of text.matchAll(/VlNUMw[A-Za-z0-9+/=\s]{40,}/g)) {
            scanBytesForVstPresets(base64ToBytes(m[0]), '', plugins);
          }
        }
        p = dataStart + compSz;
      } else {
        break;
      }
    }
  } else {
    scanBytesForVstPresets(bytes, plugins.length === 0 ? '' : baseName, plugins);
    if (!plugins.length) {
      const text = new TextDecoder('utf-8', { fatal: false }).decode(bytes);
      for (const m of text.matchAll(/VlNUMw[A-Za-z0-9+/=\s]{40,}/g)) {
        scanBytesForVstPresets(base64ToBytes(m[0]), '', plugins);
      }
    }
  }

  return buildNodesFromPlugins(plugins, [], baseName, 'VST3 Preset', tuidDb);
}

async function parseAnyDawFileInBrowser(file) {
  const lower = file.name.toLowerCase();
  if (lower.endsWith('.patchlink') || lower.endsWith('.json')) {
    const patch = JSON.parse(await file.text());
    if (!patch || !Array.isArray(patch.nodes)) throw new Error('Invalid .patchlink JSON.');
    return { patch, skipped: [] };
  }
  const tuidDb = await ensureTuidDb();
  const ab = await file.arrayBuffer();
  if (lower.endsWith('.fst')) return parseFstBufferInBrowser(ab, file.name, tuidDb);
  if (lower.endsWith('.rfxchain') || lower.endsWith('.rpp')) {
    return parseRfxChainInBrowser(new TextDecoder('utf-8').decode(ab), file.name, tuidDb);
  }
  if (lower.endsWith('.adg') || lower.endsWith('.adv') || lower.endsWith('.als')) {
    return parseAbletonAdgInBrowser(ab, file.name, tuidDb);
  }
  return parseVstPresetInBrowser(ab, file.name, tuidDb);
}

async function api(path, opts) {
  const r = await fetch(path, opts);
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(j.error || ('HTTP ' + r.status));
  return j;
}

function attachDawDragData(ev, p, ext = 'fst') {
  const absUrl = new URL(`api/p/${p.code}/patchdrop-${p.code}.${ext}`, window.location.href).href;
  if (ev.dataTransfer) {
    ev.dataTransfer.effectAllowed = 'copy';
    ev.dataTransfer.setData('DownloadURL', `application/octet-stream:patchdrop-${p.code}.${ext}:${absUrl}`);
    ev.dataTransfer.setData('text/uri-list', absUrl);
    ev.dataTransfer.setData('text/plain', absUrl);
  }
}

function buildPresetCard(p, { isPreview = false, activeTag = '', onTagSelect = null } = {}) {
  const el = document.createElement('div');
  el.className = 'card';
  el.draggable = true;
  el.title = TR('card.dragTip');
  el.addEventListener('dragstart', (ev) => attachDawDragData(ev, p, 'fst'));

  const head = document.createElement('div');
  head.className = 'card-head';
  const code = document.createElement('div');
  code.className = 'code';
  code.textContent = p.code || '';
  const badge = document.createElement('span');
  badge.className = 'cat-badge';
  badge.textContent = catLabel(p.category || 'fx');
  head.append(code, badge);

  const title = document.createElement('h3');
  title.textContent = p.title || TR('card.untitled');
  el.append(head, title);

  if (p.description) {
    const desc = document.createElement('p');
    desc.className = 'card-desc';
    desc.textContent = p.description;
    el.appendChild(desc);
  }

  const meta = document.createElement('div');
  meta.className = 'meta';
  const locale = (window.PD_I18N && window.PD_I18N.getLang() === 'fr') ? 'fr-FR' : undefined;
  const dateText = p.createdAt ? new Date(p.createdAt).toLocaleDateString(locale) : '';
  meta.textContent = TR('card.meta', {
    author: p.author || TR('card.anon'),
    date: dateText,
    dl: p.downloads || 0,
  });
  el.appendChild(meta);

  if (Array.isArray(p.tags) && p.tags.length) {
    const tagsEl = document.createElement('div');
    tagsEl.className = 'card-tags';
    for (const rawTag of p.tags.slice(0, 3)) {
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
  const allPlugins = p.plugins || [];
  const pluginList = allPlugins.slice(0, 4);
  if (pluginList.length) {
    for (const n of pluginList) {
      const s = document.createElement('span');
      s.textContent = n;
      pl.appendChild(s);
    }
    if (allPlugins.length > 4) {
      const more = document.createElement('span');
      more.textContent = `+${allPlugins.length - 4}`;
      pl.appendChild(more);
    }
  } else {
    const s = document.createElement('span');
    s.textContent = TR('card.zeroPlugins');
    pl.appendChild(s);
  }
  el.appendChild(pl);

  const row = document.createElement('div');
  row.className = 'row';
  const copyBtn = document.createElement('button');
  copyBtn.type = 'button';
  copyBtn.textContent = TR('card.copy');
  copyBtn.onclick = async () => {
    await navigator.clipboard.writeText(p.code);
    copyBtn.textContent = TR('card.copied');
    setTimeout(() => { copyBtn.textContent = TR('card.copy'); }, 1600);
  };

  const dlSelect = document.createElement('select');
  dlSelect.className = 'dl-select';
  dlSelect.setAttribute('aria-label', TR('card.dlLabel'));
  const dlOptions = [
    { value: '', label: TR('card.dlPrompt') },
    { value: 'patchlink', label: 'PatchLink (.patchlink)' },
    { value: 'RfxChain', label: 'REAPER (.RfxChain)' },
    { value: 'adg', label: 'Ableton Live (.adg)' },
    { value: 'fst', label: 'FL Studio (.fst)' },
    { value: 'vstpreset', label: TR('card.dlVst3') },
  ];
  for (const opt of dlOptions) {
    const o = document.createElement('option');
    o.value = opt.value;
    o.textContent = opt.label;
    dlSelect.appendChild(o);
  }
  dlSelect.addEventListener('change', () => {
    const ext = dlSelect.value;
    if (!ext) return;
    dlSelect.selectedIndex = 0;
    window.location.href = ext === 'patchlink'
      ? `api/p/${p.code}/download`
      : `api/p/${p.code}/patchdrop-${p.code}.${ext}`;
  });

  row.append(copyBtn, dlSelect);
  el.append(row);
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
    mkTab('all', TR('cat.all'), allTotal);
    for (const key of CATEGORY_ORDER) {
      mkTab(key, catLabel(key), categories[key] || 0);
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
    fullGrid.innerHTML = `<div class="empty">${TR('dir.emptyLoad')}</div>`;
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
      if (state.category !== 'all') activeParts.push(TR('dir.catPrefix', { c: catLabel(state.category) }));
      if (state.tag) activeParts.push(`#${state.tag}`);
      if (state.q) activeParts.push(TR('dir.queryPrefix', { q: state.q }));
      statusEl.textContent = TR('dir.showing', { n: j.presets.length, total: j.allTotal ?? j.total })
        + (activeParts.length ? ' · ' + activeParts.join(' · ') : '');

      if (!j.presets.length) {
        fullGrid.innerHTML = `<div class="empty">${TR('dir.noMatch')}</div>`;
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
      statusEl.textContent = TR('dir.apiErr');
      fullGrid.innerHTML = `<div class="empty">${TR('dir.apiUnreach', { err: e.message })}</div>`;
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
    previewGrid.innerHTML = `<div class="empty">${TR('dir.loadingPreview')}</div>`;
    try {
      const j = await api('api/list?limit=3');

      if (previewCatsEl) {
        previewCatsEl.innerHTML = '';
        const allLink = document.createElement('a');
        allLink.className = 'cat-tab active';
        allLink.href = 'presets.html';
        allLink.append(document.createTextNode(TR('cat.all') + ' '));
        const allCnt = document.createElement('span');
        allCnt.className = 'cnt';
        allCnt.textContent = String(j.allTotal ?? j.total ?? 0);
        allLink.appendChild(allCnt);
        previewCatsEl.appendChild(allLink);

        for (const key of CATEGORY_ORDER) {
          const a = document.createElement('a');
          a.className = 'cat-tab';
          a.href = `presets.html?category=${encodeURIComponent(key)}`;
          a.append(document.createTextNode(catLabel(key) + ' '));
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
        previewCountEl.textContent = TR('dir.previewShowing', { n: j.presets.length, total });
      }

      if (!j.presets.length) {
        previewGrid.innerHTML = `<div class="empty">${TR('dir.none')}</div>`;
        return;
      }
      previewGrid.innerHTML = '';
      for (const p of j.presets) {
        previewGrid.appendChild(buildPresetCard(p, { isPreview: true }));
      }
    } catch (e) {
      previewGrid.innerHTML = `<div class="empty">${TR('dir.apiUnreach', { err: e.message })}</div>`;
    }
  };

  reloadActiveView = loadPreview;
  loadPreview();
}

// 3. Publish & Multi-DAW Browser Drag & Drop (dedicated publish.html + instant modal on all other pages)
(() => {
  let pubForm = document.getElementById('pubform');
  let modalBackdrop = null;

  if (!pubForm) {
    modalBackdrop = document.createElement('div');
    modalBackdrop.className = 'pub-modal-backdrop';
    modalBackdrop.innerHTML = `
      <div class="pub-modal" role="dialog" aria-modal="true" aria-label="Publish a chain">
        <div class="pub-modal-head">
          <h2 data-i18n="pub.modal.title">Publish a chain</h2>
          <button type="button" class="btn small ghost" id="pub-modal-close" data-i18n="pub.modal.close">Close ✕</button>
        </div>
        <form id="pubform" class="pub">
          <div id="drop-box" class="drop-zone-large">
            <strong data-i18n="pub.drop.strong">Drop your DAW chain file here</strong>
            <div id="drop-summary" class="drop-summary" data-i18n="pub.drop.summary">Supports .RfxChain, .adg, .fst, .vstpreset, or .patchlink</div>
            <input id="f-file" type="file" accept=".patchlink,.fst,.rfxchain,.RfxChain,.rpp,.RPP,.adg,.adv,.als,.vstpreset,.bwpreset,.fxchainpreset,.fxchain,.multipreset,.json,application/json">
          </div>
          <div class="pub-row">
            <label><span data-i18n="pub.lbl.title">Title</span><input id="f-title" maxlength="80" data-i18n-placeholder="pub.ph.title" placeholder="Lead vocal chain" required></label>
            <label><span data-i18n="pub.lbl.author">Author</span><input id="f-author" maxlength="40" placeholder="leve"></label>
          </div>
          <div class="pub-row">
            <label><span data-i18n="pub.lbl.category">Category</span>
              <select id="f-category">
                <option value="" data-i18n="pub.cat.auto">Auto-detect from title &amp; plugins</option>
                <option value="vocals" data-i18n="cat.vocals">Vocals</option>
                <option value="master" data-i18n="cat.master">Mix Bus &amp; Master</option>
                <option value="drums" data-i18n="cat.drums">Drums &amp; 808</option>
                <option value="synths" data-i18n="cat.synths">Synths &amp; Pads</option>
                <option value="inst" data-i18n="cat.inst">Guitars &amp; Keys</option>
                <option value="fx" data-i18n="cat.fx">FX &amp; Creative</option>
              </select>
            </label>
            <label><span data-i18n="pub.lbl.tags">Hashtags</span><input id="f-tags" maxlength="160" placeholder="#vocals #rap #warmth"></label>
          </div>
          <label><span data-i18n="pub.lbl.desc">Description</span><input id="f-desc" maxlength="500" data-i18n-placeholder="pub.ph.desc" placeholder="Pro-Q 3 -> CLA-76 -> Pro-DS (optional)"></label>
          <label class="check"><input id="f-public" type="checkbox" checked> <span data-i18n="pub.public">List publicly in Community Presets</span></label>
          <button class="btn big" type="submit" data-i18n="pub.submit">Upload and generate code</button>
          <div id="pubres"></div>
        </form>
      </div>`;
    document.body.appendChild(modalBackdrop);
    if (window.PD_I18N) window.PD_I18N.applyPage();
    pubForm = document.getElementById('pubform');

    const closeBtn = document.getElementById('pub-modal-close');
    if (closeBtn) closeBtn.addEventListener('click', () => modalBackdrop.classList.remove('open'));
    modalBackdrop.addEventListener('click', (ev) => {
      if (ev.target === modalBackdrop) modalBackdrop.classList.remove('open');
    });
    document.addEventListener('keydown', (ev) => {
      if (ev.key === 'Escape') modalBackdrop.classList.remove('open');
    });
  }

  const quickBanner = document.getElementById('quick-drop-banner');
  if (quickBanner) {
    quickBanner.addEventListener('click', (ev) => {
      if (!ev.target.closest('a')) window.location.href = 'publish.html';
    });
  }

  let stagedFilePatch = null;
  const fileInput = document.getElementById('f-file');
  const titleInput = document.getElementById('f-title');
  const dropBox = document.getElementById('drop-box');
  const dropSummary = document.getElementById('drop-summary');

  const veil = document.createElement('div');
  veil.className = 'page-drop-veil';
  veil.setAttribute('data-i18n', 'drop.veil');
  veil.textContent = TR('drop.veil');
  document.body.appendChild(veil);

  const setDropStatus = (msg) => {
    if (dropSummary) dropSummary.textContent = msg;
  };

  const setDragActive = (active) => {
    veil.classList.toggle('active', active);
    if (dropBox) dropBox.classList.toggle('dragover', active);
    if (quickBanner) quickBanner.classList.toggle('dragover', active);
  };

  const revealPublishUI = () => {
    if (modalBackdrop) {
      modalBackdrop.classList.add('open');
    } else {
      pubForm.scrollIntoView({ behavior: 'smooth', block: 'center' });
    }
  };

  const loadLocalPresetFile = async (f) => {
    if (!f) return;
    setDropStatus(TR('drop.parsing', { name: f.name }));
    try {
      const res = await parseAnyDawFileInBrowser(f);
      const patch = res.patch;
      const note = res.skipped && res.skipped.length ? TR('drop.skippedPrefix', { list: res.skipped.join(', ') }) : '';
      stagedFilePatch = patch;
      if (fileInput) fileInput.required = false;
      if (titleInput && !titleInput.value.trim()) {
        titleInput.value = (patch.name && patch.name !== 'Untitled chain') ? patch.name : f.name.replace(/\.[^/.]+$/, '');
      }
      const names = (patch.nodes || []).map((n) => (n.ids && n.ids.name) || n.name || '?').filter(Boolean);
      setDropStatus(TR('drop.loaded', { name: f.name, n: names.length, list: names.slice(0, 6).join(', '), note }));
    } catch (err) {
      stagedFilePatch = null;
      setDropStatus(TR('drop.errRead', { name: f.name, err: err.message }));
    }
  };

  if (fileInput) {
    fileInput.addEventListener('change', () => {
      if (fileInput.files && fileInput.files[0]) loadLocalPresetFile(fileInput.files[0]);
    });
  }

  const isFileDrag = (e) => {
    const dt = e.dataTransfer;
    if (!dt) return false;
    const types = Array.from(dt.types || []);
    if (types.includes('Files') || types.includes('application/x-moz-file')) return true;
    return Array.from(dt.items || []).some((it) => it.kind === 'file');
  };

  let dragDepth = 0;
  document.addEventListener('dragenter', (e) => {
    if (isFileDrag(e)) {
      e.preventDefault();
      e.stopPropagation();
      dragDepth++;
      setDragActive(true);
    }
  }, true);
  document.addEventListener('dragover', (e) => {
    if (isFileDrag(e)) {
      e.preventDefault();
      e.stopPropagation();
      try { e.dataTransfer.dropEffect = 'copy'; } catch {}
    }
  }, true);
  document.addEventListener('dragleave', () => {
    dragDepth = Math.max(0, dragDepth - 1);
    if (dragDepth === 0) setDragActive(false);
  }, true);
  document.addEventListener('drop', (e) => {
    dragDepth = 0;
    setDragActive(false);
    const files = e.dataTransfer && e.dataTransfer.files;
    if (isFileDrag(e) || (files && files.length)) {
      e.preventDefault();
      e.stopPropagation();
      revealPublishUI();
      if (files && files.length) {
        loadLocalPresetFile(files[0]);
      } else {
        setDropStatus(TR('drop.noReadable'));
      }
    }
  }, true);

  pubForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    const box = document.getElementById('pubres');
    const f = fileInput && fileInput.files[0];
    if (!stagedFilePatch && !f) { box.textContent = TR('drop.pickFirst'); return; }
    box.textContent = TR('pub.uploading');
    try {
      let patch = stagedFilePatch;
      if (!patch && f) {
        patch = (await parseAnyDawFileInBrowser(f)).patch;
      }
      const catEl = document.getElementById('f-category');
      const tagsEl = document.getElementById('f-tags');
      const body = {
        title: document.getElementById('f-title').value,
        author: document.getElementById('f-author').value || TR('card.anon'),
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
      box.innerHTML = TR('pub.readyHtml', {
        code: j.code,
        cat: catLabel(j.category || 'fx'),
        tags: tagsTxt ? ' · ' + tagsTxt : '',
      });
      if (reloadActiveView) reloadActiveView();
    } catch (err) {
      box.textContent = TR('pub.errPrefix', { err: err.message });
    }
  });
})();

if (window.PD_I18N) {
  window.PD_I18N.onChange(() => {
    if (reloadActiveView) reloadActiveView();
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
          note.innerHTML = TR('dl.missing');
        }
      }
    } catch { /* let the browser handle it */ }
  });
});
