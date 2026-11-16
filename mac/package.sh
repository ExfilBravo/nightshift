#!/bin/bash
# Makes NightShift's Mac installer, nightshift/build/installer/NightShift-<version>-mac.pkg, from what
# mac/build.sh and nightshift-drums/mac/build.sh built (run those first). From the folder that holds
# nightshift and the plugin projects beside it:
#
#     bash nightshift/mac/package.sh
#
# The installer puts NightShift (and the Nightshift Drums app) in Applications, and the NightShift
# plugins (VST3 and Audio Unit: Lofi, Bass, Drums) in /Library/Audio/Plug-Ins, where every plugin host
# looks. It isn't signed by Apple (that needs a paid developer account), so the first time macOS stops
# it: System Settings > Privacy & Security > Open Anyway. What it installs isn't stopped again.
set -euo pipefail

MAC_DIR="$(cd "$(dirname "$0")" && pwd)"
NS_DIR="$(cd "$MAC_DIR/.." && pwd)"
ROOT_DIR="$(cd "$NS_DIR/.." && pwd)"
VERSION="$(sed -n 's/^project(Nightshift VERSION \([0-9.]*\).*/\1/p' "$NS_DIR/CMakeLists.txt")"
OUT="$NS_DIR/build/installer"
WORK="$NS_DIR/build/pkg"

[ -n "$VERSION" ] || { echo "No version in CMakeLists.txt"; exit 1; }
rm -rf "$WORK"
mkdir -p "$WORK/app" "$WORK/vst3" "$WORK/au" "$OUT"

# NightShift, with its licence (the build puts it in Resources) and the third-party notices
APP="$(find "$NS_DIR/build/mac" -maxdepth 5 -name "NightShift.app" -type d | head -n 1)"
[ -n "$APP" ] || { echo "NightShift.app isn't built: run nightshift/mac/build.sh first"; exit 1; }
ditto "$APP" "$WORK/app/NightShift.app"
cp "$NS_DIR/NOTICES.txt" "$WORK/app/NightShift.app/Contents/Resources/NOTICES.txt"
echo "NightShift $VERSION: $APP"

# The Nightshift Drums app, when it's built
DRUMS_APP="$(find "$ROOT_DIR/nightshift-drums/build/mac" -maxdepth 6 -name "Nightshift Drums.app" -type d 2>/dev/null | head -n 1 || true)"
if [ -n "$DRUMS_APP" ]; then
    ditto "$DRUMS_APP" "$WORK/app/Nightshift Drums.app"
    echo "Nightshift Drums app: $DRUMS_APP"
fi

# Every plugin the three plugin projects built (JUCE puts them in <target>_artefacts/Release/VST3 and AU)
for project in nightshift-lofi nightshift-bass nightshift-drums; do
    B="$ROOT_DIR/$project/build/mac"
    if [ ! -d "$B" ]; then
        echo "$project isn't built, so the installer leaves it out"
        continue
    fi

    while IFS= read -r -d '' p; do
        ditto "$p" "$WORK/vst3/$(basename "$p")"
        echo "VST3: $(basename "$p")"
    done < <(find "$B" -path "*_artefacts/*" -name "*.vst3" -type d -prune -print0)

    while IFS= read -r -d '' p; do
        ditto "$p" "$WORK/au/$(basename "$p")"
        echo "Audio Unit: $(basename "$p")"
    done < <(find "$B" -path "*_artefacts/*" -name "*.component" -type d -prune -print0)
done

# Signed ad hoc (no Apple account): Apple Silicon runs only signed code, and the seal must cover the
# notices added above
for b in "$WORK"/app/*.app "$WORK"/vst3/*.vst3 "$WORK"/au/*.component; do
    [ -e "$b" ] && codesign --force --deep --sign - "$b"
done

# One package per place things go. Their bundles aren't relocatable: without that, Installer would
# "update" a copy of NightShift it finds anywhere else on the Mac instead of installing into Applications.
component () {
    local root="$1" location="$2" name="$3"
    local plist="$WORK/$name.plist"

    pkgbuild --analyze --root "$root" "$plist" >/dev/null
    local i=0
    while /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist" 2>/dev/null; do
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsVersionChecked false" "$plist" 2>/dev/null || true
        i=$((i + 1))
    done

    pkgbuild --root "$root" --component-plist "$plist" --install-location "$location" \
             --identifier "com.nightwriter.nightshift.$name" --version "$VERSION" "$WORK/$name.pkg" >/dev/null
    PACKAGES+=(--package "$WORK/$name.pkg")
}

PACKAGES=()
component "$WORK/app" /Applications app
[ -n "$(ls -A "$WORK/vst3")" ] && component "$WORK/vst3" /Library/Audio/Plug-Ins/VST3 vst3
[ -n "$(ls -A "$WORK/au")" ] && component "$WORK/au" /Library/Audio/Plug-Ins/Components au

PKG="$OUT/NightShift-$VERSION-mac.pkg"
rm -f "$PKG"
productbuild "${PACKAGES[@]}" "$PKG" >/dev/null
echo "Built: $PKG ($(du -h "$PKG" | cut -f1))"
