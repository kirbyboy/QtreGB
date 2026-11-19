# Third-Party Notices

QtreGB uses third-party components. This document collects their copyright
notices and licenses. It does not modify the original licenses: each component
remains governed by its own license.

The third-party licenses are not affected by the QtreGB source-code license
(`LICENSE.txt`, GPL-3.0-or-later) or by `TRADEMARK.md`. The project-original
visual assets, including the application icon, are covered by `assets/LICENSE`
(CC0 1.0).

## zlib-licensed components

### SDL2

Simple DirectMedia Layer
Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

```text
This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

## MIT-licensed components

The following text applies to the components listed below.

```text
MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Dear ImGui

Copyright (c) 2014-2025 Omar Cornut

Used for the user interface.

### ImGuiFileDialog

ImGuiFileDialog v0.6.8 (commit `ee77f190861193a995435c388682f1512f6fd29a`)
Copyright (c) 2018-2025 Stephane Cuillerdier (aka Aiekick)
<https://github.com/aiekick/ImGuiFileDialog>

Used for the file open/save dialogs. Vendored in
`third_party/ImGuiFileDialog`, with local changes for runtime UI translations.
It is not QtreGB code; see `third_party/ImGuiFileDialog/LOCAL_CHANGES.md` and
the MIT text above.

### dirent

Copyright (c) 1998-2019 Toni Ronkko

Included by ImGuiFileDialog.

### stb

stb_image v2.29
Copyright (c) 2017 Sean Barrett

stb_image_resize2 v2.06
Copyright (c) 2017 Sean Barrett

stb_image_write v1.16
Copyright (c) 2017 Sean Barrett

stb_image and stb_image_resize2 are included by ImGuiFileDialog for image
loading and resizing. stb_image_write is used by QtreGB to embed the preview
image in savestate files.

### doctest

doctest v2.4.12
Copyright (c) 2016-2025 Viktor Kirilov

Used only by the tests; it is not distributed in the application binary.

## BSD-3-Clause-licensed components

### GLEW

OpenGL Extension Wrangler Library 2.2.0
Copyright (C) 2002-2008 Milan Ikits
Copyright (C) 2002-2008 Marcelo E. Magallon
Copyright (C) 2002 Lev Povalahev

```text
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
* The name of the author may not be used to endorse or promote products
  derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Note

If a new dependency is added, its copyright notice, license and any attribution
requirements must be added here before distributing the program.
