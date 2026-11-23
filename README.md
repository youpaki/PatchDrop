# PatchDrop + PatchLink

Send your FX chain with a 6-character code, file, or direct DAW drag-and-drop. Open chains cleanly across **REAPER** (`.RfxChain` / `.rpp`), **Ableton Live** (`.adg` / `.adv`), **FL Studio** (`.fst` single & 10-slot mixer tracks), **Cubase / Nuendo / Studio One / Bitwig** (`.vstpreset`, `.fxchainpreset`, `.fxchain`, `.bwpreset`), or **Zero-Load** mode.

- **[PatchLink](patchlink/)** — VST3 + Standalone effect chainer (JUCE 8) with 4 studio themes (`Obsidian Amber`, `Carbon Iris`, `Tape Vermilion`, `Phosphor Console`) and 60fps UI transitions. Hosts third-party VST3 plugins, imports/exports `.RfxChain`, Ableton `.adg` Racks, FL `.fst` presets/tracks, and Steinberg `.vstpreset` chains with host DAW auto-detection, Zero-Load sharing/converting without instantiating plugins, and missing-plugin reporting.
- **[PatchDrop](patchdrop/)** — zero-dependency Node server + website. Community presets directory with categories and `#hashtags` (`presets.html`), code-based patch sharing (`POST /api/publish`, `GET /api/p/:code`, `/download`, `/fst`, `/rfxchain`, `/adg`, `/vstpreset`, `/api/tuids`), browser drag-and-drop publishing for all supported DAW formats, and multi-DAW download/drag cards. Live at `https://gg.trenas.fr/patchdrop/`.

## Quick start

```powershell
# Build the plugin (Windows, VS2022)
cd patchlink
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
# VST3 -> C:\Program Files\Common Files\VST3\

# Run the site locally
cd ../patchdrop
node server.js  # http://127.0.0.1:3250
```

See [patchlink/README.md](patchlink/README.md) for the full workflow.

## Releases

Windows VST3 + Standalone installer (`PatchLink-Setup-1.6.0.exe`) is attached to [GitHub Releases](../../releases) and served on the PatchDrop download page. macOS build: same CMake project (`-G Xcode`), AU + VST3 included.
