#!/usr/bin/env bash
# Fetch the pinned local dependencies into lib/ and build their static
# libraries. The script is idempotent: existing sources and libraries are
# reused, missing ones are downloaded/built.
#
# Versions are pinned to the same ones used by the project:
#   Dear ImGui       v1.92.4-docking
#   doctest          v2.4.12
# ImGuiFileDialog v0.6.8 is vendored in third_party/ImGuiFileDialog (MIT) and
# is built from there; it is not downloaded by this script.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIB_DIR="$ROOT_DIR/lib"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/qtrebg-deps.XXXXXX")"

cleanup()
{
	rm -rf "$TMP_DIR"
}
trap cleanup EXIT

require()
{
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "error: '$1' is required to fetch the dependencies" >&2
		exit 1
	fi
}

require git
require make
require g++
require sdl2-config

clone_tag()
{
	local repo="$1"
	local tag="$2"
	local dest="$3"

	git -c advice.detachedHead=false clone --depth 1 --branch "$tag" \
		"$repo" "$dest"
	rm -rf "$dest/.git" "$dest/.github"
}

fetch_imgui()
{
	local dir="$LIB_DIR/imgui"
	local src="$dir/src"

	if [ ! -f "$src/imgui.cpp" ]; then
		echo "Fetching Dear ImGui v1.92.4 (docking)..."
		clone_tag https://github.com/ocornut/imgui.git v1.92.4-docking \
			"$TMP_DIR/imgui"
		mkdir -p "$src"
		cp "$TMP_DIR/imgui"/*.cpp "$TMP_DIR/imgui"/*.h "$src/"
		cp -R "$TMP_DIR/imgui/backends" "$src/backends"
		cp "$TMP_DIR/imgui/LICENSE.txt" "$dir/LICENSE.txt"
	fi

	if [ ! -f "$src/backends/imgui_impl_sdl2.cpp" ] ||
	   [ ! -f "$src/backends/imgui_impl_sdlrenderer2.cpp" ] ||
	   [ ! -f "$src/backends/imgui_impl_opengl3.cpp" ]; then
		echo "error: $src/backends is incomplete; remove $dir and retry" >&2
		exit 1
	fi

	if ! grep -q "ImGuiConfigFlags_DockingEnable" "$src/imgui.h"; then
		echo "error: $src is not the docking build required by the project" >&2
		echo "       remove $dir and run 'make deps' again" >&2
		exit 1
	fi

	cp "$ROOT_DIR/scripts/deps/imgui.Makefile" "$dir/Makefile"
	make -C "$dir"
}

build_imgui_file_dialog()
{
	local dir="$ROOT_DIR/third_party/ImGuiFileDialog"

	if [ ! -f "$dir/ImGuiFileDialog.cpp" ]; then
		echo "error: missing vendored ImGuiFileDialog in $dir" >&2
		exit 1
	fi

	make -C "$dir"
}

fetch_doctest()
{
	local dir="$LIB_DIR/doctest"

	if [ ! -f "$dir/doctest.h" ]; then
		echo "Fetching doctest v2.4.12..."
		clone_tag https://github.com/doctest/doctest.git v2.4.12 \
			"$TMP_DIR/doctest"
		mkdir -p "$dir"
		cp "$TMP_DIR/doctest/doctest/doctest.h" "$dir/doctest.h"
		cp "$TMP_DIR/doctest/LICENSE.txt" "$dir/LICENSE.txt"
	fi
}

echo "Dependencies directory: $LIB_DIR"
fetch_imgui
build_imgui_file_dialog
fetch_doctest
echo "Dependencies ready. Run 'make' to build QtreGB."
