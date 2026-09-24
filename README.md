# imgui_rich_md

Rich Markdown for Dear ImGui: headings, lists, tables, code blocks, images, admonitions, LaTeX, wikilinks,
collapsible sections, and *sections and imports* (a source file can carry its own narrative). Works on stock
Dear ImGui 1.92+, with optional backends for syntax-highlighted code (ImGuiColorTextEdit) and LaTeX (MicroTeX).

Extracted from [Dear ImGui Bundle](https://github.com/pthom/imgui_bundle), where it powers the markdown of the demos
and of the Python playground. The library is being set up here. The examples build against stock Dear ImGui:
- `examples/minimal`: the smallest setup
- `examples/tour`: every feature, with its markdown source
- `examples/editor`: a live markdown editor
- `examples/custom_host`: the library on an application's own textures, assets and log

Until this README grows, the API is documented in the bundle's documentation.
