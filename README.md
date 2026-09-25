# imgui_rich_md

Rich Markdown for [Dear ImGui](https://github.com/ocornut/imgui): text, tables, code, images and LaTeX formulas,
rendered inside your ImGui windows, between your widgets. Works on stock Dear ImGui 1.92+ (C++17).

**[Try the examples in your browser](https://pthom.github.io/imgui_rich_md/)**

Part of the [Dear ImGui Bundle](https://github.com/pthom/imgui_bundle) family: in Python, it comes with
`pip install imgui-bundle`, as `imgui_bundle.rich_md` (see [Python](#python)).

## Quickstart

CMake, in a project that has a target named `imgui`:

```cmake
include(FetchContent)
FetchContent_Declare(imgui_rich_md GIT_REPOSITORY https://github.com/pthom/imgui_rich_md.git GIT_TAG main)
FetchContent_MakeAvailable(imgui_rich_md)
target_link_libraries(my_app PRIVATE imgui_rich_md)
```

C++:

```cpp
#include "imgui_rich_md/rich_md.h"

RichMd::InitializeMarkdown();          // once, after the ImGui backends are initialized

ImGui::Begin("Doc");                   // each frame, anywhere in your UI
RichMd::Render("# Hello\nSome *markdown*, a [link](https://github.com/pthom/imgui_rich_md) and `code`.");
ImGui::End();

RichMd::DeInitializeMarkdown();        // before the backends shut down
```

[examples/minimal](examples/minimal/main.cpp) is a complete program (GLFW + OpenGL3).

## Features

| | |
|---|---|
| Text | headings, emphasis, strikethrough, `<u>`, `<mark>`, `<kbd>`, `<sub>`/`<sup>`, links and autolinks, lists, quotes |
| Tables | column alignment, resizable columns |
| Code | inline code; code blocks with a copy button, and syntax highlighting with the code editor option |
| Images | from files or assets, from URLs with the download option, sized with `<img width=...>`, a spinner while loading |
| LaTeX | inline `$...$` and display `$$...$$`, with the LaTeX option |
| Mermaid | flowcharts, sequence and class diagrams in ` ```mermaid ` blocks, drawn natively with `ImDrawList` (no web view); [what is supported](docs/mermaid.md) |
| Structure | GitHub admonitions (`> [!NOTE]`), task lists, collapsible sections (`<details>`), `<center>` |
| Callbacks | links, wikilinks (`[[target]]`), headings (tables of contents), your own renderer for a fenced block language |
| Sections and imports | a source file carries markdown blocks (`@@md#Name` ... `@@/md`) that a document imports with `@import`: a program can be its own narrative |
| Fonts | Roboto and Inconsolata included; CJK, emoji or icon fonts merged into every markdown font |
| Hosts | textures, assets, downloads and logging go through replaceable services (`RichMd::HostServices`) |

The [tour](https://pthom.github.io/imgui_rich_md/tour.html) shows each feature with its markdown source.

## Build options

| CMake option | Default | |
|---|---|---|
| `IMGUI_RICHMD_WITH_CODE_EDITOR` | OFF | syntax highlighting ([ImGuiColorTextEdit](https://github.com/goossens/ImGuiColorTextEdit): fetched, or `IMGUI_RICHMD_COLOR_TEXT_EDIT_DIR`) |
| `IMGUI_RICHMD_WITH_LATEX` | OFF | formulas ([MicroTeX](https://github.com/NanoMichael/MicroTeX): fetched, or `IMGUI_RICHMD_MICROTEX_DIR`; needs FreeType: a `freetype` target, or `find_package(Freetype)`) |
| `IMGUI_RICHMD_WITH_DOWNLOAD_IMAGES` | OFF | images from URLs (libcurl on desktop, the browser's fetch with Emscripten) |
| `IMGUI_RICHMD_WITH_MERMAID` | ON | Mermaid diagrams: flowcharts, sequence and class diagrams (no dependency) |
| `IMGUI_RICHMD_EMBED_ASSETS` | ON | the fonts and images are embedded in the library; OFF: they are read from the assets folder (`RichMd::SetAssetsFolder`) |
| `IMGUI_RICHMD_STB_IMAGE_IMPLEMENTATION` | ON | compiles stb_image; set it OFF if your project already does (e.g. HelloImGui) |
| `IMGUI_RICHMD_BUILD_EXAMPLES` | top-level only | builds the examples |
| `IMGUI_RICHMD_BUILD_TESTS` | OFF | builds the smoke test (`ctest`) |

## Examples

| | | |
|---|---|---|
| minimal | the smallest setup: where the library's calls go in your own loop | [run](https://pthom.github.io/imgui_rich_md/minimal.html), [source](examples/minimal/main.cpp) |
| tour | every feature, with its markdown source | [run](https://pthom.github.io/imgui_rich_md/tour.html), [source](examples/tour/main.cpp) |
| editor | the markdown source next to its live render | [run](https://pthom.github.io/imgui_rich_md/editor.html), [source](examples/editor/main.cpp) |
| mermaid | Mermaid diagrams, each with its source to edit and a link to Mermaid's own render | [run](https://pthom.github.io/imgui_rich_md/mermaid.html), [source](examples/mermaid/main.cpp) |
| custom_host | the library on an application's own textures, assets and log | [run](https://pthom.github.io/imgui_rich_md/custom_host.html), [source](examples/custom_host/main.cpp) |
| fonts | CJK, emoji and icon fonts merged into every markdown font | [run](https://pthom.github.io/imgui_rich_md/fonts.html), [source](examples/fonts/main.cpp) |

They build against stock Dear ImGui, fetched by CMake: `cmake -S examples -B build && cmake --build build`
(with Emscripten: `emcmake cmake -S examples -B build_web`, one page per example).

## API

The public API is in [imgui_rich_md/rich_md.h](imgui_rich_md/rich_md.h) (initialization, rendering, options, callbacks,
fonts) and, for applications that provide their own textures, assets or downloads,
[imgui_rich_md/rich_md_host.h](imgui_rich_md/rich_md_host.h).

## Python

The library is part of [Dear ImGui Bundle](https://github.com/pthom/imgui_bundle) (`pip install imgui-bundle`):

```python
from imgui_bundle import immapp, rich_md

def gui():
    rich_md.render("# Hello, *markdown*")

immapp.run(gui, with_markdown=True)   # with_latex=True for the formulas
```

Try it without installing anything, in the [Dear ImGui Bundle playground](https://imgui-bundle.pages.dev/playground/).

## Credits

- The renderer started as [imgui_md](https://github.com/mekhontsev/imgui_md) by Dmitry Mekhontsev, and was extensively
  rewritten.
- [md4c](https://github.com/mity/md4c) by Martin Mitáš parses the markdown.
- [MicroTeX](https://github.com/NanoMichael/MicroTeX) lays out the formulas.
- [ImGuiColorTextEdit](https://github.com/goossens/ImGuiColorTextEdit) (by BalazsJako, rewritten by Johan A. Goossens)
  highlights the code.
- [stb_image](https://github.com/nothings/stb) by Sean Barrett decodes the images; the fonts are Roboto, Inconsolata and
  Latin Modern Math.

## License

MIT (see [LICENSE](LICENSE)); the third-party components are listed in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
