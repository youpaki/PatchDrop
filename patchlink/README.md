# PatchLink

A resizable VST3 and Standalone multi-DAW effect chainer by **leve**. Host VST3 effects (and Audio Units on macOS), arrange them in a 60fps drag-to-reorder serial rack or a pannable stereo node canvas, drop FX chains from **REAPER** (`.RfxChain`, `.rpp`), **Ableton Live** (`.adg`, `.adv`), **FL Studio** (`.fst`), **Cubase / Studio One / Bitwig** (`.vstpreset`, `.fxchainpreset`, `.fxchain`, `.bwpreset`), share/stage in **Zero-Load** mode, and drag chains back out into any DAW natively.

## Interface & v1.6.0 Features

- **Chain** — ordered, smooth 60fps drag-to-reorder plugin cards with bypass, hosted-editor, per-slot single-preset DAW drag/save icons, and per-plugin actions. Audio runs from top to bottom.
- **Patcher** — clipped-grid stereo routing canvas with left/middle-mouse drag panning, port/cable hover highlights, curved stereo cables, right-click actions, and Delete-key removal.
- **Studio Color Themes** — live **Theme** switcher in the top bar with persistent selection (`%APPDATA%\PatchLink\ui_theme.txt`): *Obsidian Amber* (default), *Carbon Iris*, *Tape Vermilion*, and *Phosphor Console*.
- **Multi-DAW → PatchLink** — drop `.RfxChain` / `.rpp` (REAPER), `.adg` / `.adv` (Ableton Live Audio Effect Racks), `.fst` (FL Studio single plugin or full 10-slot Mixer track), or `.vstpreset` / `.fxchainpreset` / `.fxchain` / `.bwpreset` (Cubase, Nuendo, Studio One, Bitwig) straight onto PatchLink to unpack third-party VST3 chains with bypass flags and binary preset states intact.
- **PatchLink → Any DAW (Auto-Detected Bidirectional Drag & Drop)** — PatchLink auto-detects the DAW hosting it (`FL Studio` → `.fst`, `REAPER` → `.RfxChain`, `Ableton Live` → `.adg` Audio Effect Rack, `Cubase / Nuendo / Studio One / Bitwig` → `.vstpreset`), and in **Standalone** asks your target format once via the toolbar button. Drag the toolbar button (or an individual slot's export icon) straight onto your DAW mixer/track, or save preset files to disk.
- **Zero-Load Mode (Drop & Open Code)** — when PatchLink is empty and receives a drop, or when opening a 6-character PatchDrop code, you can choose **Share code**, **Stage only**, or **Drag to DAW** directly from RAM without loading or instantiating any plugin DLLs/GUIs inside PatchLink.
- **Plugin Library & Scan Manager** — instant full-library search, DAW-aware folder proposals (FL Studio, REAPER, Ableton, Cubase, VST3 system paths), live scan progress, cancellation, failure reporting, and blacklist recovery.
- **PatchDrop Web & Multi-DAW Cards** — public/unlisted publishing, category and `#hashtag` filtering on the PatchDrop directory (`https://gg.trenas.fr/patchdrop/presets.html`), browser drag-in publishing across all DAW formats, and per-card `.fst`, `.RfxChain`, `.adg`, and `.vstpreset` drag/download buttons.

Double-click a chain card or patcher node to open its own editor. Double-click the patch title to rename it. `Ctrl/Cmd+F`, `Ctrl/Cmd+I`, and `Ctrl/Cmd+E` open the browser, import, and export respectively.

## Patch compatibility

The writer uses chain format v2 and retains complete opaque plugin-state blobs plus human-readable plugin identifiers (`uniqueId`, `tuidHex`, `flPluginName`, `identifier`, `fileOrId`). The loader accepts both v1 and v2 files, recalls by plugin identity rather than absolute path, and reports unavailable dependencies without discarding their saved state from the source patch. Unknown JSON properties remain forward-compatible.

## Build (Windows)

Requirements: CMake 3.22+, Git, and Visual Studio 2022 C++ Build Tools. JUCE 8.0.6 is fetched at configure time.

```powershell
cd patchlink
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

Artifacts:

- `build/PatchLink_artefacts/Release/VST3/PatchLink.vst3`
- `build/PatchLink_artefacts/Release/Standalone/PatchLink.exe`
- `build/ScanTest_artefacts/Release/ScanTest.exe`

Headless scanner & `.fst` roundtrip checks:

```powershell
.\build\ScanTest_artefacts\Release\ScanTest.exe
.\build\ScanTest_artefacts\Release\ScanTest.exe --folder "C:\path\to\plugins"
.\build\ScanTest_artefacts\Release\ScanTest.exe --test-fst "C:\path\to\mixer.fst" --instantiate
.\build\ScanTest_artefacts\Release\ScanTest.exe --test-export "C:\path\to\mixer.fst"
```

`COPY_PLUGIN_AFTER_BUILD` is intentionally disabled. Use the Inno Setup installer (`installer/installer.iss`) for deployment to the system VST3 folder.

## macOS

The same CMake project targets VST3 and Standalone and enables JUCE's native Audio Unit hosting. This path has not yet received the same machine-level QA as Windows.

## Scope

PatchLink currently hosts VST3 everywhere and AU on macOS. Stock JUCE does not host CLAP; supporting it would require an additional host library. Advanced per-branch dry/wet processing and full host latency compensation remain future engine work.
