# PatchLink — Chainer VST3 + Standalone type Patcher avec partage de patch

## Concept (ton besoin)
Tu crées une chaîne d'effets avec des plugins tiers chez toi (FL Studio, Windows).
Tu exportes un fichier `.patchlink`.
Ton pote l'ouvre dans sa propre instance (Ableton, chez lui) et retrouve la même chaîne,
les mêmes réglages, avec un rapport "plugins manquants" si besoin.
Il peut s'enregistrer avec tes effets en monitoring direct.

## Choix techniques imposés par tes réponses
- **Format cible : VST3 + Standalone** → JUCE 8 fait les deux depuis le même code.
- **Hébergement : VST3 + AU + CLAP**
  - Windows : VST3 + CLAP (AU = macOS uniquement, impossible sur Windows)
  - macOS : VST3 + AU + CLAP
  - JUCE hoste nativement VST3 et AU. CLAP demande une extension (voir TODO plus bas).
- **Partage : fichier + détection manquants** → format JSON + états binaires base64.
- **Routage : nodal complet** → `juce::AudioProcessorGraph` en moteur audio.

## État de ce scaffold
V1 fonctionnelle minimale :
- [x] Plugin VST3 + Standalone (CMake + JUCE via FetchContent)
- [x] Moteur `AudioProcessorGraph` : in -> nodes -> out
- [x] Scan VST3 / AU, liste, instanciation
- [x] Sérialisation patch `.patchlink` (JSON) avec état complet de chaque plugin
- [x] Détection plugins manquants à l'import
- [x] Editeur nodal minimal (ajouter / supprimer / bypass / câblage auto série)
- [ ] Vrai éditeur nodal drag-cable type Patcher (V2)
- [ ] Hosting CLAP (V2, via `clap-helpers` ou `carla`)
- [ ] Parallèle / split / dry-wet par branche (V2, le graph le permet déjà)
- [ ] Compensation latence PDC complète (V2)

## Compiler

### Windows (FL + Ableton)
1. Installer Visual Studio 2022 (Desktop C++), CMake 3.22+, Git.
2. ```powershell
   cd patchlink
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release --parallel
   ```
3. Le VST3 est dans `build/PatchLink_artefacts/Release/VST3/PatchLink.vst3`
   → copier vers `C:\Program Files\Common Files\VST3\`
4. Le Standalone est dans `build/PatchLink_artefacts/Release/Standalone/`

### macOS (pour ton pote si Mac)
```bash
cd patchlink
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```
AU + VST3 générés automatiquement.

## Workflow de partage
1. Toi : `Export Patch` → `Maxime-vox-rap.patchlink` → envoi Discord / Drive.
2. Lui : `Import Patch` dans son instance → le plugin :
   - scanne ses plugins installés,
   - restaure chaque état,
   - affiche : `✅ FabFilter Pro-R OK | ❌ Valhalla VintageVerb 2.1.0 manquant (a trouvé 2.0.0)`.
3. Il s'enregistre en laissant le plugin en monitoring sur sa piste voix (Ableton : piste audio IN monitor ON).

> Règle d'or : pour un rappel à 100%, vous devez avoir **exactement les mêmes plugins aux mêmes versions**.
> Sinon on restaure ce qu'on peut + rapport clair. C'est pour ça qu'on embarque
> `manufacturer, version, format, fileOrIdentifierString` dans le patch.

## Format `.patchlink` (JSON)
```json
{
  "app": "PatchLink", "version": 1,
  "nodes": [{
    "uid": 101, "name": "Pro-R",
    "format": "VST3", "fileOrId": "FabFilter Pro-R",
    "manufacturer": "FabFilter", "pluginVersion": "1.0",
    "stateBase64": "...", "x": 100, "y": 200, "bypass": false
  }],
  "connections": [{ "src": 100, "srcCh": 0, "dst": 101, "dstCh": 0 }]
}
```

## Prochaines étapes conseillées
1. Compile ce scaffold, teste vide FL → Ableton.
2. Ajoute 1-2 plugins VST3 gratuits (TDR Nova, Valhalla Supermassive) pour tester l'export/import.
3. Ensuite on attaque : UI nodale complète + CLAP + PDC.
