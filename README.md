# PatchDrop + PatchLink

Send your effect chain with a code. Your friend types it in and records with **your** effects.

- **[PatchLink](patchlink/)** — VST3 + Standalone chainer (JUCE 8). Simple vertical mode or full Patcher nodal mode. Hosts third-party VST3 plugins, exports `.patchlink` files or 6-letter share codes, reports missing plugins instead of crashing.
- **[PatchDrop](patchdrop/)** — zero-dependency Node server + website. Code-based patch sharing API (`POST /api/publish`, `GET /api/p/:code`), community preset explorer, download page. Live at `https://gg.trenas.fr/patchdrop/`.

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

See [patchlink/README.md](patchlink/README.md) for the full workflow (FL Studio ↔ Ableton).

## Releases

Windows VST3 + Standalone zips are attached to [GitHub Releases](../../releases) and served on the PatchDrop download page. macOS build: same CMake project (`-G Xcode`), AU + VST3 included.
