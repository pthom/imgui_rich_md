<!-- Generated from api.src.md by narrative_resolve: do not edit -->
# imgui_rich_md: API

The public API is in [rich_md.h](../imgui_rich_md/rich_md.h), with
[narrative_programming.h](../imgui_rich_md/narrative_programming.h) (the resolver of transclusions, without ImGui) and
[rich_md_host.h](../imgui_rich_md/rich_md_host.h) (for the hosts that provide their own textures, assets or downloads).
In Python, the same API is in snake_case:
[rich_md.pyi](https://github.com/pthom/imgui_bundle/blob/main/bindings/imgui_bundle/rich_md.pyi) in Dear ImGui Bundle.

This page is assembled from the headers by [narrative programming](narrative_programming/narrative_programming.md):
its source is [api.src.md](api.src.md).

## Rendering

`Render()` draws a markdown string where it is called, every frame: rich_md is immediate mode, like Dear ImGui.
It needs a context (see Contexts).

```cpp
// Renders a markdown string. Its common indentation is removed first (so that a string written
// inside an indented function renders as expected; no-op on flush-left text), then its transclusions
// are resolved (see ResolveTransclusions; the files are read through the host's ReadAsset).
void Render(const std::string& markdownString);
// Renders a markdown string as is (no unindent, no transclusion)
void RenderRaw(const std::string& markdownString);
```

## Contexts

A context holds the options, the fonts and the caches (textures, formulas, diagrams). Most applications have one.

```cpp
struct Context;
// Contexts: CreateContext makes one, any time after ImGui::CreateContext(); it becomes the current context when
// there is none. The fonts load at the first Render() (Dear ImGui 1.92 loads glyphs on demand).
// DestroyContext destroys one (nullptr: the current one) and frees its textures: call it while the rendering
// backend is still alive. Several contexts (e.g. two font sizes, several ImGui contexts) can live together; all
// the other functions act on the current one. In ImGui Bundle, ImmApp makes one for you when markdown is enabled.
Context* CreateContext(const MarkdownOptions& options = MarkdownOptions());
void DestroyContext(Context* context = nullptr);
void SetCurrentContext(Context* context);
Context* GetCurrentContext();


// The folder where the default host reads the assets (fonts, images) from the file system,
// when they are not embedded in the binary. Default: the current directory.
void SetAssetsFolder(const std::string& folder);
```

## Options and callbacks

The options of a context: its fonts, LaTeX, links, and the callbacks through which the application handles links,
images, HTML tags and headings.

```cpp
struct MarkdownFontOptions
{
    std::string fontBasePath = "fonts/Roboto/Roboto";
    // This size is in density-independent pixels
    float regularSize = 16.f;

    // Multipliers for header sizes, from h1 to h6
    float headerSizeFactors[6] = { 1.42f, 1.33f, 1.24f, 1.15f, 1.10f, 1.05f };

    // Fonts merged into every markdown font (asset paths), e.g. an icon or a CJK font.
    // Since Dear ImGui 1.92 glyphs are loaded on demand, so merging a large font costs nothing until it is used.
    std::vector<std::string> mergeFonts;
};


struct MarkdownImage
{
    ImTextureID	texture_id;
    ImVec2	size;
    ImVec2	uv0;
    ImVec2	uv1;
    ImVec4	col_tint;
    ImVec4	col_border;
};

using VoidFunction = std::function<void(void)>;
using StringFunction = std::function<void(std::string)>;
using HtmlDivFunction = std::function<void(const std::string& divClass, bool openingDiv)>;
using HtmlSpanFunction = std::function<bool(const std::string& tagName, bool opening)>;
using MarkdownImageFunction = std::function<std::optional<MarkdownImage>(const std::string&)>;


std::optional<MarkdownImage> OnImage_Default(const std::string& image_path);
void OnOpenLink_Default(const std::string& url);


struct MarkdownCallbacks
{
    // The default version will open the link in a browser iif it starts with "http"
    StringFunction OnOpenLink = OnOpenLink_Default;

    // The default version will load the image as a cached texture and display it
    MarkdownImageFunction OnImage = OnImage_Default;

    // OnHtmlDiv does nothing by default, by you could write:
    //     In  C++:
    //        markdownOptions.callbacks.onHtmlDiv = [](const std::string& divClass, bool openingDiv)
    //        {
    //            if (divClass == "red")
    //            {
    //                if (openingDiv)
    //                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
    //                else
    //                    ImGui::PopStyleColor();
    //            }
    //        };
    //     In  Python:
    //        def on_html_div(div_class: str, opening_div: bool) -> None:
    //            if div_class == 'red':
    //                if opening_div:
    //                    imgui.push_style_color(imgui.Col_.text.value, imgui.ImColor(255, 0, 0, 255).value)
    //                else:
    //                    imgui.pop_style_color()
    //        md_options = rich_md.MarkdownOptions()
    //        md_options.callbacks.on_html_div = on_html_div
    //        immapp.run(
    //            gui_function=gui, with_markdown_options=md_options #, more options here
    //        )
    HtmlDivFunction OnHtmlDiv;

    // OnHtmlSpan: optional callback for inline HTML tags encountered in
    // markdown text (one call per open/close tag; e.g. "sub", "sup",
    // "kbd", "mark", or any custom tag).
    // Return true to indicate the tag was fully handled, false to let
    // the built-in renderer apply its default rendering (if any).
    // Example (C++):
    //   callbacks.OnHtmlSpan = [](const std::string& tag, bool opening) {
    //       if (tag == "small") {
    //           // ... push/pop a smaller font
    //           return true;
    //       }
    //       return false;
    //   };
    HtmlSpanFunction OnHtmlSpan;

    // Deprecated: the download function is HostServices::Download (Python: rich_md.set_download_function).
    // When set here, it is copied into the host services when the context is created.
    MarkdownDownloadFunction OnDownloadData;

    // CanUseChildWindows: callback that tells whether child windows can be used at this moment (empty by default,
    // which means yes).
    // Code blocks are rendered inside a child window. Where child windows do not work (e.g. inside the canvas of
    // imgui-node-editor), return false: code blocks are then rendered as inline code.
    // ImmApp fills it when the node editor is available.
    std::function<bool()> CanUseChildWindows;

    // OnWikiLink: a wikilink [[target]] or [[target|label]] was clicked. Wikilinks are parsed only
    // when this callback is set.
    std::function<void(const std::string& target)> OnWikiLink;

    // OnHeading: called after each heading is rendered, with its level (1 to 6) and its text
    // (without markup): for a table of contents, or scrolling to an anchor.
    std::function<void(int level, const std::string& text)> OnHeading;
};


struct MarkdownOptions
{
    MarkdownFontOptions fontOptions;
    MarkdownCallbacks callbacks;

    // Enable native LaTeX math rendering via MicroTeX.
    // When true, $...$ and $$...$$ in markdown will be rendered as math formulas
    // (requires building with IMGUI_RICHMD_WITH_LATEX=ON; in the bundle it is the default
    // when IMGUI_BUNDLE_WITH_MICROTEX and FreeType are both available).
    // When false, $ is rendered as a literal character (legacy behavior).
    bool withLatex = false;

    // Recognize bare URLs, email addresses and www.* as clickable links
    // without requiring <...> or []() syntax
    // (MD_FLAG_PERMISSIVEAUTOLINKS — URL + email + WWW).
    // Set to false to get strict CommonMark link behavior.
    bool autolinks = true;

    // A newline in the source is a line break (as in GitHub comments and chat messages)
    bool hardSoftBreaks = false;
};
```

## Narrative programming

A program can carry its own narrative: markdown sections and code regions in its comments and strings, that
markdown transcludes (`![[file.cpp#Name]]`). See the
[presentation](https://github.com/pthom/imgui_rich_md/blob/main/docs/narrative_programming/narrative_programming.md)
and the
[specification](https://github.com/pthom/imgui_rich_md/blob/main/docs/narrative_programming/narrative_programming_spec.md).

A source file (C, C++, GLSL, JavaScript, Python) names parts of itself with annotations:
```cpp
// ::md Area                    a markdown section, in line comments (// or #)
// The area of a *circle*.
// ::code                      its associated code
float Area(float r) { return 3.14159f * r * r; }
// ::endcode                   closes the code and the section

// ::md Intro                   a section without code is closed by ::endmd
// # Circles
// ::endmd

// ::code Main loop             a named code region (regions may nest)
for (int i = 0; i < n; ++i) {}
// ::endcode
```
A section may also be a block comment or a Python string (triple quotes) whose first line is `::md Name` after
the opener: it ends with the comment or the string, or continues into the code after it when its last line is
`::code`.

A markdown text transcludes them with an embed alone on its line (Obsidian's syntax):
```text
![[circle.cpp#Area]]            the prose of a section
![[circle.cpp#Area#code]]       its associated code, as a code block
![[circle.cpp#Main loop]]       a code region
![[#Area]]                      a section of the current file: a source file can be its own narrative
![[circle.cpp]]                 a whole source file, as code
![[notes.md]]                   a markdown document, whole...
![[notes.md#Setup#Linux]]       ...or under a heading (Linux, under Setup)
```
Paths are relative to the file holding the embed (a text without a file: the assets, then the file system).
Transclusions resolve recursively. An error (a missing file or name, a malformed annotation, a cycle)
renders the embed in the error color, with the reason as a tooltip.

```cpp
// Reads a text file for ResolveTransclusions, or returns std::nullopt when it does not exist
using ReadTextFile = std::function<std::optional<std::string>(const std::string& path)>;

// ResolveTransclusions replaces the embeds of a markdown text: readFile reads a file (or returns std::nullopt);
// currentFile is the file the text comes from, if any. Render() calls it with the host's ReadAsset.
std::string ResolveTransclusions(const std::string& markdown, const ReadTextFile& readFile,
                                 const std::string& currentFile = "");
```

`RenderFile()` renders a section, a code region or a whole file: a program renders its own narrative with
`RICHMD_RENDER_THIS_FILE`. The syntax and the resolver: `narrative_programming.h`.

```cpp
// Renders ![[path#target]]: target is a section ("Intro"), its code ("Escape#code"), a code region, a heading
// of a markdown document, or empty (the whole file). The path is looked up in the assets first, then on the
// file system: a source file renders its own narrative with RICHMD_RENDER_THIS_FILE("Intro") (C++) or
// rich_md.render_this_file("Intro") (Python).
void RenderFile(const std::string& path, const std::string& target = "");
#define RICHMD_RENDER_THIS_FILE(target) RichMd::RenderFile(__FILE__, target)
```

## Extensions

Your own renderers for fenced code blocks, Mermaid diagrams and links outside of markdown.

```cpp
// Renders the code blocks of a given language (```mermaid, ```csv, ...) with your own function,
// instead of the code block renderer. Applies to the current context.
void RegisterFencedBlockRenderer(const std::string& language,
                                 std::function<void(const std::string& code)> renderer);

// Renders a Mermaid diagram (flowchart, sequence or class diagram), as ```mermaid blocks do. A diagram that
// cannot be parsed is shown as code, with the error below it; so is any diagram when the library is built
// without IMGUI_RICHMD_WITH_MERMAID.
// Limitations: a subset of Mermaid, for small and medium diagrams, with its own layout (not a copy of
// mermaid.js) and the colors of the ImGui style. The other diagram types, styles (classDef, style), click,
// themes, front matter and markdown in labels are not supported. Details: docs/mermaid.md in imgui_rich_md.
void RenderMermaid(const std::string& source);

// Renders a link with the given text and url. Can be used outside of markdown rendering.
void RenderTextAsLink(const char* text, const char* url);
```

## Style and fonts

The colors, spacing and fonts of the current context, for widgets drawn next to the markdown.

```cpp
// Note: Since v1.92, Fonts can be displayed at any size:
// in order to display a font at a given size, we need to call
//   ImGui::PushFont(font, size) (or call separately ImGui::PushFontSize)
struct SizedFont
{
    ImFont* font;
    float size;
};

// Colors and spacing. A color with a negative alpha is automatic: derived from the ImGui style
// at render time, so theme changes are followed. Gaps and scales are relative to the font size.
struct Style
{
    ImVec4 linkColor = ImVec4(0, 0, 0, -1);            // automatic: the text color shifted to blue
    ImVec4 linkUnderline = ImVec4(0, 0, 0, -1);        // automatic: ImGuiCol_Button
    ImVec4 linkUnderlineHovered = ImVec4(0, 0, 0, -1); // automatic: ImGuiCol_ButtonHovered
    ImVec4 codeColor = ImVec4(0, 0, 0, -1);            // automatic: the text color, a little more blue
    // errorColor: an invalid formula, a failed transclusion (<md-error>)
    ImVec4 errorColor = ImVec4(0, 0, 0, -1);           // automatic: the Caution admonition color
    ImVec4 quoteBar = ImVec4(0, 0, 0, -1);             // automatic: ImGuiCol_TextDisabled
    ImVec4 kbdBorder = ImVec4(0, 0, 0, -1);            // automatic: ImGuiCol_Border
    ImVec4 markBackground = ImVec4(245.f / 255.f, 205.f / 255.f, 60.f / 255.f, 120.f / 255.f);
    // Note, Tip, Important, Warning, Caution (label and bar)
    ImVec4 admonitionColors[5] = {
        ImVec4(0.35f, 0.65f, 1.00f, 1.0f), ImVec4(0.25f, 0.73f, 0.32f, 1.0f), ImVec4(0.82f, 0.60f, 0.97f, 1.0f),
        ImVec4(0.95f, 0.75f, 0.22f, 1.0f), ImVec4(0.97f, 0.32f, 0.29f, 1.0f) };
    float blockGap = 0.3f;              // vertical gap between blocks
    float headerGapStep = 0.12f;        // extra gap above a header: (7 - level) * headerGapStep
    float subSupScale = 0.7f;           // font size of <sub> and <sup>
    float quoteBarThickness = 2.0f;     // pixels
    float admonitionBarThickness = 3.0f;
    bool linkTooltip = true;            // show the url when hovering a link
    // Space above and below each rendered fragment (a print() call), in em: fragments stack
    // with widgets between them. Bottom: negative = automatic (one ImGui::NewLine()).
    float fragmentGapTop = 0.0f;
    float fragmentGapBottom = -1.0f;
};

// The colors and spacing of the current context. C++ only.
Style& GetStyle();

SizedFont GetCodeFont();

struct MarkdownFontSpec
{
    bool italic = false;
    bool bold = false;
    int headerLevel = 0;  // 0 means no header, 1 means h1, 2 means h2, etc.

    MarkdownFontSpec(bool italic_ = false, bool bold_ = false, int headerLevel_ = 0) :
        italic(italic_), bold(bold_), headerLevel(headerLevel_) {}
};
SizedFont GetFont(const MarkdownFontSpec& fontSpec);

ImVec4 LinkColor();
```

## Capabilities

What this build and its host provide (available once a context exists).

```cpp
bool HasLatex();         // $...$ and $$...$$ rendered as formulas (else shown as their source)
bool HasUrlImages();     // images downloaded from http(s) urls
bool HasCodeEditor();    // code blocks with syntax highlighting (else plain monospaced blocks)
```

## Host services

What the markdown renderer needs from the application or the framework that hosts it (GPU textures, asset files,
logging). Every service is optional and has a plain default; a host (e.g. ImGui Bundle with HelloImGui) installs
richer ones with `SetHostServices()` before `CreateContext()`. Only the download types are part of the Python API.

### Types and defaults

The types the services exchange (textures, formula bitmaps, asset files), and the default services, which a
host may call from its own.

```cpp
// A GPU texture owned by the markdown caches (images, LaTeX).
// keepAlive owns the GPU resource: the texture is freed when the last copy is dropped
// (e.g. when the caches are cleared by DestroyContext).
struct MarkdownTexture
{
    ImTextureRef ref;                // a backend id, or an ImTextureData the backend creates at the next frame
    ImVec2 size = ImVec2(0.f, 0.f);
    std::shared_ptr<void> keepAlive; // releases the GPU texture with the last copy

    bool Valid() const { return ref._TexData != nullptr || ref._TexID != ImTextureID_Invalid; }
};

// A formula rendered to pixels (see HostServices::RenderLatex)
struct LatexBitmap
{
    std::vector<uint8_t> rgba;   // width * height * 4 bytes
    int width = 0, height = 0;
    int baselineY = 0;           // from the top of the bitmap to the text baseline, in pixels
    // set (with no pixels) when the formula is invalid: the source is shown with this message
    std::string error;
};

// The default UploadRgba: an ImTextureData registered with Dear ImGui, created by the rendering backend at
// the next frame (backends with ImGuiBackendFlags_RendererHasTextures, Dear ImGui 1.92+; an invalid
// texture otherwise). A host may call it from its own UploadRgba.
MarkdownTexture UploadRgbaDefault(const unsigned char* rgba, int w, int h);

// The content of an asset file, or std::nullopt when it does not exist
using AssetBytes = std::optional<std::vector<uint8_t>>;

// An asset embedded in the binary (IMGUI_RICHMD_EMBED_ASSETS, see cmake/imgui_richmd_embed_files.cmake)
struct EmbeddedAsset
{
    const char* path;
    const unsigned char* data;   // a gzip stream of the file
    size_t size;                 // of the file
    size_t compressedSize;       // of data
};

// The default ReadAsset: the embedded assets, then the file system under the assets folder
AssetBytes ReadAssetDefault(const std::string& assetPath);
```

### Downloads

The download of URL images: its result, and the function that makes it (`HostServices::Download`).

```cpp
// Status of a download (see HostServices::Download)
enum class MarkdownDownloadStatus {
    NotStarted,   // Download has not been initiated
    Downloading,  // Download is in progress (show placeholder)
    Ready,        // Download complete, data is available
    Failed        // Download failed, errorMessage has details
};

// Result of a download attempt
struct MarkdownDownloadResult {
    MarkdownDownloadStatus status = MarkdownDownloadStatus::NotStarted;
    std::vector<uint8_t> data;       // Only valid if status == Ready
    std::string errorMessage;        // Only valid if status == Failed

    // Fill data from a raw buffer (convenience for C++ users)
    void FillFromData(const void* buffer, size_t size) {
        data.assign(static_cast<const uint8_t*>(buffer), static_cast<const uint8_t*>(buffer) + size);
    }
};

using MarkdownDownloadFunction = std::function<MarkdownDownloadResult(const std::string& url)>;
```

### The services

Every field is optional: an empty one keeps its default. Set them before `CreateContext()`.

```cpp
struct HostServices
{
    // Uploads an RGBA8 buffer (w * h * 4 bytes, no padding) to a GPU texture.
    // Return an invalid MarkdownTexture on failure. If empty, images and LaTeX are skipped.
    std::function<MarkdownTexture(const unsigned char* rgba, int w, int h)> UploadRgba;

    // Reads an asset file (fonts, images: "fonts/Roboto/Roboto-Regular.ttf", "images/x.png").
    // Return std::nullopt when the asset does not exist. Default: the file system.
    std::function<AssetBytes(const std::string& assetPath)> ReadAsset;

    // Fonts merged into every markdown font (in addition to MarkdownFontOptions::mergeFonts),
    // e.g. the host's icon font. Evaluated when the fonts are loaded. Default: none.
    std::function<std::vector<std::string>()> DefaultMergeFonts;

    // Renders a code block (fenced or indented). Default: a read-only editor with syntax highlighting
    // when built with IMGUI_RICHMD_WITH_CODE_EDITOR, else monospaced text in a frame, with a copy button.
    std::function<void(const std::string& code, const std::string& language)> RenderCodeBlock;

    // Renders a LaTeX formula (without its $ delimiters) to an RGBA bitmap. fontSizePx is in physical
    // pixels; displayStyle is true for $$...$$. Return std::nullopt when LaTeX is not available, or a
    // bitmap with only `error` set when the formula is invalid: the formula's source is shown instead.
    // Default: MicroTeX when built with IMGUI_RICHMD_WITH_LATEX, else none.
    std::function<std::optional<LatexBitmap>(const std::string& latex, float fontSizePx, ImU32 color,
                                             bool displayStyle)> RenderLatex;

    // Downloads the data of a URL image (http:// or https://). Called every frame for a URL until it returns
    // Ready or Failed; the result is then cached. A synchronous download returns Ready or Failed at once, an
    // asynchronous one returns Downloading first and tracks its pending downloads itself.
    // Default: libcurl when built with IMGUI_RICHMD_WITH_DOWNLOAD_IMAGES, emscripten_fetch on Emscripten
    // (IMGUI_RICHMD_EMSCRIPTEN_FETCH), else none (URL images are shown as broken). Python installs its own.
    MarkdownDownloadFunction Download;

    // Logs a warning. Default: stderr.
    std::function<void(const std::string& message)> Log;
};

// Sets the host services (call before CreateContext). Empty fields keep their default.
void SetHostServices(const HostServices& services);
const HostServices& GetHostServices();
```

## Backends

### LaTeX

Native LaTeX math rendering with MicroTeX and FreeType: a formula becomes an RGBA pixel buffer (Level 1, here).
Level 2, the cached GPU textures, lives with the host (in ImGui Bundle: `imgui_microtex/imgui_microtex.h`; the Python
module `imgui_microtex` binds both levels).

Thread safety: all functions are protected by a mutex and can be called from any thread.

#### TeX style

The layout style of a formula: `Display` for `$$...$$`, `Text` for `$...$`.

```cpp
// Selects the layout style used when rendering a formula. This maps directly
// to MicroTeX's TexStyle and corresponds to the four TeX styles defined by
// Knuth (D, T, S, SS). Pick Display for centered "display math" ($$...$$)
// and Text for inline math ($...$).
//
// The style affects symbol size, big-operator appearance, and the spacing
// around \frac (numerator shift-up and denominator shift-down): Display gives
// generous spacing; Text is compact.
enum class TexStyle {
    // Largest size. Big operators (\sum, \int, ...) use their large variants
    // with limits placed above and below. \frac uses generous vertical
    // spacing. This is what LaTeX uses inside $$...$$ and \[...\].
    Display,
    // Default inline size. Big operators use their small variants with
    // limits attached as sub/superscripts. \frac uses compact spacing.
    // This is what LaTeX uses inside $...$ and \(...\).
    Text,
    // Smaller size used by LaTeX inside sub/superscripts. Rarely useful at
    // the top level; MicroTeX switches to it automatically where needed.
    Script,
    // Smallest size, used inside scripts-of-scripts. Same caveat as Script.
    ScriptScript,
};
```

#### Initialization and shutdown

`Init()` loads the font files, once. `Release()` lets the host free the textures it made from formulas,
while its rendering backend is still alive.

```cpp
// Initialize MicroTeX + FreeType backend.
// clmFile: path to the .clm1 font metrics file
// fontFile: path to the .otf font file
// Safe to call repeatedly: subsequent calls after the first successful
// Init() no-op (MicroTeX itself stays initialized for process life; the
// underlying MicroTeX::init()/release() pair is not re-entrant, so we
// defer the real teardown to std::atexit: see rich_md_latex.cpp).
void Init(const std::string& clmFile, const std::string& fontFile);
// Same, with the two files read in memory (.clm1 and .otf). C++ only.
void InitFromMemory(const std::vector<uint8_t>& clmData, const std::vector<uint8_t>& fontData);

// Check if initialized.
bool IsInitialized();

// Drop the cached GPU texture set so the GL context can be torn down
// cleanly. Call from BeforeExit (or any point where the GL context is
// about to die). Safe to call multiple times, and safe to call Init()
// again afterwards: the underlying MicroTeX library stays alive for
// the whole process and its real teardown runs once at exit via a
// std::atexit handler installed on first Init().
void Release();

// Registers a callback run by Release(): a host that caches GPU textures made from formulas clears
// them here, while the rendering backend is still alive.
void AddReleaseCallback(std::function<void()> callback);
```

#### Rendering

`Render()` draws a formula into an RGBA buffer, with its baseline, to align it with the text.

```cpp
struct RenderedFormula {
    std::vector<uint8_t> Pixels;  // RGBA, Width * Height * 4 bytes
    int Width = 0;
    int Height = 0;
    int Depth = 0;        // distance below baseline (in pixels, unpadded)
    // BaselineY: pixel y-offset from the TOP of the (padded) image to
    // the formula's typographic baseline. Use this to align the formula
    // with surrounding text:
    //
    //     // ImGui text baseline is at cursor.y + GetFontBaked()->Ascent.
    //     float ascent = ImGui::GetFontBaked()->Ascent;
    //     float imageTop = ImGui::GetCursorPosY() + ascent - formula.BaselineY;
    //     ImGui::SetCursorPosY(imageTop);
    //     ImGui::Image(texId, ImVec2(formula.Width, formula.Height));
    //
    int BaselineY = 0;
};

// Render a LaTeX string to an RGBA pixel buffer.
// latex: the LaTeX math string (without $ delimiters)
// fontSize: font size in pixels
// color: foreground color (alpha channel is used)
// style: TeX layout style (Display for $$...$$, Text for $...$)
RenderedFormula Render(const std::string& latex, float fontSize, ImU32 color = IM_COL32_BLACK,
                       TexStyle style = TexStyle::Text);
RenderedFormula Render(const std::string& latex, float fontSize, const ImVec4& color, TexStyle style = TexStyle::Text);
```

### Code snippets

Code snippets with syntax highlighting (ImGuiColorTextEdit), read-only or editable, with a copy button. The markdown
code blocks use `ShowCodeSnippet` when built with `IMGUI_RICHMD_WITH_CODE_EDITOR`.

#### Snippet data

A snippet: its code, its language, its look.

```cpp
    enum class SnippetLanguage
    {
        Cpp,
        Hlsl,
        Glsl,
        C,
        Sql,
        AngelScript,
        Lua,
        Python
    };

    enum class SnippetTheme
    {
        Auto,   // Automatic based on bg color
        Dark,
        Light,
    };


    // DefaultSnippetLanguage: Cpp, or Python when the host defines IMGUI_RICHMD_DEFAULT_SNIPPET_LANGUAGE_PYTHON
    // (Python bindings)
    inline SnippetLanguage DefaultSnippetLanguage()
    {
#ifdef IMGUI_RICHMD_DEFAULT_SNIPPET_LANGUAGE_PYTHON
        return SnippetLanguage::Python;
#else
        return SnippetLanguage::Cpp;
#endif
    }


    struct SnippetData
    {
        std::string Code = "";
        SnippetLanguage Language = DefaultSnippetLanguage();
        SnippetTheme Palette = SnippetTheme::Auto;

        bool ShowCopyButton = true;         // Displayed on top of the editor (Top Right corner)
        bool ShowCursorPosition = true;     // Show line and column number
        std::string DisplayedFilename = {}; // Displayed on top of the editor

        int HeightInLines = 0;              // Number of visible lines in the editor
        // If the number of lines in the code exceeds MaxHeightInLines, the editor will scroll. Set to 0 to disable.
        int MaxHeightInLines = 40;

        bool ReadOnly = true;               // Snippets are read-only by default

        bool Border = false;                // Draw a border around the editor

        bool DeIndentCode = true;           // Keep the code indentation, but remove main indentation,
                                            // so that the displayed code start at column 1

        bool AddFinalEmptyLine = false;     // Add an empty line at the end of the code if missing
    };
```

#### Showing snippets

One snippet, editable or not, or several side by side.

```cpp
bool ShowEditableCodeSnippet(const std::string& label_id, SnippetData* snippetData, float width = 0.f,
                             int overrideHeightInLines = 0);
void ShowCodeSnippet(const SnippetData& snippetData, float width = 0.f, int overrideHeightInLines = 0);
void ShowSideBySideSnippets(const SnippetData& snippet1, const SnippetData& snippet2,
                            bool hideIfEmpty = true, bool equalVisibleLines = true);
void ShowSideBySideSnippets(const std::vector<SnippetData>& snippets ,
                            bool hideIfEmpty = true, bool equalVisibleLines = true);
```

## Legacy names

The former names, kept for existing code.

```cpp
// The former names: InitializeMarkdown makes a default context and makes it current (a second call does
// nothing); DeInitializeMarkdown destroys it.
void InitializeMarkdown(const MarkdownOptions& options = MarkdownOptions());
void DeInitializeMarkdown();

// The former name of Render
void RenderUnindented(const std::string& markdownString);

// Legacy: the fonts now load at the first Render(). The returned function loads them right away,
// for hosts that build their font atlas once (no dynamic fonts).
VoidFunction GetFontLoaderFunction();
```
