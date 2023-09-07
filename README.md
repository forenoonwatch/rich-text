# Rich Text
This is a prototype codebase containing the necessary code for displaying and editing rich text in a manner suitable for integrating in game engines, and with unicode support comparable to that which is found in browsers or operating systems.

## Topics to Investigate

### Layout and Rendering
- &check; LTR and RTL line break aware paragraph layout
- &check; Bidirectional text support
- &cross; Vertical text layout, and mixed vertical/horizontal layout
- &check; Per-script font fallbacks
- &check; Common font fallbacks
- &cross; Family fallbacks
- &check; Inline font size and face switching
- &check; icu4c-compatible text run parsing
- &check; Full feature unicode support (Complex scripts, Emoji, Zalgo text, etc)
- &check; Color glyph support
- &check; Dynamic glyph atlas
- &cross; MSDF text rendering
- &cross; Synthetic bold and italic

### Text Input
- &cross; OS text event and Input Method Editor support
- &cross; Clipboard integration and accelerators (Ctrl+X, Ctrl+C, Ctrl+V)
- &cross; Character break aware cursor navigation (mouse, arrow keys)
- &cross; Basic text selection (click and drag, shift+arrow keys)
- &cross; Word, line, and full text aware navigation (Ctrl+arrow keys, Ctrl+A, Home, End, Ctrl+Home, Ctrl+End, n-clicking text area)
- &cross; Undo, Redo (Ctrl+Z, Ctrl+Y)

### Rich Text Markup
- &check; Color
- &cross; Transparency
- &check; Font family
- &check; Font size
- &cross; Font weight
- &cross; Bold
- &cross; Italic
- &check; Underline
- &check; Strikeout
- &check; Stroke color
- &check; Stroke transparency
- &check; Stroke thickness
- &check; Stroke join type
- &cross; Uppercase modifier
- &cross; Lowercase modifier
- &cross; Smallcaps modifier
- &cross; Line break `<br />`
- &cross; XML escape sequences e.g. `&lt;`
- &check; Comment
- &cross; SIMD-accelerated parsing and UTF encoding/decoding

## Dependencies
- [FreeType](https://freetype.org/)
- [GLAD](https://glad.dav1d.de/)
- [GLFW](https://www.glfw.org/)
- [HarfBuzz](https://harfbuzz.github.io/)
- [ICU](https://icu.unicode.org/)
- [icu-le-hb](https://github.com/harfbuzz/icu-le-hb)
- [msdfgen](https://github.com/Chlumsky/msdfgen)
- [simdjson](https://github.com/simdjson/simdjson)

## Resources
- [Text Rendering Hates You](https://faultlore.com/blah/text-hates-you/)
- [Text Editing Hates You Too](https://lord.io/text-editing-hates-you-too/)