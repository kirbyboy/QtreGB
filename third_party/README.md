# Third-party source

`third_party/` contains third-party source that is tracked in this repository.
`lib/` is different: it contains downloaded/generated dependencies and is
ignored by Git.

## ImGuiFileDialog

`third_party/ImGuiFileDialog` is a vendored copy of ImGuiFileDialog v0.6.8
(MIT), with local changes for runtime UI translations. See its `LICENSE`,
`VERSION.txt` and `LOCAL_CHANGES.md`. The code is not QtreGB code; it is pinned
here until the project decides to update or replace it.

The library is not wired into the QtreGB build yet. When it is, the Makefile in
that directory builds `libImGuiFileDialog.a` against `lib/imgui/src`.
