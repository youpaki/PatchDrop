// PatchDrop front: explorer commu + publier un .patchlink -> code
async function api(path, opts) {
  const r = await fetch(path, opts);
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(j.error || ('HTTP ' + r.status));
  return j;
}
const grid = document.getElementById('grid');
async function loadList() {
  const q = document.getElementById('q').value.trim();
  grid.innerHTML = '<div class="empty">Chargement…</div>';
  try {
    const j = await api('api/list' + (q ? '?q=' + encodeURIComponent(q) : ''));
    // quand servi sous /patchdrop/, fetch relatif marche déjà. Rien à faire.
    if (!j.presets.length) { grid.innerHTML = '<div class="empty">Aucun preset. Sois le premier à publier 👇</div>'; return; }
    grid.innerHTML = '';
    for (const p of j.presets) {
      const el = document.createElement('div');
      el.className = 'card';
      el.innerHTML = `<div class="code">${p.code}</div><h3></h3>
        <div class="meta"></div><div class="plugins"></div>
        <div class="row"><button data-copy>Copier le code</button><button data-dl>Télécharger</button></div>`;
      el.querySelector('h3').textContent = p.title;
      el.querySelector('.meta').textContent = `par ${p.author} · ${new Date(p.createdAt).toLocaleDateString()} · ${p.downloads || 0} dl`;
      const pl = el.querySelector('.plugins');
      (p.plugins || []).slice(0, 6).forEach((n) => { const s = document.createElement('span'); s.textContent = n; pl.appendChild(s); });
      el.querySelector('[data-copy]').onclick = async () => {
        await navigator.clipboard.writeText(p.code);
        el.querySelector('[data-copy]').textContent = 'Copié ✓ colle dans PatchLink';
        setTimeout(() => (el.querySelector('[data-copy]').textContent = 'Copier le code'), 1600);
      };
      el.querySelector('[data-dl]').onclick = () => { window.location.href = `api/p/${p.code}/download`; };
      grid.appendChild(el);
    }
  } catch (e) { grid.innerHTML = `<div class="empty">API injoignable : ${e.message}</div>`; }
}
document.getElementById('qbtn').onclick = loadList;
document.getElementById('q').addEventListener('keydown', (e) => { if (e.key === 'Enter') loadList(); });

// Publier : lit le .patchlink en local, POST /api/publish
document.getElementById('pubform').addEventListener('submit', async (e) => {
  e.preventDefault();
  const box = document.getElementById('pubres');
  const f = document.getElementById('f-file').files[0];
  if (!f) { box.innerHTML = 'Choisis un fichier .patchlink.'; return; }
  box.innerHTML = 'Envoi…';
  try {
    const patch = JSON.parse(await f.text());
    const body = {
      title: document.getElementById('f-title').value,
      author: document.getElementById('f-author').value || 'Anonyme',
      description: document.getElementById('f-desc').value,
      isPublic: document.getElementById('f-public').checked,
      patch,
    };
    const j = await api('api/publish', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
    box.innerHTML = `<div class="ok">Code prêt : <b>${j.code}</b><br>Envoie ce code à ton pote — il le colle dans PatchLink → <i>Ouvrir via code</i>.</div>`;
    loadList();
  } catch (err) { box.innerHTML = 'Erreur : ' + err.message; }
});

// liens de download : si les zips n'existent pas encore, message propre
document.querySelectorAll('[data-dl]').forEach((a) => {
  a.addEventListener('click', async (ev) => {
    try {
      const r = await fetch(a.getAttribute('href'), { method: 'HEAD' });
      if (!r.ok) {
        ev.preventDefault();
        document.getElementById('dl-note').innerHTML =
          'Les installeurs arrivent (build Release en cours). En attendant : compile via <span class="mono">patchlink/README.md</span> ou partage déjà des <span class="mono">.patchlink</span> avec un code.';
      }
    } catch { /* laisse le navigateur gérer */ }
  });
});
loadList();
