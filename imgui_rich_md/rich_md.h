// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
#pragma once

#include "imgui.h"
#include "rich_md_host.h"       // HostServices, MarkdownDownloadResult
#include "narrative_programming.h" // ResolveTransclusions

#include <cstdint>
#include <functional>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <array>


namespace RichMd
{
    // =================================================================================================================
    //                                        Main API: Rendering
    // =================================================================================================================
    /*::md Rendering
    `Render()` draws a markdown string where it is called, every frame: rich_md is immediate mode, like Dear ImGui.
    It needs a context (see Contexts). Its text can be selected with the mouse (a double click selects a word, a triple
    click a paragraph) and copied with Ctrl+C (Cmd+C on macOS); a right click opens a menu: Copy, Copy as Markdown,
    Select All, and Copy Link over a link.
    ::code
    */

    // Renders a markdown string. Its common indentation is removed first (so that a string written
    // inside an indented function renders as expected; no-op on flush-left text), then its transclusions
    // are resolved (see ResolveTransclusions; the files are read through the host's ReadAsset).
    // Its size follows a size pushed with ImGui::PushFont(NULL, size), relative to the frame's base size:
    // PushFont(NULL, ImGui::GetStyle().FontSizeBase * 0.8f) renders it at 80%.
    void Render(const std::string& markdownString);
    // Renders a markdown string as is (no unindent, no transclusion)
    void RenderRaw(const std::string& markdownString);

    // Whether the text of the renders that follow can be selected, until the matching PopSelectableText(), in the same
    // frame. For markdown inside something that reacts to a drag itself (a node of a node editor, a custom widget).
    void PushSelectableText(bool selectable);
    void PopSelectableText();
    // Whether the text can be selected, outside of a PushSelectableText(): changes the option selectableText of the
    // current context
    void SetSelectableTextDefault(bool selectable);
    // ::endcode

    // =================================================================================================================
    //                                        Headings
    // =================================================================================================================
    /*::md Headings
    After a `Render()`, its headings and their places: for a table of contents drawn by the application.
    A link `[text](#slug)` scrolls to the heading of that slug in the same render, and opens the collapsed sections
    (`<details>`) that hide it. A slug that no heading of the render has goes to `OnOpenLink`, as any link.

    With `MarkdownOptions::foldableHeadings`, the headings fold: an arrow in a margin at their left (shown under the
    mouse, always on a touch screen) hides their section, until the next heading of the same level or above. The menu
    of a right click folds or unfolds them all, as `FoldAllHeadings()` does. A link or a search that reaches a folded
    section unfolds it.
    ::code
    */

    // A heading of the last render
    struct Heading
    {
        int level = 1;          // 1 to 6
        std::string text;       // without markup
        std::string slug;       // its anchor, made from the text as GitHub does (a repeated title gets -1, -2...)
        float y = 0.f;          // its top, in its window's content coordinates: SetScrollY(y) shows it at the top
        bool hidden = false;    // inside a collapsed `<details>` or a fold: y is the one of what hides it
    };
    // The headings of the last Render() call (valid until the next one)
    const std::vector<Heading>& LastRenderHeadings();
    // Folds (or unfolds) all the foldable headings (MarkdownOptions::foldableHeadings), from the next frame: those of
    // the current document between BeginDocument() and EndDocument(), else those of the last Render()
    void FoldAllHeadings(bool folded);
    // ::endcode

    // =================================================================================================================
    //                                        Documents
    // =================================================================================================================
    /*::md Documents
    A document is markdown in a scroll area of its own, with a table of contents beside it. `RenderDocument()` draws
    one markdown string. `BeginDocument()` and `EndDocument()` frame several renders and widgets: their headings belong
    to the document, and a link `[text](#slug)` in any of them reaches a heading of any other. `DocumentHeading()`
    gives a section made of widgets its heading, as a markdown heading does.

    With foldable headings (`DocumentOptions::foldableHeadings` for one document, or the context's option), a fold goes
    on across the renders and the sections of widgets: `DocumentHeading()` returns false when a fold hides its section,
    and the application skips its widgets. The table of contents folds or unfolds them all.

    The search: Ctrl+F (Cmd+F on macOS) in a document opens its find bar, and so does "/" when no text field is active
    (in a web page, Cmd+F also opens the browser's find bar, unless the page cancels the key while its canvas has the
    focus). On a touch screen: the magnifier above a narrow document, or the Search tab of the table of contents. The
    matches are highlighted in all its renders; Enter goes to the next one, Shift+Enter (or the up arrow) to the
    previous one, and past the last one back to the first (the bar says so). Escape closes the bar, and the current
    match becomes the selection. The matches with their context are listed under the bar and in the Search tab, the
    table of contents shows their count per section, and ticks on the scrollbar show where they are. The search also
    finds the text of the collapsed sections (a jump to one of their matches opens them), of the code blocks, and of the
    formulas (their LaTeX source: a formula is one match).
    ::code
    */

    // The scroll to a match, a heading or an anchor: eased over scrollAnimationSeconds, unless reduced motion is asked
    enum class ScrollAnimation
    {
        FollowSystem,   // animated, unless the system asks for reduced motion (HostServices::PrefersReducedMotion)
        Always,
        Never
    };

    // The options of a document
    struct DocumentOptions
    {
        // The table of contents: a side panel, whose edge the reader drags to resize it, and which a button hides
        // (a line above the content then gives the current section, and the table of contents as a menu)
        bool toc = true;
        int tocMinHeadings = 3;   // with fewer headings, no table of contents
        int tocMaxLevel = 3;      // the deepest level listed
        // A document narrower than this (in em) shows the line above the content instead of the panel (a phone)
        float narrowWidth = 40.f;
        // The search: Ctrl+F (Cmd+F on macOS) in the document opens its find bar
        bool search = true;
        ScrollAnimation scrollAnimation = ScrollAnimation::FollowSystem;
        float scrollAnimationSeconds = 0.3f;  // the duration of an animated scroll, whatever its distance
        // The headings of this document fold (also when the context's MarkdownOptions::foldableHeadings is off)
        bool foldableHeadings = false;
    };
    // A document: the renders and the widgets drawn until EndDocument() share a scroll area and their headings.
    // size: as BeginChild (0: all the available space). Call EndDocument() in the same frame. The content is always
    // drawn: there is no return value to skip it. One document at a time.
    void BeginDocument(const char* id, ImVec2 size = ImVec2(0.f, 0.f),
                       const DocumentOptions& options = DocumentOptions());
    void EndDocument();
    // The one-call form: BeginDocument(id, size, options), Render(markdown), EndDocument()
    void RenderDocument(const char* id, const std::string& markdown, ImVec2 size = ImVec2(0.f, 0.f),
                        const DocumentOptions& options = DocumentOptions());
    // Starts a section of the document that is not markdown (widgets, a plot): a heading of the document, with its slug
    // (an anchor for `[text](#slug)` links). drawTitle: draws the text as a markdown heading of that level; false when
    // the title is drawn by other means (an ImGui text, a plot's own title), or not at all. Returns whether the section
    // shows: false when a fold hides it (MarkdownOptions::foldableHeadings), and the application skips its widgets.
    bool DocumentHeading(int level, const std::string& text, bool drawTitle = true);
    // ::endcode

    // =================================================================================================================
    //                                        Folding text
    // =================================================================================================================
    /*::md Folding text
    A text that folds to its first paragraph, or to a line `<!--more-->` (as Jekyll and Hugo mark an excerpt: an HTML
    comment, which other renderers show as nothing): an introduction above a demo, which leaves the room to the demo
    once read. A link at its end folds or unfolds it, with a short slide. With `foldOnClickElsewhere`, it folds as soon
    as the user clicks (or taps) elsewhere in the window: on the demo below it.
    ::code
    */

    // The options of a folding text
    struct FoldingTextOptions
    {
        bool startFolded = false;            // folded at first (else open, until the user folds it)
        bool foldOnClickElsewhere = false;   // it folds when the user clicks or taps elsewhere in the window
        float animationSeconds = 0.25f;      // the slide (none when the system asks for reduced motion)
    };
    // Renders a markdown text, folded to its first paragraph (the headings before it stay) or to a line <!--more-->,
    // or open; a link at its end folds or unfolds it. id: keeps its state, in the current window. Returns whether it
    // is open.
    bool RenderFolding(const char* id, const std::string& markdown,
                       const FoldingTextOptions& options = FoldingTextOptions());
    // ::endcode

    // =================================================================================================================
    //                                        Options and callbacks
    // =================================================================================================================
    /*::md Options and callbacks
    The options of a context: its fonts, LaTeX, links, and the callbacks through which the application handles links,
    images, HTML tags and headings.
    ::code
    */

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

        // The text can be selected with the mouse and copied (SetSelectableTextDefault changes it later,
        // PushSelectableText for some renders)
        bool selectableText = true;

        // The headings fold: an arrow in a margin at their left hides their section, until the next heading of the
        // same level or above. Only the headings outside lists, quotes and <details> fold.
        bool foldableHeadings = false;
        int foldableHeadingsMinLevel = 2;  // the first level that folds (a "#" heading often titles the whole text)
    };
    // ::endcode

    // =================================================================================================================
    //                                      Context
    // =================================================================================================================
    /*::md Contexts
    A context holds the options, the fonts and the caches (textures, formulas, diagrams). Most applications have one.
    ::code
    */

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
    // ::endcode

    // =================================================================================================================
    //                                      Rendering files
    // =================================================================================================================
    /*::md Rendering files
    `RenderFile()` renders a section, a code region or a whole file: a program renders its own narrative with
    `RICHMD_RENDER_THIS_FILE`. The syntax and the resolver: `narrative_programming.h`.
    ::code
    */

    // Renders ![[path#target]]: target is a section ("Intro"), its code ("Escape#code"), a code region, a heading
    // of a markdown document, or empty (the whole file). The path is looked up in the assets first, then on the
    // file system: a source file renders its own narrative with RICHMD_RENDER_THIS_FILE("Intro") (C++) or
    // rich_md.render_this_file("Intro") (Python).
    void RenderFile(const std::string& path, const std::string& target = "");
    #define RICHMD_RENDER_THIS_FILE(target) RichMd::RenderFile(__FILE__, target)
    // ::endcode

    // =================================================================================================================
    //                                      Extensions
    // =================================================================================================================
    /*::md Extensions
    Your own renderers for fenced code blocks, Mermaid diagrams and links outside of markdown.
    ::code
    */

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
    // ::endcode

    // =================================================================================================================
    //                                      Style and fonts
    // =================================================================================================================
    /*::md Style and fonts
    The colors, spacing and fonts of the current context, for widgets drawn next to the markdown.
    ::code
    */

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
        ImVec4 searchMatch = ImVec4(245.f / 255.f, 205.f / 255.f, 60.f / 255.f, 0.45f);  // a match of the search
        ImVec4 searchMatchCurrent = ImVec4(1.f, 0.55f, 0.1f, 0.75f);                    // the current match
        // Note, Tip, Important, Warning, Caution (label and bar)
        ImVec4 admonitionColors[5] = {
            ImVec4(0.35f, 0.65f, 1.00f, 1.0f), ImVec4(0.25f, 0.73f, 0.32f, 1.0f), ImVec4(0.82f, 0.60f, 0.97f, 1.0f),
            ImVec4(0.95f, 0.75f, 0.22f, 1.0f), ImVec4(0.97f, 0.32f, 0.29f, 1.0f) };
        float blockGap = 0.3f;              // vertical gap between blocks
        float listItemGap = 0.0f;           // vertical gap between list items (0: tight, as the lines of a paragraph)
        float headerGapStep = 0.12f;        // extra gap above a header: (7 - level) * headerGapStep
        float subSupScale = 0.7f;           // font size of <sub> and <sup>
        float quoteBarThickness = 2.0f;     // pixels
        float admonitionBarThickness = 3.0f;
        bool linkTooltip = true;            // show the url when hovering a link
        // Space above and below each rendered fragment (a print() call), in em: fragments stack
        // with widgets between them. Bottom: negative = automatic (the last line ends there).
        float fragmentGapTop = 0.0f;
        float fragmentGapBottom = -1.0f;
    };

    // The colors and spacing of the current context. C++ only.
    Style& GetStyle();

    // The markdown's fonts, at the size that a render would use now (see Render(): they follow a pushed size)
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
    // ::endcode

    // =================================================================================================================
    //                                      Capabilities
    // =================================================================================================================
    /*::md Capabilities
    What this build and its host provide (available once a context exists).
    ::code
    */

    bool HasLatex();         // $...$ and $$...$$ rendered as formulas (else shown as their source)
    bool HasUrlImages();     // images downloaded from http(s) urls
    bool HasCodeEditor();    // code blocks with syntax highlighting (else plain monospaced blocks)
    // ::endcode

    // =================================================================================================================
    //                                      Legacy names
    // =================================================================================================================
    /*::md Legacy names
    The former names, kept for existing code.
    ::code
    */

    // The former names: InitializeMarkdown makes a default context and makes it current (a second call does
    // nothing); DeInitializeMarkdown destroys it.
    void InitializeMarkdown(const MarkdownOptions& options = MarkdownOptions());
    void DeInitializeMarkdown();

    // The former name of Render
    void RenderUnindented(const std::string& markdownString);

    // Legacy: the fonts now load at the first Render(). The returned function loads them right away,
    // for hosts that build their font atlas once (no dynamic fonts).
    VoidFunction GetFontLoaderFunction();
    // ::endcode
}
