# Control text and fonts (0.17, F10a)

The ordinary path still uses `Label`, `Button` and `TextBox`. Register additional
fonts in `WindowOptions::m_Fonts` before creating the window; typography inherits
through CSS. See the compiled [example](examples/ControlText.cpp) and the demo's
**Text & fonts** tab. Demo/test fonts are fetched at configure time with pinned
revision and SHA-256 checks; prepopulate `ESTUDIOUI_CONTROL_FONT_DIR` for offline
builds. Library-only builds do not fetch them. No application shaping or platform-message code is needed.

```cpp
options.m_Fonts.push_back({"fonts/NotoSansArabic.ttf"});
options.m_Fonts.push_back({"fonts/NotoSansHebrew.ttf"});
options.m_Fonts.push_back({"fonts/NotoSans.ttf", {}, 700});
options.m_Fonts.push_back({"fonts/NotoSansItalic.ttf", {}, 400, true});
```

CSS supports inherited `font-family` (ordered quoted/unquoted family names),
`font-size`, `font-weight` (normal/bold or 1..1000), `font-style` (normal/italic),
`direction` (auto/ltr/rtl), and `language` (BCP-47 tag). Font names match registered
metadata case-insensitively; sans-serif resolves the primary face. Registration
order breaks equivalent matches and supplies deterministic fallback. A fallback
must cover an entire grapheme. Unsupported graphemes use the primary face's
missing-glyph outline and increment `TextLayout::m_MissingGlyphs`; there is no
silent system-font discovery. Register real italic sources. Variable wght axes
are applied to both FreeType outlines and HarfBuzz; static faces use their actual
outlines rather than synthetic bold/italic effects.

SheenBidi resolves Unicode paragraph embedding levels, isolates, mirroring and
visual runs. HarfBuzz shapes each script/font/direction segment with line context.
libunibreak supplies Unicode word and soft-line opportunities. Representative
Latin/Greek/Cyrillic, Arabic, Hebrew and Devanagari run through the delivered font
set; other scripts require suitable registered fonts and testing. This is a control
text service, not a claim of complete document-editor behavior or emoji rendering.

TextBox storage and selections remain logical UTF-8 grapheme boundaries. Pointer
hit testing and Left/Right operate in visual order; Ctrl+Left/Right use Unicode
word boundaries, Home/End use paragraph visual edges. Bidi boundaries retain
caret affinity. Required shaping remains enabled; optional ligatures are disabled
for editing. At coincident bidi boundaries, pointer hit testing chooses the edge
of the grapheme on the pointed side, rather than another run's logical endpoint.
This uses the existing sorted caret/span indices without allocation or reshaping.
The highlight still follows the logical UTF-8 range and may be visually disjoint.
GDEF ligature caret positions are used when present, with evenly
partitioned grapheme advances as a deterministic fallback. Selection may produce
several painted rectangles. UIA text ranges report those clipped rectangles and
logical visible intervals. Clipboard, validators, undo, F09 shortcut/AltGr rules
and the F11 composition protocol share this canonical editor.

Labels normalize explicit display paragraph separators and optionally wrap with
`text-wrap: wrap` / `SetWordWrap(true)`. Measurement and drawing use the same shaped
lines; wrapped lines preserve their paragraph's base direction. Fonts operate in
logical pixels; the renderer scales analytic outlines to framebuffer DPI without
creating a separate face per scale. TextBox continues to sanitize line separators
and tabs to spaces and is bounded to 1 MiB of complete UTF-8 graphemes. Multiline
storage, paragraph editing, rich documents and publishing remain F10b.

FontFace and registrations are owner-thread services. `AddFont`/`RemoveFont` are
idle-boundary operations when attached; all attached contexts retire widget text
caches and invalidate measurement without reparsing CSS. A retained TextLayout
keeps its actual native faces alive after registration removal. GPU glyph keys
include lifetime-unique actual-face identity and glyph ID, including variation
instances and collection indices. CPU outline references expire on eviction;
retain a TextLayout rather than a borrowed GlyphGeometry reference.

Default hard limits are 32 live faces (including retired retained faces), 128 MiB
aggregate font-file storage, 64 MiB per file, 16 MiB cached CPU geometry, and 4 MiB /
256 entries in the shared exact-input shape cache. A registered weight currently
owns its file bytes independently. Caption caches remain 512 entries / 8 MiB per
scroll control. Single shaping inputs are at most 1 MiB; script/font/bidi work and
wrapped output have explicit 4,096-run/line bounds. Cold shaping/registry resolution
is proportional to input and registered faces; cached captions avoid reshaping.
Wrapping can reshape candidate lines and is bounded rather than constant-time.
`GetStats` exposes admission and cache counters; `ClearCaches` retires CPU caches.

Renderer font buffers default to 32 MiB and 65,536 admitted font/glyph pairs.
`SetFontCacheLimit` and `ClearFontCache` are between-frame operations. Registry
changes retire GPU font storage at the next BeginFrame; the retained paint history
is invalidated. Overflow is diagnosed before submission rather than growing an
unbounded atlas. Other renderer/resource budgets remain feature-driven F12 work.

Native installed-language IME, assistive technology, mixed-monitor DPI and hardware
performance require the separate [acceptance checklist](verification/PLATFORM_ACCEPTANCE.md).
Automated protocol, framebuffer and native-provider evidence does not substitute
for those human sessions.
