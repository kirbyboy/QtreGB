#!/usr/bin/env bash
# Build a self-contained QtreGB AppImage.
#
# Usage:
#   ./packaging/build_appimage.sh
#   make appimage
#
# linuxdeploy and appimagetool are downloaded into temp/appimage-tools when
# missing. The resulting AppImage is written to build/.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${VERSION:-$(sed -n 's/.*#define QTREGB_VERSION "\([^"]*\)".*/\1/p' "$ROOT_DIR/gui/inc/version.h")}"
VERSION="${VERSION:-0.9.0}"
TOOLS_DIR="${TOOLS_DIR:-$ROOT_DIR/temp/appimage-tools}"
APPDIR="${APPDIR:-$ROOT_DIR/temp/appimage/QtreGB.AppDir}"
OUTPUT="${OUTPUT:-$ROOT_DIR/build/QtreGB-${VERSION}-x86_64.AppImage}"
LINUXDEPLOY="${LINUXDEPLOY:-$TOOLS_DIR/linuxdeploy-x86_64.AppImage}"
APPIMAGETOOL="${APPIMAGETOOL:-$TOOLS_DIR/appimagetool-x86_64.AppImage}"
CXX="${CXX:-g++}"

download_tool() {
	local output="$1"
	local url="$2"
	if [[ ! -x "$output" ]]; then
		mkdir -p "$(dirname "$output")"
		echo "Downloading $(basename "$output")..."
		curl -L --fail --retry 3 -o "$output" "$url"
		chmod +x "$output"
	fi
}

download_tool "$LINUXDEPLOY" \
	"https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
download_tool "$APPIMAGETOOL" \
	"https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"

if [ ! -d "$ROOT_DIR/lib/imgui" ] || [ ! -d "$ROOT_DIR/third_party/ImGuiFileDialog" ]; then
	echo "error: local dependencies are missing (lib/imgui, third_party/ImGuiFileDialog)" >&2
	exit 1
fi

make -C "$ROOT_DIR" -j"$(nproc)"

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" \
	"$APPDIR/usr/share/QtreGB/assets" \
	"$APPDIR/usr/share/QtreGB/lang" \
	"$APPDIR/usr/share/applications" \
	"$APPDIR/usr/share/icons/hicolor/256x256/apps" \
	"$APPDIR/usr/share/metainfo" \
	"$APPDIR/usr/share/doc/qtrebg"

cp "$ROOT_DIR/build/QtreGB-GUI" "$APPDIR/usr/bin/QtreGB"
cp -a "$ROOT_DIR/assets/." "$APPDIR/usr/share/QtreGB/assets/"
cp -a "$ROOT_DIR/lang/." "$APPDIR/usr/share/QtreGB/lang/"
cp "$ROOT_DIR/packaging/qtrebg.png" \
	"$APPDIR/usr/share/icons/hicolor/256x256/apps/qtrebg.png"
cp "$ROOT_DIR/packaging/qtrebg.desktop" \
	"$APPDIR/usr/share/applications/io.github.kirbyboy.QtreGB.desktop"
cp "$ROOT_DIR/packaging/io.github.kirbyboy.QtreGB.metainfo.xml" \
	"$APPDIR/usr/share/metainfo/io.github.kirbyboy.QtreGB.metainfo.xml"
cp "$ROOT_DIR/README.md" "$ROOT_DIR/LICENSE.txt" "$ROOT_DIR/THIRD_PARTY_NOTICES.md" \
	"$ROOT_DIR/TRADEMARK.md" "$APPDIR/usr/share/doc/qtrebg/"
cp "$ROOT_DIR/assets/LICENSE" "$APPDIR/usr/share/doc/qtrebg/assets-LICENSE.txt"
mkdir -p "$APPDIR/usr/share/doc/qtrebg/assets"
cp "$ROOT_DIR/assets/QtreGB_icon_generic_256.png" \
	"$APPDIR/usr/share/doc/qtrebg/assets/QtreGB_icon_generic_256.png"
cp "$ROOT_DIR/assets/screenshot_01.png" \
	"$APPDIR/usr/share/doc/qtrebg/assets/screenshot_01.png"
if [ -f "$ROOT_DIR/packaging/RELEASE_NOTES_${VERSION}.md" ]; then
	cp "$ROOT_DIR/packaging/RELEASE_NOTES_${VERSION}.md" \
		"$APPDIR/usr/share/doc/qtrebg/RELEASE_NOTES.md"
fi

# AppImage root metadata.
cp "$ROOT_DIR/packaging/qtrebg.desktop" \
	"$APPDIR/io.github.kirbyboy.QtreGB.desktop"
cp "$ROOT_DIR/packaging/qtrebg.png" "$APPDIR/qtrebg.png"

"$LINUXDEPLOY" --appimage-extract-and-run \
	--appdir "$APPDIR" \
	--executable "$APPDIR/usr/bin/QtreGB" \
	--desktop-file "$APPDIR/io.github.kirbyboy.QtreGB.desktop" \
	--icon-file "$APPDIR/qtrebg.png"

# linuxdeploy excludes these by default; ship them for older systems.
LIBSTDCXX="$("$CXX" -print-file-name=libstdc++.so.6)"
LIBGCC="$("$CXX" -print-file-name=libgcc_s.so.1)"
cp -L "$LIBSTDCXX" "$APPDIR/usr/lib/"
cp -L "$LIBGCC" "$APPDIR/usr/lib/"

mkdir -p "$ROOT_DIR/build"
rm -f "$OUTPUT"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 "$APPIMAGETOOL" \
	--no-appstream "$APPDIR" "$OUTPUT"
ls -lh "$OUTPUT"
