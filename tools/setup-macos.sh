#!/bin/sh
# setup-macos.sh - set up the macOS build environment (milestone 7 dev tool).
#
#   tools/setup-macos.sh [--skip-tests] [--skip-godot-check]
#
# Installs the toolchain with Homebrew (ninja + the godot 4.7 cask; cmake and
# python3 if missing), checks out godot-cpp godot-4.5-stable at
# native/third_party/godot-cpp, builds the core library, runs the golden test
# suite, and builds the GDExtension for both Godot targets. The extension is
# built universal (x86_64 + arm64) so the library names match the macos.*
# entries in native/godot/worldcore.gdextension:
#   native/godot/bin/libworldcore.macos.template_debug.universal.dylib
#   native/godot/bin/libworldcore.macos.template_release.universal.dylib
#
# Xcode Command Line Tools (Apple clang) must already be installed:
#   xcode-select --install
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
GODOT_CPP_TAG=godot-4.5-stable
GODOT_CPP_DIR=$ROOT/native/third_party/godot-cpp
MACOS_ARCHS="x86_64;arm64"

SKIP_TESTS=0
SKIP_GODOT_CHECK=0
for arg in "$@"; do
    case $arg in
        --skip-tests) SKIP_TESTS=1 ;;
        --skip-godot-check) SKIP_GODOT_CHECK=1 ;;
        *) echo "usage: tools/setup-macos.sh [--skip-tests] [--skip-godot-check]" >&2; exit 64 ;;
    esac
done

[ "$(uname -s)" = Darwin ] || { echo "setup-macos.sh: this is not macOS" >&2; exit 1; }
xcode-select -p >/dev/null 2>&1 || {
    echo "setup-macos.sh: Xcode Command Line Tools missing - run: xcode-select --install" >&2
    exit 1
}
command -v brew >/dev/null 2>&1 || {
    echo "setup-macos.sh: Homebrew missing - install it from https://brew.sh" >&2
    exit 1
}

# --- toolchain ---------------------------------------------------------------
command -v cmake >/dev/null 2>&1 || brew install cmake
command -v ninja >/dev/null 2>&1 || brew install ninja
command -v python3 >/dev/null 2>&1 || brew install python3
# The godot cask ships 4.7.x and links a `godot` binary onto PATH.
command -v godot >/dev/null 2>&1 || [ -x /Applications/Godot.app/Contents/MacOS/Godot ] \
    || brew install --cask godot
GODOT=$(command -v godot || echo /Applications/Godot.app/Contents/MacOS/Godot)
if [ "$SKIP_GODOT_CHECK" = 0 ]; then
    "$GODOT" --version | grep -q '^4\.7\.' || {
        echo "setup-macos.sh: need Godot 4.7.x, found: $("$GODOT" --version)" >&2
        exit 1
    }
fi

# --- godot-cpp ---------------------------------------------------------------
if [ ! -f "$GODOT_CPP_DIR/CMakeLists.txt" ]; then
    git clone --depth 1 -b "$GODOT_CPP_TAG" \
        https://github.com/godotengine/godot-cpp "$GODOT_CPP_DIR"
fi

# --- core library + golden tests (host arch is enough for tests) -------------
cmake -S "$ROOT/native/worldcore" -B "$ROOT/native/build/core" -G Ninja
cmake --build "$ROOT/native/build/core"
[ "$SKIP_TESTS" = 1 ] || ctest --test-dir "$ROOT/native/build/core" --output-on-failure

# --- GDExtension, debug + release --------------------------------------------
# CMAKE_OSX_ARCHITECTURES must list both archs explicitly: godot-cpp only names
# the library `universal` (matching worldcore.gdextension) when it does, and it
# also makes worldcore_core universal so the dylib links both slices.
for target in template_debug template_release; do
    build=$ROOT/native/build/gde
    [ "$target" = template_release ] && build=$ROOT/native/build/gde-release
    cmake -S "$ROOT/native/worldcore" -B "$build" -G Ninja \
        -DWORLDCORE_GODOT=ON -DWORLDCORE_TESTS=OFF \
        -DGODOTCPP_TARGET=$target \
        -DCMAKE_OSX_ARCHITECTURES="$MACOS_ARCHS"
    cmake --build "$build" --target worldcore
done

echo
echo "macOS build environment ready. Play with:"
echo "  $GODOT --path $ROOT/native/godot"
