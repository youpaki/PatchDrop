// PatchDrop front: community explorer + publish a .patchlink -> code
async function api(path, opts) {
  const r = await fetch(path, opts);
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(j.error || ('HTTP ' + r.status));
  return j;
}
const grid = document.getElementById('grid');
async function loadList() {
  const q = document.getElementById('q').value.trim();
  grid.innerHTML = '<div class="empty">Loading…</div>';
  try {
    const j = await api('api/list' + (q ? '?q=' + encodeURIComponent(q) : ''));
    if (!j.presets.length) { grid.innerHTML = '<div class="empty">No presets found.</div>'; return; }
    grid.innerHTML = '';
    for (const p of j.presets) {
      const el = document.createElement('div');
      el.className = 'card';
      el.innerHTML = `<div class="code">${p.code}</div><h3></h3>
        <div class="meta"></div><div class="plugins"></div>
        <div class="row"><button data-copy>Copy code</button><button data-dl>Download</button></div>`;
      el.querySelector('h3').textContent = p.title;
      el.querySelector('.meta').textContent = `by ${p.author} · ${new Date(p.createdAt).toLocaleDateString()} · ${p.downloads || 0} dl`;
      const pl = el.querySelector('.plugins');
      (p.plugins || []).slice(0, 6).forEach((n) => { const s = document.createElement('span'); s.textContent = n; pl.appendChild(s); });
      el.querySelector('[data-copy]').onclick = async () => {
        await navigator.clipboard.writeText(p.code);
        el.querySelector('[data-copy]').textContent = 'Copied ✓ paste it in PatchLink';
        setTimeout(() => (el.querySelector('[data-copy]').textContent = 'Copy code'), 1600);
      };
      el.querySelector('[data-dl]').onclick = () => { window.location.href = `api/p/${p.code}/download`; };
      grid.appendChild(el);
    }
  } catch (e) { grid.innerHTML = `<div class="empty">API unreachable: ${e.message}</div>`; }
}
document.getElementById('qbtn').onclick = loadList;
document.getElementById('q').addEventListener('keydown', (e) => { if (e.key === 'Enter') loadList(); });

// Publish: read the .patchlink locally, POST api/publish
document.getElementById('pubform').addEventListener('submit', async (e) => {
  e.preventDefault();
  const box = document.getElementById('pubres');
  const f = document.getElementById('f-file').files[0];
  if (!f) { box.innerHTML = 'Pick a .patchlink file.'; return; }
  box.innerHTML = 'Uploading…';
  try {
    const patch = JSON.parse(await f.text());
    const body = {
      title: document.getElementById('f-title').value,
      author: document.getElementById('f-author').value || 'Anonymous',
      description: document.getElementById('f-desc').value,
      isPublic: document.getElementById('f-public').checked,
      patch,
    };
    const j = await api('api/publish', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
    box.innerHTML = `<div class="ok">Code ready: <b>${j.code}</b><br>Send this code to your collaborator — they paste it in PatchLink → <i>Open code</i>.</div>`;
    loadList();
  } catch (err) { box.innerHTML = 'Error: ' + err.message; }
});

// download links: if the file is missing, show a clean message instead
document.querySelectorAll('[data-dl]').forEach((a) => {
  a.addEventListener('click', async (ev) => {
    try {
      const r = await fetch(a.getAttribute('href'), { method: 'HEAD' });
      if (!r.ok) {
        ev.preventDefault();
        document.getElementById('dl-note').innerHTML =
          'That file is not published yet. Source + build instructions: <a href="https://github.com/youpaki/PatchDrop">youpaki/PatchDrop</a>.';
      }
    } catch { /* let the browser handle it */ }
  });
});
loadList();
