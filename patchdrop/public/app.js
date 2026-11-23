// PatchDrop front: community presets explorer, multi-DAW browser drag-in/out, and publisher
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
      if (rec) {
        if (!pluginName && rec.flPlugin) pluginName = rec.flPlugin;
        if (!slotName && rec.flPlugin) slotName = rec.flPlugin;
        if (!vendor && rec.vendor) vendor = rec.vendor;
        if (!filePath && rec.file) filePath = rec.file;
        if (!reaperHash && rec.reaperHash) reaperHash = rec.reaperHash;
      }
    }

    const displayName = slotName || pluginName || (fuid ? `VST3 ${fuid.slice(0, 8)}` : `Plugin ${idx + 1}`);
    const ids = {
      name: displayName,
      manufacturer: vendor,
      version: '',
      format: e.isVst2 ? 'VST' : 'VST3',
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
          if (entry.pluginName || entry.slotName || entry.filePath) plugins.push(entry);
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

async function parseAbletonAdgInBrowser(arrayBuffer, fileName = 'Ableton Rack', tuidDb = null) {
  const xmlBytes = await gunzipIfNeededBytes(arrayBuffer);
  const xmlText = new TextDecoder('utf-8').decode(xmlBytes);
  const doc = new DOMParser().parseFromString(xmlText, 'text/xml');
  if (!doc || doc.querySelector('parsererror')) throw new Error('Invalid Ableton preset XML.');

  const groupNameEl = doc.querySelector('GroupDevicePreset > Device > AudioEffectGroupDevice > UserName');
  const rackName = (groupNameEl && groupNameEl.getAttribute('Value')) || fileName.replace(/\.[^/.]+$/, '');
  const plugins = [];
  const skipped = [];

  for (const pd of doc.querySelectorAll('PluginDevice')) {
    const manualEl = pd.querySelector('On > Manual');
    const bypass = manualEl ? manualEl.getAttribute('Value') === 'false' : false;
    const v3 = pd.querySelector('PluginDesc > Vst3PluginInfo');
    if (v3) {
      const nameEl = v3.querySelector(':scope > Name') || v3.querySelector(':scope > PlugName')
        || v3.querySelector('Vst3Preset > Name');
      const plugName = (nameEl && nameEl.getAttribute('Value')) || '';
      const uidEl = v3.querySelector(':scope > Uid') || v3.querySelector('Vst3Preset > Uid');
      let fuidHex = '';
      if (uidEl) {
        fuidHex = [0, 1, 2, 3].map((k) => {
          const fel = uidEl.querySelector(`Fields\\.${k}`);
          const v = fel ? (parseInt(fel.getAttribute('Value') || '0', 10) >>> 0) : 0;
          return v.toString(16).padStart(8, '0').toUpperCase();
        }).join('');
      }
      const vp = v3.querySelector('Preset > Vst3Preset');
      const psEl = vp && vp.querySelector('ProcessorState');
      const csEl = vp && vp.querySelector('ControllerState');
      const comp = psEl ? hexToBytes(psEl.textContent) : new Uint8Array(0);
      const cont = csEl ? hexToBytes(csEl.textContent) : new Uint8Array(0);
      plugins.push({
        slotName: plugName, pluginName: plugName, vendor: '', filePath: '',
        fuidHex, bypass, isVst2: false, stateBase64: packVst3ChunksToBase64(comp, cont),
      });
      continue;
    }
    const v2 = pd.querySelector('PluginDesc > VstPluginInfo');
    if (v2) {
      const ne = v2.querySelector('PlugName') || v2.querySelector('Name');
      skipped.push(((ne && ne.getAttribute('Value')) || 'VST2 Plugin') + ' (VST2)');
      continue;
    }
    const au = pd.querySelector('PluginDesc > AuPluginInfo');
    if (au) {
      const ne = au.querySelector('Name');
      skipped.push(((ne && ne.getAttribute('Value')) || 'Audio Unit') + ' (AU)');
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
  el.title = 'Step 1: Drag this card into PatchLink -> Step 2: Drag from PatchLink into your DAW';
  el.addEventListener('dragstart', (ev) => attachDawDragData(ev, p, 'fst'));

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
  row.append(copyBtn, dlBtn);

  const dawRow = document.createElement('div');
  dawRow.className = 'daw-row';
  const dawTargets = [
    { label: 'FL .fst', ext: 'fst', tip: 'FL Studio .fst — drag to PatchLink or click to download' },
    { label: 'REAPER', ext: 'RfxChain', tip: 'REAPER .RfxChain — drag to PatchLink or click to download' },
    { label: 'Ableton', ext: 'adg', tip: 'Ableton Live Rack .adg — drag to PatchLink or click to download' },
    { label: '.vstpreset', ext: 'vstpreset', tip: 'VST3 Preset (Cubase/Studio One/Bitwig) — drag to PatchLink or click to download' },
  ];
  for (const d of dawTargets) {
    const a = document.createElement('a');
    a.className = 'btn-daw';
    a.draggable = true;
    a.href = `api/p/${p.code}/patchdrop-${p.code}.${d.ext}`;
    a.setAttribute('download', `patchdrop-${p.code}.${d.ext}`);
    a.title = `Step 1: Drag onto PatchLink (Step 2: drag to your DAW) — or click to download ${d.tip}`;
    a.textContent = d.label;
    a.addEventListener('dragstart', (ev) => {
      ev.stopPropagation();
      attachDawDragData(ev, p, d.ext);
    });
    dawRow.appendChild(a);
  }

  el.append(row, dawRow);
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

// 3. Publish & Multi-DAW Browser Drag & Drop
const pubForm = document.getElementById('pubform');
if (pubForm) {
  let stagedFilePatch = null;
  const fileInput = document.getElementById('f-file');
  const titleInput = document.getElementById('f-title');
  const dropBox = document.getElementById('drop-box');
  const dropSummary = document.getElementById('drop-summary');

  const veil = document.createElement('div');
  veil.className = 'page-drop-veil';
  veil.textContent = 'Drop any DAW chain (.fst, .RfxChain, .adg, .vstpreset, .patchlink) to publish a code';
  document.body.appendChild(veil);

  const loadLocalPresetFile = async (f) => {
    if (!f) return;
    if (dropSummary) dropSummary.textContent = 'Parsing ' + f.name + '…';
    try {
      const res = await parseAnyDawFileInBrowser(f);
      const patch = res.patch;
      const note = res.skipped && res.skipped.length ? ` (skipped: ${res.skipped.join(', ')})` : '';
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
      if (/\.(fst|rfxchain|rpp|adg|adv|als|vstpreset|bwpreset|fxchainpreset|fxchain|multipreset|patchlink|json)$/i.test(f.name)) {
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
    if (!stagedFilePatch && !f) { box.textContent = 'Drop or pick any DAW chain (.fst, .RfxChain, .adg, .vstpreset) or .patchlink file.'; return; }
    box.textContent = 'Uploading…';
    try {
      let patch = stagedFilePatch;
      if (!patch && f) {
        patch = (await parseAnyDawFileInBrowser(f)).patch;
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
      box.innerHTML = `<div class="ok">Code ready: <b>${j.code}</b><br>Category: ${(CATEGORY_LABELS[j.category] || j.category || 'FX')}${tagsTxt ? ' · ' + tagsTxt : ''}<br>Drag this preset card into <b>PatchLink</b> (then drag from PatchLink into your DAW), or paste <b>${j.code}</b> in PatchLink → <i>Open code</i>.</div>`;
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
