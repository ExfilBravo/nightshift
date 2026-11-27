#!/bin/bash
# NightShift for macOS, in one command. From the folder that holds nightshift (and, beside it,
# nightshift-lofi and nightshift-bass), in Terminal:
#
#     bash nightshift/mac/build.sh              builds NightShift.app and its plugins, runs the tests
#     bash nightshift/mac/build.sh --install    ...then puts NightShift in Applications
#     bash nightshift/mac/build.sh --quick      builds only (no tests)
#     bash nightshift/mac/build.sh --universal  an app for Apple Silicon and Intel both (twice the
#                                               build; lets Finder's "Open using Rosetta" run it
#                                               with Intel-only plugins)
#
# It builds for the Mac it runs on: on an M1 (or any Apple Silicon) Mac a native Apple Silicon
# app, on an Intel Mac an Intel one. What it needs it gets: Apple's command line tools (it asks
# macOS to install them, then you run it again), CMake (downloaded into nightshift/mac/.tools, no
# admin rights), and the Tracktion engine with JUCE (in the kit, else cloned at the exact versions
# NightShift was made with). Everything it does goes into nightshift/mac/report.txt: bring that
# file back if anything fails.

set -u
MAC_DIR="$(cd "$(dirname "$0")" && pwd)"
NS_DIR="$(cd "$MAC_DIR/.." && pwd)"
ROOT_DIR="$(cd "$NS_DIR/.." && pwd)"
REPORT="$MAC_DIR/report.txt"
LOG="$MAC_DIR/build.log"
TOOLS="$MAC_DIR/.tools"
INSTALL=0
TESTS=1
ARCHS=""

for arg in "$@"; do
    case "$arg" in
        --install)   INSTALL=1 ;;
        --quick)     TESTS=0 ;;
        --universal) ARCHS="arm64;x86_64" ;;
    esac
done

TE_URL="https://github.com/Tracktion/tracktion_engine.git"
TE_COMMIT="a5e44996f5f08dfa6af5f5d02d900fb3cfa37422"          # the engine NightShift was built with
JUCE_COMMIT="7ff644f65d7daa1428c34b9738ebb5c449dc9172"        # and its JUCE
NAM_URL="https://github.com/sdatkinson/NeuralAmpModelerCore.git"
NAM_TAG="v0.6.0"                                              # Neural Amp Modeler's core, for the NAM Amp
CMAKE_VERSION="3.31.6"

: > "$REPORT"
: > "$LOG"
say()  { echo "$*"; echo "$*" >> "$REPORT"; }
fail() { say ""; say "STOPPED: $*"; say "The last lines of the build log ($LOG):"; tail -n 60 "$LOG" >> "$REPORT"; echo; echo "Bring $REPORT back to the PC."; exit 1; }

CHIP="$(uname -m)"
CORES="$(sysctl -n hw.ncpu)"
MEM_GB="$(( $(sysctl -n hw.memsize) / 1073741824 ))"
say "NightShift macOS build, $(date)"
say "macOS $(sw_vers -productVersion) on $([ "$CHIP" = "arm64" ] && echo "Apple Silicon" || echo "Intel"), $CORES cores, ${MEM_GB} GB"

#==============================================================================
# Apple's command line tools (the compiler)
if ! xcode-select -p >/dev/null 2>&1 || ! command -v clang++ >/dev/null 2>&1; then
    say "Apple's command line tools aren't installed: asking macOS to install them."
    xcode-select --install >/dev/null 2>&1
    say "Click Install in the window that opens, wait for it to finish, then run this again."
    exit 1
fi
say "Compiler: $(clang++ --version | head -n 1)"

# The engine is C++20 and uses its ranges library, which Apple's tools got with Command Line
# Tools 14.3 (macOS 13 Ventura). Older tools fail minutes into the build with "no member named
# 'ranges' in namespace 'std'", so try the exact constructs the engine uses up front
if ! clang++ -std=c++20 -x c++ -fsyntax-only - >> "$LOG" 2>&1 <<'EOF'
#include <algorithm>
#include <ranges>
int main() { int a[1] {}; std::ranges::copy (a, a); (void) std::views::take (a, 1); (void) std::views::iota (0, 1); return 0; }
EOF
then
    fail "this Mac's compiler ($(clang++ --version | head -n 1), macOS $(sw_vers -productVersion)) doesn't have the C++20 ranges library the Tracktion engine uses. NightShift needs macOS 13 Ventura or newer with Apple's Command Line Tools 14.3 or newer: update macOS (System Settings > General > Software Update), then run this again."
fi

# Xcode 15.0's linker produces broken binaries (JUCE refuses it: "Please upgrade to Xcode 15.1").
# Every 15.x reports "Apple clang 15.0.0", so tell 15.0 apart by its build number, as JUCE does
APPLE_BUILD="$(echo | clang++ -dM -E -x c++ - 2>/dev/null | awk '$2 == "__apple_build_version__" { print $3 }')"
if [ "${APPLE_BUILD:-0}" -ge 15000000 ] && [ "${APPLE_BUILD:-0}" -lt 15000100 ]; then
    fail "Command Line Tools 15.0 have a broken linker: update them (System Settings > General > Software Update) to 15.1 or newer, then run this again."
fi

#==============================================================================
# CMake: the one on this Mac if it's 3.22 or newer, else a copy of the official one in mac/.tools
if command -v cmake >/dev/null 2>&1 && cmake --version | head -n 1 | awk '{split($3,v,"."); exit !(v[1]>3 || (v[1]==3 && v[2]>=22))}'; then
    CMAKE="$(command -v cmake)"
else
    CMAKE="$TOOLS/cmake/CMake.app/Contents/bin/cmake"

    if [ ! -x "$CMAKE" ]; then
        say "Getting CMake $CMAKE_VERSION (official, from Kitware's GitHub releases)..."
        mkdir -p "$TOOLS"
        curl -fsSL -o "$TOOLS/cmake.tar.gz" \
            "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-macos-universal.tar.gz" >> "$LOG" 2>&1 \
            || fail "couldn't download CMake (is the Mac online?)"
        rm -rf "$TOOLS/cmake" && mkdir -p "$TOOLS/cmake"
        tar -xzf "$TOOLS/cmake.tar.gz" -C "$TOOLS/cmake" --strip-components 1 >> "$LOG" 2>&1 || fail "couldn't unpack CMake"
        rm -f "$TOOLS/cmake.tar.gz"
        xattr -dr com.apple.quarantine "$TOOLS/cmake" 2>/dev/null
    fi
fi
say "CMake: $("$CMAKE" --version | head -n 1)"

#==============================================================================
# The Tracktion engine and JUCE, at the versions NightShift was made with
TE_DIR="$NS_DIR/external/tracktion_engine"

if [ ! -f "$TE_DIR/modules/juce/CMakeLists.txt" ]; then
    say "Getting the Tracktion engine and JUCE (about 1 GB, once)..."
    rm -rf "$TE_DIR"
    mkdir -p "$NS_DIR/external"
    git clone "$TE_URL" "$TE_DIR" >> "$LOG" 2>&1 || fail "couldn't clone the Tracktion engine"
    git -C "$TE_DIR" checkout -q "$TE_COMMIT" >> "$LOG" 2>&1 || fail "couldn't check out the engine's version"
    # The engine names JUCE by its SSH address, which needs a GitHub key: plain https instead
    git -C "$TE_DIR" config submodule.modules/juce.url https://github.com/juce-framework/JUCE.git >> "$LOG" 2>&1
    git -C "$TE_DIR" submodule update --init modules/juce >> "$LOG" 2>&1 || fail "couldn't get JUCE"
fi

# (a kit carries the engine's sources without git; a clone can be checked)
if [ -e "$TE_DIR/modules/juce/.git" ] && [ "$(git -C "$TE_DIR/modules/juce" rev-parse HEAD 2>/dev/null)" != "$JUCE_COMMIT" ]; then
    say "Note: JUCE isn't at the version NightShift was made with ($JUCE_COMMIT)."
fi

# Neural Amp Modeler's core (MIT) with Eigen, nlohmann/json and AudioDSPTools, for the NAM Amp: in the kit, or
# cloned at the tag NightShift was made with
NAM_DIR="$NS_DIR/external/NeuralAmpModelerCore"

if [ ! -f "$NAM_DIR/NAM/get_dsp.h" ] || [ ! -d "$NAM_DIR/Dependencies/eigen/Eigen" ]; then
    say "Getting Neural Amp Modeler's core ($NAM_TAG, with Eigen; about 60 MB, once)..."
    rm -rf "$NAM_DIR"
    mkdir -p "$NS_DIR/external"
    git -c advice.detachedHead=false clone --depth 1 --branch "$NAM_TAG" --recurse-submodules --shallow-submodules "$NAM_URL" "$NAM_DIR" >> "$LOG" 2>&1 \
        || fail "couldn't clone Neural Amp Modeler's core (is the Mac online?)"
fi

#==============================================================================
# NightShift. The engine's source files are big: one compiler at a time per 2 GB of memory, so an
# 8 GB MacBook doesn't swap, and never more than there are cores
JOBS=$(( MEM_GB / 2 ))
[ "$JOBS" -gt "$CORES" ] && JOBS="$CORES"
[ "$JOBS" -lt 2 ] && JOBS=2
BUILD="$NS_DIR/build/mac"
CMAKE_ARCH=""
[ -n "$ARCHS" ] && CMAKE_ARCH="-DCMAKE_OSX_ARCHITECTURES=$ARCHS"
say ""
say "Building NightShift ($JOBS at a time; the first build takes 10-20 minutes)..."
"$CMAKE" -S "$NS_DIR" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 $CMAKE_ARCH >> "$LOG" 2>&1 \
    || fail "CMake couldn't set the build up"

if ! "$CMAKE" --build "$BUILD" --config Release -j "$JOBS" >> "$LOG" 2>&1; then
    say ""
    say "NightShift didn't build. Its errors:"
    grep -E "error:|error [A-Z][0-9]+|undefined symbol|ld: " "$LOG" | sort -u | head -n 80 >> "$REPORT"
    fail "the build failed"
fi

APP="$(find "$BUILD" -maxdepth 4 -name "NightShift.app" -type d | head -n 1)"
[ -d "$APP" ] || fail "the build finished but NightShift.app isn't where it should be"

# Signed for this Mac (no Apple account needed): an app built here runs without Gatekeeper asking
codesign --force --deep --sign - "$APP" >> "$LOG" 2>&1 || say "Note: couldn't sign the app ad hoc (it may still run)."
say "Built: $APP"

#==============================================================================
# The Nightshift Lofi and Bass plugins (VST3 and Audio Unit, into ~/Library/Audio/Plug-Ins)
for project in nightshift-lofi nightshift-bass; do
    if [ -f "$ROOT_DIR/$project/CMakeLists.txt" ]; then
        say "Building $project..."
        if "$CMAKE" -S "$ROOT_DIR/$project" -B "$ROOT_DIR/$project/build/mac" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 $CMAKE_ARCH >> "$LOG" 2>&1 \
           && "$CMAKE" --build "$ROOT_DIR/$project/build/mac" --config Release -j "$JOBS" >> "$LOG" 2>&1; then
            say "  $project: built and installed into ~/Library/Audio/Plug-Ins"
        else
            say "  $project: didn't build. Its errors:"
            grep -E "error:|ld: " "$LOG" | sort -u | tail -n 30 >> "$REPORT"
        fi
    fi
done

# Audio Units new to the system: macOS lists them after its plugin cache is refreshed
killall -9 AudioComponentRegistrar >/dev/null 2>&1

#==============================================================================
# The tests, as on the PC (the drum kit and instrument ones need the kits; NightShift downloads them)
if [ "$TESTS" = "1" ]; then
    EXE="$APP/Contents/MacOS/NightShift"
    say ""
    say "Running the tests..."
    PASSED=0
    FAILED=0

    for suite in masteringtest startscreentest trackstest closetest pianotest arrangetest stepstest sidebartest fxtest savetest \
                 fxstoptest looptest mutetest inspectortest stockfxtest bustest mixertest metertest drumtest drumbartest \
                 offlinedrummertest offlinebasstest basstest offlineguitartest guitartest guitarsoundtest offlinekeystest keystest \
                 offlinesynthtest synthplayertest masterengineertest masteraitest songwritertest bandmetertest songtest machinetest kitimporttest \
                 synthtest novatest keyboardtest metronometest editingtest arrangeedittest automationtest miditest tempotest tempocurvetest texttest namtest genretest feeltest vibetest vibepaneltest plugininstrumenttest pluginfindtest updatetest; do
        out="$MAC_DIR/.tests/$suite.txt"
        mkdir -p "$MAC_DIR/.tests"
        "$EXE" "--$suite" > "$out" 2>&1 &
        pid=$!
        # Ten minutes at most for a suite, then it's killed (NightShift itself: it's the background job)
        ( sleep 600; kill -9 $pid 2>/dev/null ) &
        watchdog=$!
        wait $pid 2>/dev/null
        code=$?
        pkill -P $watchdog 2>/dev/null; kill $watchdog 2>/dev/null     # its sleep too
        last="$(tail -n 1 "$out" 2>/dev/null)"

        if grep -q "^FAIL" "$out" || [ $code -ne 0 ]; then
            FAILED=$((FAILED + 1))
            say "  $suite: $last (exit $code)"
            grep "^FAIL" "$out" | head -n 8 | sed 's/^/      /' >> "$REPORT"
            [ -s "$out" ] || say "      (no output: it crashed or hung)"
        else
            PASSED=$((PASSED + 1))
        fi
    done

    say "Tests: $PASSED suites passed, $FAILED with failures."
fi

#==============================================================================
if [ "$INSTALL" = "1" ]; then
    DEST="/Applications"
    [ -w "$DEST" ] || DEST="$HOME/Applications"
    mkdir -p "$DEST"
    rm -rf "$DEST/NightShift.app"
    cp -R "$APP" "$DEST/" && say "Installed: $DEST/NightShift.app (open it from Launchpad or Finder)"
fi

say ""
say "Done. The first time NightShift records, macOS asks to let it use the microphone (your interface's inputs): allow it."
