# PatchLink

A resizable VST3 and Standalone effect chainer by **leve**. Host VST3 effects (and Audio Units on macOS), arrange them in a serial chain or a stereo routing canvas, then share the complete chain and plugin states as a `.patchlink` file or a six-character PatchDrop code.

## Interface

- **Chain** — ordered, drag-to-reorder plugin cards with bypass, hosted-editor, and per-plugin actions. Audio runs from top to bottom.
- **Patcher** — scrollable node canvas with curved stereo cables. Drag from a right-hand port to a left-hand port, right-click nodes or cables for actions, and press Delete to remove the selected plugin.
- **Plugin library** — instant full-library search by name, developer, format, or category. No fixed result limit.
- **Scan Manager** — opt-in folder scanning, DAW-aware folder proposals, subfolder controls, live progress, cancellation, failure reporting, and blacklist management.
- **Sharing** — public or unlisted PatchDrop publishing, dependency manifest, open-by-code, and file import/export. Network work is asynchronous.

Double-click a chain card or patcher node to open its own editor. Double-click the patch title to rename it. `Ctrl/Cmd+F`, `Ctrl/Cmd+I`, and `Ctrl/Cmd+E` open the browser, import, and export respectively.

## Patch compatibility

The writer uses chain format v2 and retains complete opaque plugin-state blobs plus human-readable plugin identifiers. The loader accepts both v1 and v2 files, recalls by plugin identity rather than absolute path, and reports unavailable dependencies without discarding their saved state from the source patch. Unknown JSON properties remain forward-compatible. Explicit chain order and patch title are additive v2 properties.

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

Headless scanner checks:

```powershell
.\build\ScanTest_artefacts\Release\ScanTest.exe
.\build\ScanTest_artefacts\Release\ScanTest.exe --folder "C:\path\to\plugins"
.\build\ScanTest_artefacts\Release\ScanTest.exe --full
```

`COPY_PLUGIN_AFTER_BUILD` is intentionally disabled. Use the Inno Setup installer for deployment to the system VST3 folder.

## macOS

The same CMake project targets VST3 and Standalone and enables JUCE's native Audio Unit hosting. This path has not yet received the same machine-level QA as Windows.

## Scope

PatchLink currently hosts VST3 everywhere and AU on macOS. Stock JUCE does not host CLAP; supporting it would require an additional host library. Advanced per-branch dry/wet processing and full host latency compensation remain future engine work.
