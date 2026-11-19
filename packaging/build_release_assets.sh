#!/usr/bin/env bash
# Build the Debian package and AppImage, then write a SHA256SUMS file.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${VERSION:-$(sed -n 's/.*#define QTREGB_VERSION "\([^"]*\)".*/\1/p' "$ROOT_DIR/gui/inc/version.h")}"
VERSION="${VERSION:-0.9.0}"
ARCH="${ARCH:-$(dpkg --print-architecture)}"

VERSION="$VERSION" "$ROOT_DIR/packaging/build_deb.sh"
VERSION="$VERSION" "$ROOT_DIR/packaging/build_appimage.sh"

DEB="$ROOT_DIR/build/qtrebg_${VERSION}_${ARCH}.deb"
APPIMAGE="$ROOT_DIR/build/QtreGB-${VERSION}-x86_64.AppImage"
SUMS="$ROOT_DIR/build/SHA256SUMS-${VERSION}"
NOTES="$ROOT_DIR/packaging/RELEASE_NOTES_${VERSION}.md"

(
	cd "$ROOT_DIR/build"
	sha256sum "$(basename "$DEB")" "$(basename "$APPIMAGE")" > "$(basename "$SUMS")"
if [ -f "$NOTES" ]; then
	cp "$NOTES" "$ROOT_DIR/build/RELEASE_NOTES-${VERSION}.md"
fi
)

echo "Release assets for $VERSION:"
ls -lh "$DEB" "$APPIMAGE" "$SUMS"
if [ -f "$ROOT_DIR/build/RELEASE_NOTES-${VERSION}.md" ]; then
	ls -lh "$ROOT_DIR/build/RELEASE_NOTES-${VERSION}.md"
fi
