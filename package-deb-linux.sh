#!/usr/bin/env bash
#
# Packages an already-built OwpenGram binary as a .deb for Debian/Ubuntu
# systems. Does NOT build anything - run build-linux.sh first. Does NOT
# require Debian/apt to run: dpkg-deb builds a valid archive on any Linux
# that has the `dpkg` package installed (e.g. `pacman -S dpkg` on Arch).
#
# Usage:
#   ./package-deb-linux.sh [path-to-OwpenGram-binary]
#
# Without an argument, picks the first binary found among:
#   out-docker/Release, out-docker/Debug, out/Release, out/Debug
#
# Output: owpengram-desktop_<version>_amd64.deb in the repo root.
#
# Dependency note: the binary lazy-loads Qt/WebRTC/X11/EGL/GL via
# Implib.so stub archives (dlopen at runtime), so `ldd` shows only base
# system libraries (glibc, fontconfig, freetype, glib2, ...) - present on
# essentially any current Debian/Ubuntu desktop install already. This
# script does not pin exact Depends: versions, since that would need
# `dpkg-shlibdeps` run on an actual Debian system to get right, which
# isn't available here. If an install ever hits a missing .so, run
# `sudo apt-get install -f` on the target machine, or add the specific
# package to DEB_DEPENDS below and repackage.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "[ERROR] dpkg-deb not found. Install it first, e.g.:" >&2
    echo "        sudo pacman -S --needed dpkg" >&2
    exit 1
fi

BIN_SRC="${1:-}"
if [ -z "$BIN_SRC" ]; then
    for candidate in \
        "$ROOT/out-docker/Release/OwpenGram" \
        "$ROOT/out-docker/Debug/OwpenGram" \
        "$ROOT/out/Release/OwpenGram" \
        "$ROOT/out/Debug/OwpenGram"
    do
        if [ -f "$candidate" ]; then BIN_SRC="$candidate"; break; fi
    done
fi
if [ -z "$BIN_SRC" ] || [ ! -f "$BIN_SRC" ]; then
    echo "[ERROR] No built OwpenGram binary found under out-docker/ or out/." >&2
    echo "        Build first (./build-linux.sh), or pass the binary path explicitly." >&2
    exit 1
fi

ART_DIR="$ROOT/Telegram/Resources/OwpenGram/art"
XDG_DIR="$ROOT/lib/xdg"
VERSION="$(awk '/^AppVersionStrSmall/{print $2}' "$ROOT/Telegram/build/version")"
PKG_NAME="owpengram-desktop"
ARCH="amd64"
DEB_DEPENDS="libc6"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

echo "Packaging $BIN_SRC as ${PKG_NAME}_${VERSION}_${ARCH}.deb"

mkdir -p "$STAGE/DEBIAN"
mkdir -p "$STAGE/usr/bin"
mkdir -p "$STAGE/usr/share/applications"
mkdir -p "$STAGE/usr/share/dbus-1/services"
mkdir -p "$STAGE/usr/share/metainfo"

install -m 755 "$BIN_SRC" "$STAGE/usr/bin/OwpenGram"

for size in 16 32 48 64 128 256 512; do
    dir="$STAGE/usr/share/icons/hicolor/${size}x${size}/apps"
    mkdir -p "$dir"
    install -m 644 "$ART_DIR/icon${size}.png" "$dir/org.owpengram.desktop.png"
done

# Absolute Exec= (not just "OwpenGram") so launching doesn't depend on PATH
# ordering ever putting something else named OwpenGram first.
sed -E "s#^(Exec|TryExec)=OwpenGram#\1=/usr/bin/OwpenGram#" \
    "$XDG_DIR/org.owpengram.desktop.desktop" \
    > "$STAGE/usr/share/applications/org.owpengram.desktop.desktop"

sed "s#@CMAKE_INSTALL_FULL_BINDIR@#/usr/bin#" \
    "$XDG_DIR/org.owpengram.desktop.service" \
    > "$STAGE/usr/share/dbus-1/services/org.owpengram.desktop.service"

if [ -f "$XDG_DIR/org.owpengram.desktop.metainfo.xml" ]; then
    install -m 644 "$XDG_DIR/org.owpengram.desktop.metainfo.xml" "$STAGE/usr/share/metainfo/"
fi

INSTALLED_SIZE="$(du -sk "$STAGE/usr" | cut -f1)"

cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $VERSION
Section: net
Priority: optional
Architecture: $ARCH
Depends: $DEB_DEPENDS
Installed-Size: $INSTALLED_SIZE
Maintainer: OwpenGram <noreply@example.com>
Description: OwpenGram Desktop
 Fork of Telegram Desktop. Portable build - Qt, WebRTC, X11/EGL client
 libraries are bundled and lazy-loaded rather than depended on directly.
EOF

OUT="$ROOT/${PKG_NAME}_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$STAGE" "$OUT"

echo ""
echo "Built: $OUT"
echo "Install with: sudo apt install ./$(basename "$OUT")"
echo "(or on a non-apt system: sudo dpkg -i $(basename "$OUT"))"
