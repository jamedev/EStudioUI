# Third-party notices

EStudioUI is MIT licensed. Runtime libraries use permissive licenses allowing
commercial use; the demo fonts use SIL OFL 1.1. Preserve the notices when
redistributing binaries. **This software uses the FreeType project
(https://freetype.org/)** under the FTL alternative of its dual license.

Runtime packages come from `vcpkg.json`, pinned to the official registry's
2025.04.09 baseline `ce613c41372b23b1f51333815feb3edd87ef8a8b`.
Kiwi uses the project overlay in `cmake/ports/kiwi`, pinned to upstream tag
1.4.8, tree `ee80a5e061a61c0ff6d47b7c61ef95d68766b232` and a SHA-512 archive checksum.
It has no binary or transitive dependencies and is private to the implementation.
SheenBidi is pinned to upstream v3.0.0 with the official port archive SHA-512
in `cmake/ports/sheenbidi`; it has no transitive dependencies.
Both Kiwi's BSD license and the bundled AssocVector notice permit commercial use;
its vcpkg copyright file includes both authoritative upstream texts.

| Component | Version at baseline | Selected license | Purpose |
| --- | --- | --- | --- |
| GLFW | 3.4#1 | zlib/libpng | Windows, input, GL contexts |
| FreeType | 2.13.3 | FTL | Unhinted font outlines |
| HarfBuzz | 10.2.0#3 | Old MIT / MIT-Modern-Variant | Text shaping |
| Kiwi | 1.4.8 (project overlay) | BSD-3-Clause and bundled permissive Loki/MIT-style notice | Incremental linear layout solver |
| SheenBidi | 3.0.0 (project overlay) | Apache-2.0 | Unicode bidirectional and script analysis |
| libunibreak | 6.1#1 | zlib and Unicode data notice | Unicode line and word boundaries |
| utf8proc | 2.10.0 | MIT and permissive Unicode data notice | Grapheme boundaries |
| stb_image | stb 2024-07-29#1, upstream `f75e8d1cad7d90d72ef7a4661f1b994ef78b4e31` | MIT alternative | Bounded PNG/JPEG decoding; compiled into EStudioUI |
| nlohmann/json | 3.11.3#1 | MIT | Private header-only JSON parser/serializer for open UI documents |
| SQLite | 3.49.1, pinned vcpkg baseline; omit-load-extension | Public domain (SPDX blessing) | Optional reusable database adapter and browser |
| Generated GLAD loader | glad2 2.0.8, generated GL 3.3 core | CC0-1.0 alternative and Apache-2.0 API definitions | GL entry points |
| Slug reference shader | EricLengyel/Slug, retrieved 2026-10-21 | MIT alternative | Analytic outline coverage |
| Noto Sans | google/fonts/ofl/notosans, retrieved 2026-10-21 | SIL OFL 1.1 | Primary demo font |
| Noto Sans Arabic, Hebrew, Devanagari; Noto Sans Italic | google/fonts commit `45e3d5269e4a17d823f6d84470bc084dc5ae46f6` | SIL OFL 1.1 | Demo fallback and actual italic faces |

All ten vcpkg dependencies disable default features. HarfBuzz does not enable
GLib, ICU, Cairo, Graphite, or platform shapers; FreeType does not enable external
image/compression libraries. The Windows triplet builds static dependencies with
the dynamic MSVC runtime. vcpkg may download build-only tools such as Meson,
Python, CMake, and Ninja; these are not linked into EStudioUI.

Only stb_image's PNG and JPEG decoders are enabled. It adds no binary dependency;
its authoritative dual license is copied from the pinned port with demo builds.
The artwork in `assets/images/` is original procedural EStudioUI demonstration
and pixel-test data, distributed under the repository's MIT license.

The fragment shader in `src/Renderer.cpp` is adapted from Eric Lengyel's Slug
reference pixel shader, copyright (c) 2017 Eric Lengyel. Changes include GLSL
3.30 syntax, linear texture-buffer addresses, a unified shape/glyph instance
stream, axis-aligned quad expansion, and UI clipping. Original shaders and
upstream licenses are retained in `third_party/slug-reference`. The band builder
and widget toolkit are new code; the commercial Slug SDK is not required.

GLAD's generated code declares `(WTFPL OR CC0-1.0) AND Apache-2.0`; the CC0
alternative is selected. Full terms are in `licenses/CC0-1.0.txt` and
`licenses/Apache-2.0.txt`; the generator's MIT notice is also retained.

`licenses/` contains redistribution notices. The demo's post-build step copies
these, the repository's MIT license, and each installed vcpkg port's authoritative
`share/<port>/copyright` into its adjacent licenses directory. Dependency sources
are obtained by vcpkg rather than copied into this repository. Bundled font,
loader, and reference shader checksums are recorded in `third_party/SOURCES.sha256`.

Host C/C++ runtimes, operating-system frameworks, X11 libraries, and graphics
drivers are supplied by the OS/toolchain and retain their own terms.

The additional demo/test fonts are fetched by `cmake/ControlFonts.cmake` at the
pinned google/fonts revision above, with a SHA-256 per file. They are copied next
to the demo with their OFL notices. Library-only builds do not fetch these fonts.
For offline builds, prepopulate `ESTUDIOUI_CONTROL_FONT_DIR` with these exact files.

SQLite is dedicated to the public domain, including commercial use. Its blessing
notice is retained in `licenses/sqlite3-vcpkg-copyright.txt`; the browser also
stages the installed port notice beside its executable.
