# Changelog

# v1.0.0 - WIP

The first release of imgui_rich_md as a library of its own: rich Markdown for Dear ImGui, with text you can select and search, tables, code, images, LaTeX math and Mermaid diagrams, rendered inside your ImGui windows, between your widgets. It works with a stock Dear ImGui 1.92+ (C++17). The [README](README.md#features) lists its features.

## Where it comes from

- The renderer started as [imgui_md](https://github.com/mekhontsev/imgui_md) by Dmitry Mekhontsev.
- It grew inside [Dear ImGui Bundle](https://github.com/pthom/imgui_bundle), as its markdown renderer, from 2022.
- It now lives here. Dear ImGui Bundle 1.93 uses it as a library, and brings it to Python (`imgui_bundle.rich_md`).

## Highlights

- **Selectable text**: select with the mouse, copy with Ctrl+C or Cmd+C. A double click selects a word, a triple click a paragraph. A right click opens a menu: Copy, Copy as Markdown, Select All, Copy Link.
- **Documents**: `BeginDocument()` / `EndDocument()`, or `RenderDocument()`, show a long text with a table of contents, a search (Ctrl+F or "/"), anchor links and foldable headings.
- **Mermaid diagrams**: flowcharts, sequence and class diagrams, drawn natively with `ImDrawList`.
- **Widgets in the text**: `RegisterFencedBlockRenderer("lang", fn)` draws a fenced block of that language.
- **Narrative programming**: a source file carries its own narrative, which `RICHMD_RENDER_THIS_FILE("Name")` renders.
- **Hosts**: textures, assets, downloads and logging go through replaceable services (`RichMd::HostServices`).
- **Touch screens**: with `ImGuiConfigFlags_IsTouchScreen`, larger targets for a finger, and a button for the search.

## Coming from Dear ImGui Bundle's former renderer

- C++: the API is `RichMd::`, in `imgui_rich_md/rich_md.h`. In the bundle, the former include `imgui_md_wrapper/imgui_md_wrapper.h` and the namespaces `ImGuiMd` and `ImGuiMicroTeX` stay, as aliases.
- C++, breaking: the renderer class `imgui_md` (md4c's client) is internal.
- Behavior change: `Render()` removes the common indentation of the text, and resolves its transclusions. `RenderRaw()` renders the text as is.
- Python: the module is `imgui_bundle.rich_md`. `imgui_bundle.imgui_md` is the same module.
