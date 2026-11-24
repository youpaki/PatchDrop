// PatchDrop EN / FR client-side localization
(() => {
  const STORAGE_KEY = 'patchdrop_lang';
  const listeners = [];

  const STRINGS = {
    en: {
      // Navigation & Footer
      'nav.overview': 'Overview',
      'nav.guide': 'How it works',
      'nav.presets': 'Presets',
      'nav.publish': 'Publish',
      'footer.top': 'Top',
      'footer.credit': 'PATCHDROP · PatchLink v1.6.3 by leve · ',

      // Home Hero
      'hero.eyebrow': 'VST3 + Standalone Chainer · Multi-DAW Converter',
      'hero.title': 'Pack your FX rack into 6 characters.',
      'hero.lede': 'Share complete VST3 effect chains with a short code like <b class="code-inline">K7Q2XA</b>, or drag racks effortlessly between your DAWs.',
      'hero.dl': 'Download PatchLink 1.6.3',
      'hero.browse': 'Browse presets',
      'hero.spec': 'Windows 64-bit VST3 + Standalone · Website → PatchLink → DAW',
      'hero.rack.foot1': '4/4 VST3 blobs packed',
      'hero.rack.foot2': 'AUTO DAW EXPORT',

      // DAWs Section
      'daws.h2': 'Works across your DAWs',
      'daws.sub': 'Import, share, and unpack third-party VST3 chains natively — even in Zero-Load mode without opening the plugins.',

      // Workflow Summary
      'wf.h2': 'Three steps, zero rebuilds',
      'wf.sub': 'PatchLink bridges web presets and desktop DAWs cleanly.',
      'wf.btn': 'Visual workflow & FAQ →',
      'wf.s1.tag': 'STEP 01',
      'wf.s1.title': 'Website → PatchLink',
      'wf.s1.desc': 'Drag any preset card from this site into <b>PatchLink</b>, or paste its 6-character code.',
      'wf.s2.tag': 'STEP 02',
      'wf.s2.title': 'PatchLink → Your DAW',
      'wf.s2.desc': 'Drag PatchLink\'s handle onto any mixer track to unpack native plugin slots automatically.',
      'wf.s3.tag': 'STEP 03',
      'wf.s3.title': 'Share in 6 chars',
      'wf.s3.desc': 'Click <i>Share</i> in PatchLink or drop a DAW preset file on <a href="publish.html">Publish</a> to get a code.',

      // Explorer Preview (Home)
      'exp.h2': 'Community presets',
      'exp.sub': 'Drag any card into <b>PatchLink</b>, then drag from <b>PatchLink</b> into your DAW.',
      'exp.all': 'Open full directory →',
      'exp.more': 'Explore all presets & #tags →',

      // Quick Drop Banner (Home)
      'banner.h3': 'Have a mixer chain ready to share?',
      'banner.p': 'Drop any <span class="mono">.fst</span>, <span class="mono">.RfxChain</span>, <span class="mono">.adg</span>, <span class="mono">.vstpreset</span>, or <span class="mono">.patchlink</span> file anywhere on this window — or open the uploader.',
      'banner.btn': 'Publish a chain →',

      // Download (Home)
      'dl.h2': 'Download',
      'dl.sub': 'Install PatchLink once, choose your VST3 folders at first launch, and trade chains across DAWs.',
      'dl.win.tag': 'Windows 64-bit · v1.6.3',
      'dl.win.title': 'Windows Installer',
      'dl.win.desc': 'Includes both the VST3 plugin (<span class="mono">C:\\Program Files\\Common Files\\VST3\\</span>) and the Standalone converter.',
      'dl.mac.tag': 'macOS · Source / CMake',
      'dl.mac.title': 'macOS (VST3 + AU)',
      'dl.mac.desc': 'JUCE 8 CMake repository supporting VST3 and Audio Unit hosting for macOS and Windows.',
      'dl.mac.btn': 'View on GitHub',
      'dl.note': 'Current official Windows release: v1.6.3 (EN / FR) · Source &amp; release notes on <a href="https://github.com/youpaki/PatchDrop">youpaki/PatchDrop</a>.',
      'dl.missing': 'That file is not published yet. Source + build instructions: <a href="https://github.com/youpaki/PatchDrop">youpaki/PatchDrop</a>.',

      // Guide Page
      'guide.eyebrow': 'PATCHDROP // WORKFLOW & FAQ',
      'guide.h1': 'How PatchLink works',
      'guide.sub': 'Translate third-party VST3 chains across DAWs without rebuilding racks by hand.',
      'guide.c1.tag': 'Direction 1 · Any DAW → PatchLink',
      'guide.c1.h3': 'Import & Share Racks',
      'guide.c1.p1': 'Drop a saved mixer chain or rack file from your DAW or Explorer straight into PatchLink:',
      'guide.c1.arrow': 'drop →',
      'guide.c1.r0': 'Zero-Load or Live',
      'guide.c1.r1': 'Share 6-char code',
      'guide.c1.r2': 'Convert to another DAW',
      'guide.c1.r3': 'Or host live in PatchLink',
      'guide.c1.p2': '<b>Zero-Load mode</b> lets you share a code or convert formats without instantiating plugins first.',
      'guide.c2.tag': 'Direction 2 · Website → PatchLink → DAW',
      'guide.c2.h3': 'Two-Step Drag into Your Mix',
      'guide.c2.p1': 'Browsers can\'t drop native files straight onto DAW tracks — PatchLink bridges the drop in two quick moves:',
      'guide.c2.m1': '1. Web card → PatchLink',
      'guide.c2.m2': '2. PatchLink → Mixer track',
      'guide.c2.m3': 'Auto-detects host DAW',
      'guide.c2.arrow': 'unpacks →',
      'guide.c2.r0': 'Native DAW Slots',
      'guide.c2.r3': '03 Preserves states + bypass',
      'guide.c2.p2': 'PatchLink auto-detects REAPER, Ableton Live, FL Studio, Cubase, Studio One, and Bitwig (or asks in Standalone).',
      'guide.daws.h2': 'Supported DAW formats',
      'guide.daws.sub': 'Every VST3 plugin state blob is preserved across conversions.',
      'faq.h2': 'Technical FAQ',
      'faq.q1': 'Why drag Website → PatchLink → DAW instead of straight into my DAW?',
      'faq.a1': 'Web browsers cannot drop native binary files directly onto desktop DAW mixer tracks. PatchLink accepts drops from your browser (preset cards or 6-character codes) and writes the native DAW chain file on disk when you drag from PatchLink\'s toolbar into your DAW.',
      'faq.q2': 'Which DAW stock plugins don\'t transfer across DAWs?',
      'faq.a2': 'Host-exclusive stock effects (Ableton EQ Eight, REAPER JSFX, FL WaveShaper, etc.) only exist inside their own DAW. PatchLink transfers all third-party VST3 plugins with exact binary states and bypass flags, and skips host-only slots cleanly.',
      'faq.q3': 'What if I don\'t own one of the plugins in a shared chain?',
      'faq.a3': 'Installed plugins load with their saved preset state; missing plugins are listed clearly without crashing your DAW. In Zero-Load mode, you can even convert or re-share a chain without owning any of its plugins.',
      'faq.q4': 'My plugin list inside PatchLink is empty after installing',
      'faq.a4': 'On first launch PatchLink asks you to pick your VST3 folders and scan. You can add custom paths anytime from <b>Library</b> → <i>Add folder</i> → <i>Scan selected</i>.',

      // Presets Directory Page
      'dir.eyebrow': 'PATCHDROP // COMMUNITY DIRECTORY',
      'dir.h1': 'Community presets',
      'dir.sub': 'Drag any card into <b>PatchLink</b>, then drag from <b>PatchLink</b> into your DAW — or copy its 6-character code.',
      'dir.pubbtn': '+ Publish a chain',
      'dir.search.ph': 'Search title, plugin, author, or #tag…',
      'dir.sort.newest': 'Newest first',
      'dir.sort.popular': 'Most downloaded',
      'dir.filter': 'Filter',
      'dir.clear': 'Clear',
      'dir.loading': 'Loading directory…',
      'dir.loadingPreview': 'Loading preview…',
      'dir.emptyLoad': 'Loading presets…',

      // Publish Page & Modal
      'pub.eyebrow': 'PATCHDROP // UPLOAD & SHARE',
      'pub.h1': 'Publish a chain',
      'pub.sub': 'Drop a preset file from your DAW or Explorer below to generate a 6-character share code.',
      'pub.modal.title': 'Publish a chain',
      'pub.modal.close': 'Close ✕',
      'pub.drop.strong': 'Drop your DAW chain file anywhere here',
      'pub.drop.summary': 'Supports .RfxChain, .rpp, .adg, .adv, .fst, .vstpreset, or .patchlink',
      'pub.lbl.title': 'Title',
      'pub.ph.title': 'Lead vocal chain',
      'pub.lbl.author': 'Author',
      'pub.lbl.category': 'Category',
      'pub.cat.auto': 'Auto-detect from title & plugins',
      'pub.lbl.tags': 'Hashtags',
      'pub.lbl.desc': 'Description',
      'pub.ph.desc': 'Pro-Q 3 -> CLA-76 -> Pro-DS (optional)',
      'pub.public': 'List publicly in Community Presets',
      'pub.submit': 'Upload and generate code',

      // Categories
      'cat.all': 'All',
      'cat.vocals': 'Vocals',
      'cat.master': 'Mix Bus & Master',
      'cat.drums': 'Drums & 808',
      'cat.synths': 'Synths & Pads',
      'cat.inst': 'Guitars & Keys',
      'cat.fx': 'FX & Creative',

      // Dynamic JS UI strings
      'card.dragTip': 'Step 1: Drag this card into PatchLink -> Step 2: Drag from PatchLink into your DAW',
      'card.untitled': 'Untitled preset',
      'card.anon': 'Anonymous',
      'card.meta': 'by {author} · {date} · {dl} dl',
      'card.zeroPlugins': '0 plugins',
      'card.copy': 'Copy code',
      'card.copied': 'Copied ✓',
      'card.dlLabel': 'Download format',
      'card.dlPrompt': 'Download ▾',
      'card.dlVst3': 'Cubase / Studio One (.vstpreset)',
      'dir.showing': 'Showing {n} of {total} presets',
      'dir.previewShowing': 'Showing latest {n} of {total} public presets',
      'dir.catPrefix': 'Category: {c}',
      'dir.queryPrefix': 'Query: "{q}"',
      'dir.noMatch': 'No presets match the active filters.',
      'dir.none': 'No presets found.',
      'dir.apiErr': 'API error',
      'dir.apiUnreach': 'API unreachable: {err}',
      'drop.veil': 'Drop any DAW chain (.fst, .RfxChain, .adg, .vstpreset, .patchlink) to publish a code',
      'drop.parsing': 'Parsing {name}…',
      'drop.loaded': 'Loaded {name}: {n} plugin(s) [{list}]{note}',
      'drop.skippedPrefix': ' (skipped: {list})',
      'drop.errRead': 'Error reading {name}: {err}',
      'drop.noReadable': 'Browser did not receive a readable file from that drag — drop it onto PatchLink or click Choose File below.',
      'drop.pickFirst': 'Drop or pick any DAW chain (.fst, .RfxChain, .adg, .vstpreset) or .patchlink file.',
      'pub.uploading': 'Uploading…',
      'pub.readyHtml': '<div class="ok">Code ready: <b>{code}</b><br>Category: {cat}{tags}<br>Drag this preset card into <b>PatchLink</b> (then drag from PatchLink into your DAW), or paste <b>{code}</b> in PatchLink → <i>Open code</i>.</div>',
      'pub.errPrefix': 'Error: {err}',
    },
    fr: {
      // Navigation & Footer
      'nav.overview': 'Accueil',
      'nav.guide': 'Fonctionnement',
      'nav.presets': 'Presets',
      'nav.publish': 'Publier',
      'footer.top': 'Haut de page',
      'footer.credit': 'PATCHDROP · PatchLink v1.6.3 par leve · ',

      // Home Hero
      'hero.eyebrow': 'Chainer VST3 + Autonome · Convertisseur Multi-DAW',
      'hero.title': 'Emballez votre chaîne FX en 6 caractères.',
      'hero.lede': 'Partagez des chaînes d\'effets VST3 complètes avec un code comme <b class="code-inline">K7Q2XA</b>, ou transférez vos racks facilement entre DAW.',
      'hero.dl': 'Télécharger PatchLink 1.6.3',
      'hero.browse': 'Parcourir les presets',
      'hero.spec': 'Windows 64-bit VST3 + Autonome · Site Web → PatchLink → DAW',
      'hero.rack.foot1': '4/4 états VST3 inclus',
      'hero.rack.foot2': 'EXPORT DAW AUTO',

      // DAWs Section
      'daws.h2': 'Compatible avec vos DAWs',
      'daws.sub': 'Importez, partagez et convertissez vos chaînes VST3 tierces — même en mode Sans-chargement sans ouvrir les plugins.',

      // Workflow Summary
      'wf.h2': 'Trois étapes, zéro reconstruction',
      'wf.sub': 'PatchLink fait le pont entre le web et vos DAWs.',
      'wf.btn': 'Schéma & FAQ →',
      'wf.s1.tag': 'ÉTAPE 01',
      'wf.s1.title': 'Site Web → PatchLink',
      'wf.s1.desc': 'Glissez une carte preset de ce site vers <b>PatchLink</b>, ou collez son code à 6 caractères.',
      'wf.s2.tag': 'ÉTAPE 02',
      'wf.s2.title': 'PatchLink → Votre DAW',
      'wf.s2.desc': 'Glissez le bouton de PatchLink sur une piste du mixeur pour charger les slots nativement.',
      'wf.s3.tag': 'ÉTAPE 03',
      'wf.s3.title': 'Partage en 6 car.',
      'wf.s3.desc': 'Cliquez sur <i>Partager</i> dans PatchLink ou déposez un fichier DAW sur <a href="publish.html">Publier</a>.',

      // Explorer Preview (Home)
      'exp.h2': 'Presets de la communauté',
      'exp.sub': 'Glissez une carte dans <b>PatchLink</b>, puis glissez de <b>PatchLink</b> vers votre DAW.',
      'exp.all': 'Voir tout l\'annuaire →',
      'exp.more': 'Explorer tous les presets & #tags →',

      // Quick Drop Banner (Home)
      'banner.h3': 'Une chaîne de mix prête à partager ?',
      'banner.p': 'Déposez un fichier <span class="mono">.fst</span>, <span class="mono">.RfxChain</span>, <span class="mono">.adg</span>, <span class="mono">.vstpreset</span> ou <span class="mono">.patchlink</span> n\'importe où sur cette fenêtre.',
      'banner.btn': 'Publier une chaîne →',

      // Download (Home)
      'dl.h2': 'Téléchargement',
      'dl.sub': 'Installez PatchLink, choisissez vos dossiers VST3 au premier lancement, et échangez vos chaînes entre DAWs.',
      'dl.win.tag': 'Windows 64-bit · v1.6.3',
      'dl.win.title': 'Installateur Windows',
      'dl.win.desc': 'Inclut le plugin VST3 (<span class="mono">C:\\Program Files\\Common Files\\VST3\\</span>) et l\'application autonome.',
      'dl.mac.tag': 'macOS · Sources / CMake',
      'dl.mac.title': 'macOS (VST3 + AU)',
      'dl.mac.desc': 'Dépôt CMake JUCE 8 avec support VST3 et Audio Unit pour macOS et Windows.',
      'dl.mac.btn': 'Voir sur GitHub',
      'dl.note': 'Version Windows officielle actuelle : v1.6.3 (FR / EN) · Sources sur <a href="https://github.com/youpaki/PatchDrop">youpaki/PatchDrop</a>.',
      'dl.missing': 'Ce fichier n\'est pas encore publié. Sources : <a href="https://github.com/youpaki/PatchDrop">youpaki/PatchDrop</a>.',

      // Guide Page
      'guide.eyebrow': 'PATCHDROP // FONCTIONNEMENT & FAQ',
      'guide.h1': 'Comment fonctionne PatchLink',
      'guide.sub': 'Traduisez vos chaînes VST3 tierces entre DAWs sans tout recâbler à la main.',
      'guide.c1.tag': 'Sens 1 · N\'importe quel DAW → PatchLink',
      'guide.c1.h3': 'Importer & Partager des Racks',
      'guide.c1.p1': 'Glissez une chaîne mixeur ou un rack sauvegardé depuis votre DAW ou l\'Explorateur dans PatchLink :',
      'guide.c1.arrow': 'déposer →',
      'guide.c1.r0': 'Sans-chargement ou Direct',
      'guide.c1.r1': 'Créer un code à 6 car.',
      'guide.c1.r2': 'Convertir vers un autre DAW',
      'guide.c1.r3': 'Ou héberger dans PatchLink',
      'guide.c1.p2': 'Le mode <b>Sans-chargement</b> permet de partager ou convertir un preset sans instancier les plugins.',
      'guide.c2.tag': 'Sens 2 · Site Web → PatchLink → DAW',
      'guide.c2.h3': 'Glisser-Déposer en deux étapes',
      'guide.c2.p1': 'Les navigateurs ne peuvent pas déposer de fichiers natifs directement dans un DAW — PatchLink sert de passerelle :',
      'guide.c2.m1': '1. Carte Web → PatchLink',
      'guide.c2.m2': '2. PatchLink → Piste Mixeur',
      'guide.c2.m3': 'Détection auto du DAW',
      'guide.c2.arrow': 'convertit →',
      'guide.c2.r0': 'Slots Natifs du DAW',
      'guide.c2.r3': '03 Conserve états + bypass',
      'guide.c2.p2': 'PatchLink détecte automatiquement REAPER, Ableton Live, FL Studio, Cubase, Studio One et Bitwig (ou demande en autonome).',
      'guide.daws.h2': 'Formats DAW supportés',
      'guide.daws.sub': 'Chaque état binaire VST3 est préservé lors des conversions.',
      'faq.h2': 'FAQ Technique',
      'faq.q1': 'Pourquoi passer par Site Web → PatchLink → DAW ?',
      'faq.a1': 'Un navigateur web ne peut pas déposer directement de vrais fichiers binaires sur une piste d\'un logiciel bureau. PatchLink reçoit la carte ou le code depuis le navigateur, puis écrit le vrai fichier natif pour votre DAW au glisser-déposer.',
      'faq.q2': 'Quels plugins natifs DAW ne se transfèrent pas ?',
      'faq.a2': 'Les effets internes propres à un DAW (Ableton EQ Eight, REAPER JSFX, FL WaveShaper, etc.) n\'existent que dans leur hôte d\'origine. PatchLink transfère tous vos VST3 tiers avec leurs réglages exacts et ignore proprement les effets internes.',
      'faq.q3': 'Et si je ne possède pas l\'un des plugins d\'une chaîne partagée ?',
      'faq.a3': 'Les plugins installés chargent leurs réglages exacts ; les plugins manquants sont listés sans faire planter le DAW. En mode Sans-chargement, vous pouvez même convertir ou repartager une chaîne sans posséder aucun des plugins.',
      'faq.q4': 'Ma liste de plugins dans PatchLink est vide après installation',
      'faq.a4': 'Au premier lancement, PatchLink vous propose de choisir vos dossiers VST3 et de scanner. Vous pouvez aussi ajouter un dossier personnalisé via <b>Dossiers</b> → <i>Ajouter dossier</i> → <i>Scanner</i>.',

      // Presets Directory Page
      'dir.eyebrow': 'PATCHDROP // ANNUAIRE COMMUNAUTAIRE',
      'dir.h1': 'Presets de la communauté',
      'dir.sub': 'Glissez une carte dans <b>PatchLink</b>, puis glissez de <b>PatchLink</b> vers votre DAW — ou copiez le code à 6 caractères.',
      'dir.pubbtn': '+ Publier une chaîne',
      'dir.search.ph': 'Rechercher titre, plugin, auteur ou #tag…',
      'dir.sort.newest': 'Plus récents',
      'dir.sort.popular': 'Plus téléchargés',
      'dir.filter': 'Filtrer',
      'dir.clear': 'Effacer',
      'dir.loading': 'Chargement de l\'annuaire…',
      'dir.loadingPreview': 'Chargement de l\'aperçu…',
      'dir.emptyLoad': 'Chargement des presets…',

      // Publish Page & Modal
      'pub.eyebrow': 'PATCHDROP // ENVOI & PARTAGE',
      'pub.h1': 'Publier une chaîne',
      'pub.sub': 'Déposez un fichier preset de votre DAW ou de l\'Explorateur ci-dessous pour générer un code à 6 caractères.',
      'pub.modal.title': 'Publier une chaîne',
      'pub.modal.close': 'Fermer ✕',
      'pub.drop.strong': 'Déposez votre fichier chaîne DAW ici',
      'pub.drop.summary': 'Supporte .RfxChain, .rpp, .adg, .adv, .fst, .vstpreset ou .patchlink',
      'pub.lbl.title': 'Titre',
      'pub.ph.title': 'Chaîne voix lead',
      'pub.lbl.author': 'Auteur',
      'pub.lbl.category': 'Catégorie',
      'pub.cat.auto': 'Détection auto depuis titre & plugins',
      'pub.lbl.tags': 'Hashtags',
      'pub.lbl.desc': 'Description',
      'pub.ph.desc': 'Pro-Q 3 -> CLA-76 -> Pro-DS (optionnel)',
      'pub.public': 'Afficher publiquement dans les Presets',
      'pub.submit': 'Envoyer et générer le code',

      // Categories
      'cat.all': 'Tous',
      'cat.vocals': 'Voix',
      'cat.master': 'Mix Bus & Master',
      'cat.drums': 'Drums & 808',
      'cat.synths': 'Synthés & Pads',
      'cat.inst': 'Guitares & Claviers',
      'cat.fx': 'FX & Créatif',

      // Dynamic JS UI strings
      'card.dragTip': 'Étape 1 : Glissez cette carte dans PatchLink -> Étape 2 : Glissez de PatchLink vers votre DAW',
      'card.untitled': 'Preset sans titre',
      'card.anon': 'Anonyme',
      'card.meta': 'par {author} · {date} · {dl} dl',
      'card.zeroPlugins': '0 plugin',
      'card.copy': 'Copier code',
      'card.copied': 'Copié ✓',
      'card.dlLabel': 'Format de téléchargement',
      'card.dlPrompt': 'Télécharger ▾',
      'card.dlVst3': 'Cubase / Studio One (.vstpreset)',
      'dir.showing': '{n} sur {total} presets affichés',
      'dir.previewShowing': '{n} derniers sur {total} presets publics',
      'dir.catPrefix': 'Catégorie : {c}',
      'dir.queryPrefix': 'Recherche : « {q} »',
      'dir.noMatch': 'Aucun preset ne correspond aux filtres.',
      'dir.none': 'Aucun preset trouvé.',
      'dir.apiErr': 'Erreur API',
      'dir.apiUnreach': 'API inaccessible : {err}',
      'drop.veil': 'Déposez un fichier DAW (.fst, .RfxChain, .adg, .vstpreset, .patchlink) pour créer un code',
      'drop.parsing': 'Analyse de {name}…',
      'drop.loaded': '{name} chargé : {n} plugin(s) [{list}]{note}',
      'drop.skippedPrefix': ' (ignorés : {list})',
      'drop.errRead': 'Erreur lors de la lecture de {name} : {err}',
      'drop.noReadable': 'Le navigateur n\'a pas reçu de fichier lisible depuis ce glisser-déposer — déposez dans PatchLink ou cliquez ci-dessous.',
      'drop.pickFirst': 'Déposez ou choisissez un fichier DAW (.fst, .RfxChain, .adg, .vstpreset) ou .patchlink.',
      'pub.uploading': 'Envoi en cours…',
      'pub.readyHtml': '<div class="ok">Code prêt : <b>{code}</b><br>Catégorie : {cat}{tags}<br>Glissez cette carte dans <b>PatchLink</b> (puis de PatchLink vers votre DAW), ou collez <b>{code}</b> dans PatchLink → <i>Ouvrir code</i>.</div>',
      'pub.errPrefix': 'Erreur : {err}',
    },
  };

  function detectDefaultLang() {
    try {
      const saved = localStorage.getItem(STORAGE_KEY);
      if (saved === 'fr' || saved === 'en') return saved;
    } catch {}
    const nav = (navigator.language || (navigator.languages && navigator.languages[0]) || 'en').toLowerCase();
    return nav.startsWith('fr') ? 'fr' : 'en';
  }

  let current = detectDefaultLang();

  function t(key, vars = null) {
    const dict = STRINGS[current] || STRINGS.en;
    let s = (dict[key] !== undefined ? dict[key] : STRINGS.en[key]) || key;
    if (vars) {
      for (const [k, v] of Object.entries(vars)) {
        s = s.split('{' + k + '}').join(String(v == null ? '' : v));
      }
    }
    return s;
  }

  function applyPage() {
    document.documentElement.lang = current;
    document.querySelectorAll('[data-i18n]').forEach((el) => {
      const k = el.getAttribute('data-i18n');
      if (k) el.textContent = t(k);
    });
    document.querySelectorAll('[data-i18n-html]').forEach((el) => {
      const k = el.getAttribute('data-i18n-html');
      if (k) el.innerHTML = t(k);
    });
    document.querySelectorAll('[data-i18n-placeholder]').forEach((el) => {
      const k = el.getAttribute('data-i18n-placeholder');
      if (k) el.setAttribute('placeholder', t(k));
    });
    document.querySelectorAll('[data-i18n-title]').forEach((el) => {
      const k = el.getAttribute('data-i18n-title');
      if (k) el.setAttribute('title', t(k));
    });
    document.querySelectorAll('.lang-switch button[data-lang]').forEach((btn) => {
      const isCur = btn.getAttribute('data-lang') === current;
      btn.classList.toggle('active', isCur);
      btn.setAttribute('aria-pressed', isCur ? 'true' : 'false');
    });
  }

  function setLang(lang) {
    const next = lang === 'fr' ? 'fr' : 'en';
    if (next === current) return;
    current = next;
    try { localStorage.setItem(STORAGE_KEY, current); } catch {}
    applyPage();
    for (const fn of listeners) {
      try { fn(current); } catch {}
    }
  }

  function initSwitcher() {
    document.querySelectorAll('.lang-switch button[data-lang]').forEach((btn) => {
      btn.addEventListener('click', () => setLang(btn.getAttribute('data-lang')));
    });
    applyPage();
  }

  window.PD_I18N = {
    getLang: () => current,
    setLang,
    t,
    applyPage,
    onChange: (fn) => { if (typeof fn === 'function') listeners.push(fn); },
  };

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initSwitcher);
  } else {
    initSwitcher();
  }
})();
