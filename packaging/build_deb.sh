#!/usr/bin/env bash
# Build a binary Debian package for QtreGB.
#
# Usage:
#   ./packaging/build_deb.sh
#   make deb
#
# The package is written to build/qtrebg_<version>_<arch>.deb. It is built
# directly with dpkg-deb; debhelper is not required.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${VERSION:-$(sed -n 's/.*#define QTREGB_VERSION "\([^"]*\)".*/\1/p' "$ROOT_DIR/gui/inc/version.h")}"
VERSION="${VERSION:-0.9.0}"
ARCH="${ARCH:-$(dpkg --print-architecture)}"
PKG_NAME="qtrebg"
OUTPUT="${OUTPUT:-$ROOT_DIR/build/${PKG_NAME}_${VERSION}_${ARCH}.deb}"
STAGE_DIR="${STAGE_DIR:-$ROOT_DIR/temp/deb/${PKG_NAME}_${VERSION}_${ARCH}}"
PKG_DIR="$STAGE_DIR/pkg"
DOC_DIR="$PKG_DIR/usr/share/doc/$PKG_NAME"

if ! command -v dpkg-deb >/dev/null 2>&1; then
	echo "error: dpkg-deb is required to build the .deb" >&2
	exit 1
fi

if [ ! -d "$ROOT_DIR/lib/imgui" ] || [ ! -d "$ROOT_DIR/third_party/ImGuiFileDialog" ]; then
	echo "error: local dependencies are missing (lib/imgui, third_party/ImGuiFileDialog)" >&2
	exit 1
fi

# Build the GUI binary.
make -C "$ROOT_DIR" -j"$(nproc)"

rm -rf "$STAGE_DIR"
mkdir -p "$PKG_DIR/DEBIAN" \
	"$PKG_DIR/usr/bin" \
	"$PKG_DIR/usr/share/QtreGB/assets" \
	"$PKG_DIR/usr/share/QtreGB/lang" \
	"$PKG_DIR/usr/share/applications" \
	"$PKG_DIR/usr/share/icons/hicolor/256x256/apps" \
	"$PKG_DIR/usr/share/metainfo" \
	"$DOC_DIR"

install -Dm755 "$ROOT_DIR/build/QtreGB-GUI" \
	"$PKG_DIR/usr/bin/QtreGB"
cp -a "$ROOT_DIR/assets/." "$PKG_DIR/usr/share/QtreGB/assets/"
cp -a "$ROOT_DIR/lang/." "$PKG_DIR/usr/share/QtreGB/lang/"
install -Dm644 "$ROOT_DIR/packaging/qtrebg.png" \
	"$PKG_DIR/usr/share/icons/hicolor/256x256/apps/qtrebg.png"
install -Dm644 "$ROOT_DIR/packaging/qtrebg.desktop" \
	"$PKG_DIR/usr/share/applications/io.github.kirbyboy.QtreGB.desktop"
install -Dm644 "$ROOT_DIR/packaging/io.github.kirbyboy.QtreGB.metainfo.xml" \
	"$PKG_DIR/usr/share/metainfo/io.github.kirbyboy.QtreGB.metainfo.xml"

install -Dm644 "$ROOT_DIR/README.md" "$DOC_DIR/README.md"
install -Dm644 "$ROOT_DIR/LICENSE.txt" "$DOC_DIR/LICENSE.txt"
install -Dm644 "$ROOT_DIR/assets/LICENSE" "$DOC_DIR/assets-LICENSE.txt"
install -Dm644 "$ROOT_DIR/THIRD_PARTY_NOTICES.md" \
	"$DOC_DIR/THIRD_PARTY_NOTICES.md"
install -Dm644 "$ROOT_DIR/TRADEMARK.md" "$DOC_DIR/TRADEMARK.md"
mkdir -p "$DOC_DIR/assets"
install -Dm644 "$ROOT_DIR/assets/QtreGB_icon_generic_256.png" \
	"$DOC_DIR/assets/QtreGB_icon_generic_256.png"
install -Dm644 "$ROOT_DIR/assets/screenshot_01.png" \
	"$DOC_DIR/assets/screenshot_01.png"
if [ -f "$ROOT_DIR/packaging/RELEASE_NOTES_${VERSION}.md" ]; then
	install -Dm644 "$ROOT_DIR/packaging/RELEASE_NOTES_${VERSION}.md" \
		"$DOC_DIR/RELEASE_NOTES.md"
fi

{
	printf 'QtreGB %s\n\n' "$VERSION"
	printf 'Copyright (C) 2026 the QtreGB project\n\n'
	printf 'The program code is licensed under GPL-3.0-or-later. The visual\n'
	printf 'assets are dedicated to the public domain under CC0 1.0. The\n'
	printf 'QtreGB name and logo are covered by the trademark policy.\n\n'
	printf '%s\n' '--- LICENSE.txt ---'
	cat "$ROOT_DIR/LICENSE.txt"
	printf '\n%s\n' '--- assets/LICENSE ---'
	cat "$ROOT_DIR/assets/LICENSE"
	printf '\n%s\n' '--- TRADEMARK.md ---'
	cat "$ROOT_DIR/TRADEMARK.md"
	printf '\n%s\n' '--- THIRD_PARTY_NOTICES.md ---'
	cat "$ROOT_DIR/THIRD_PARTY_NOTICES.md"
} > "$DOC_DIR/copyright"

install -Dm755 "$ROOT_DIR/packaging/deb/postinst" "$PKG_DIR/DEBIAN/postinst"
install -Dm755 "$ROOT_DIR/packaging/deb/postrm" "$PKG_DIR/DEBIAN/postrm"

INSTALLED_SIZE="$(du -sk "$PKG_DIR" | cut -f1)"
cat > "$PKG_DIR/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $VERSION
Architecture: $ARCH
Maintainer: QtreGB project <kirbyboy@users.noreply.github.com>
Installed-Size: $INSTALLED_SIZE
Section: games
Priority: optional
Homepage: https://github.com/kirbyboy/QtreGB
Vcs-Git: https://github.com/kirbyboy/QtreGB.git
Depends: libc6, libstdc++6, libgcc-s1, libsdl2-2.0-0, libgl1, libglew2.2
Description: DMG Game Boy emulator with SDL2 and ImGui
 QtreGB is a DMG Game Boy emulator written in C++17. The CPU uses a micro-op
 pipeline and the PPU is a native dot-driven implementation. It supports
 save-state slots, battery saves, RTC, Link cable sessions, English/Spanish
 UI and desktop launch for .gb files.
EOF

mkdir -p "$ROOT_DIR/build"
rm -f "$OUTPUT"
dpkg-deb --build --root-owner-group "$PKG_DIR" "$OUTPUT"
echo "Built $OUTPUT"
dpkg-deb --info "$OUTPUT"
