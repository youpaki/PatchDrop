#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:?Usage: build_pkg.sh <VERSION> [BUILD_DIR] [OUT_DIR]}"
BUILD_DIR="${2:-patchlink/build}"
OUT_DIR="${3:-.}"

ARTEFACTS="$BUILD_DIR/PatchLink_artefacts/Release"
VST3="$ARTEFACTS/VST3/PatchLink.vst3"
AU="$ARTEFACTS/AU/PatchLink.component"
APP="$ARTEFACTS/Standalone/PatchLink.app"

for item in "$VST3" "$AU" "$APP"; do
  if [ ! -d "$item" ]; then
    echo "Missing macOS build artefact: $item" >&2
    exit 1
  fi
done

echo "=== Verifying Universal architectures (arm64 + x86_64) ==="
lipo -info "$VST3/Contents/MacOS/PatchLink"
lipo -info "$AU/Contents/MacOS/PatchLink"
lipo -info "$APP/Contents/MacOS/PatchLink"

echo "=== Ad-hoc codesigning bundles ==="
codesign --force --deep --sign - "$VST3"
codesign --force --deep --sign - "$AU"
codesign --force --deep --sign - "$APP"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

build_component_pkg() {
  local src_bundle="$1"
  local pkg_id="$2"
  local install_dir="$3"
  local out_pkg="$4"

  local root="$WORK/root_${pkg_id}"
  mkdir -p "$root"
  ditto "$src_bundle" "$root/$(basename "$src_bundle")"

  local plist="$WORK/comp_${pkg_id}.plist"
  pkgbuild --analyze --root "$root" "$plist"
  python3 - "$plist" <<'PY'
import plistlib, sys
p = sys.argv[1]
with open(p, 'rb') as f:
    data = plistlib.load(f)
for item in data:
    item['BundleIsRelocatable'] = False
with open(p, 'wb') as f:
    plistlib.dump(data, f)
PY

  pkgbuild \
    --root "$root" \
    --component-plist "$plist" \
    --identifier "$pkg_id" \
    --version "$VERSION" \
    --install-location "$install_dir" \
    "$out_pkg"
}

mkdir -p "$WORK/pkgs"
build_component_pkg "$VST3" "fr.trenas.patchlink.vst3" "/Library/Audio/Plug-Ins/VST3" "$WORK/pkgs/vst3.pkg"
build_component_pkg "$AU" "fr.trenas.patchlink.au" "/Library/Audio/Plug-Ins/Components" "$WORK/pkgs/au.pkg"
build_component_pkg "$APP" "fr.trenas.patchlink.standalone" "/Applications" "$WORK/pkgs/standalone.pkg"

cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>PatchLink ${VERSION}</title>
  <options customize="allow" require-scripts="false" rootVolumeOnly="true" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true"/>
  <choices-outline>
    <line choice="vst3"/>
    <line choice="au"/>
    <line choice="standalone"/>
  </choices-outline>
  <choice id="vst3" title="PatchLink VST3 Plugin" description="Installs to /Library/Audio/Plug-Ins/VST3 (Ableton Live, REAPER, FL Studio, Cubase, Studio One, Bitwig)">
    <pkg-ref id="fr.trenas.patchlink.vst3"/>
  </choice>
  <choice id="au" title="PatchLink Audio Unit (AU)" description="Installs to /Library/Audio/Plug-Ins/Components (Logic Pro, Ableton Live)">
    <pkg-ref id="fr.trenas.patchlink.au"/>
  </choice>
  <choice id="standalone" title="PatchLink Standalone App" description="Installs PatchLink.app to /Applications">
    <pkg-ref id="fr.trenas.patchlink.standalone"/>
  </choice>
  <pkg-ref id="fr.trenas.patchlink.vst3" version="${VERSION}">vst3.pkg</pkg-ref>
  <pkg-ref id="fr.trenas.patchlink.au" version="${VERSION}">au.pkg</pkg-ref>
  <pkg-ref id="fr.trenas.patchlink.standalone" version="${VERSION}">standalone.pkg</pkg-ref>
</installer-gui-script>
XML

mkdir -p "$OUT_DIR"
OUT_PKG="$OUT_DIR/PatchLink-Setup-macOS-${VERSION}.pkg"
productbuild \
  --distribution "$WORK/distribution.xml" \
  --package-path "$WORK/pkgs" \
  "$OUT_PKG"

echo "Built macOS installer: $OUT_PKG ($(wc -c < "$OUT_PKG") bytes)"
