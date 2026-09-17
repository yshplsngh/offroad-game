#!/bin/sh
# build-rpm.sh - build the Fedora RPM from this checkout.
#
# On a Fedora machine with the BuildRequires installed:
#   packaging/fedora/build-rpm.sh
# Extra arguments go to rpmbuild, e.g. to use a local Godot instead of
# Fedora's packages (development only):
#   packaging/fedora/build-rpm.sh --nodeps \
#     --define 'godot_bin /path/to/godot' --define 'godot_template /path/to/linux_release.x86_64'
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SPEC="$ROOT/packaging/fedora/ridgeline-offroad.spec"
NAME=ridgeline-offroad
VERSION=$(sed -n 's/^Version:[[:space:]]*//p' "$SPEC")
CPP_TAG=$(sed -n 's/^%global godot_cpp_tag[[:space:]]*//p' "$SPEC")
TOP=${RPM_TOPDIR:-$ROOT/build/rpmbuild}

mkdir -p "$TOP/SOURCES" "$TOP/SPECS"

# Source0: only what the native build needs. The browser reference, Node and
# node_modules are deliberately not in the tarball.
tar -C "$ROOT" \
    --exclude='native/build' --exclude='native/third_party' \
    --exclude='native/godot/.godot' --exclude='native/godot/bin/*.so' \
    --transform "s,^,$NAME-$VERSION/," \
    -czf "$TOP/SOURCES/$NAME-$VERSION.tar.gz" \
    native/worldcore native/godot packaging/fedora

CPP_TGZ="$TOP/SOURCES/godot-cpp-$CPP_TAG.tar.gz"
if [ ! -s "$CPP_TGZ" ]; then
    curl -fsSL -o "$CPP_TGZ" "https://github.com/godotengine/godot-cpp/archive/$CPP_TAG/godot-cpp-$CPP_TAG.tar.gz"
fi

cp "$SPEC" "$TOP/SPECS/"
exec rpmbuild --define "_topdir $TOP" -ba "$TOP/SPECS/$NAME.spec" "$@"
