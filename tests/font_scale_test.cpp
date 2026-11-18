// Font scale test: the markdown follows a size pushed with ImGui::PushFont(NULL, size), relative to the frame's base
// size, through Dear ImGui's null backend. Its fonts (GetFont, GetCodeFont) and the height of a rendered paragraph
// scale with the pushed size; a render inside a render (a fenced block's markdown) keeps the outer one's scale.
// codeSizeFactor sizes the code font, codeBlockSizeFactor the code blocks' (a negative one: as codeSizeFactor).
// GetFontOptions() changes the sizes between two frames. The application's own fonts are used when set.
#include "imgui.h"
#include "imgui_internal.h"  // ImAbs
#include "imgui_impl_null.h"
#include "imgui_rich_md/rich_md.h"

#include <cstdio>
#include <string>

static const char* kParagraph = "One line of markdown text.";

struct Measures
{
    float fontSize = 0.f;      // RichMd::GetFont(MarkdownFontSpec()).size
    float codeFontSize = 0.f;  // RichMd::GetCodeFont().size
    float height = 0.f;        // the height of the rendered paragraph
    float nestedFontSize = 0.f;  // the markdown font, asked from a render inside the render
};

static Measures gNested;

// One frame: the paragraph rendered at the given scale of the frame's base size (1: no push)
static Measures DrawFrame(float scale)
{
    Measures m;
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(600.f, 400.f), ImGuiCond_Always);
    ImGui::Begin("Font scale", nullptr, ImGuiWindowFlags_NoTitleBar);
    if (scale != 1.f)
        ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * scale);
    m.fontSize = RichMd::GetFont(RichMd::MarkdownFontSpec()).size;
    m.codeFontSize = RichMd::GetCodeFont().size;
    float y0 = ImGui::GetCursorPosY();
    RichMd::Render(kParagraph);
    m.height = ImGui::GetCursorPosY() - y0;
    RichMd::Render("```nested\n```\n");  // its renderer renders markdown, and measures the font it gets
    m.nestedFontSize = gNested.fontSize;
    if (scale != 1.f)
        ImGui::PopFont();
    ImGui::End();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    return m;
}

// The code font that a code block's renderer gets: the host's RenderCodeBlock, set in main(), records it
static float gBlockCodeFontSize = 0.f;
static float BlockCodeFontSize()
{
    gBlockCodeFontSize = 0.f;
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    ImGui::Begin("Font scale", nullptr, ImGuiWindowFlags_NoTitleBar);
    RichMd::Render("```\nint x;\n```\n");
    ImGui::End();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    return gBlockCodeFontSize;
}

// The lowest glyph pixel of a render: the baseline of "x", which has no descender
static float GlyphBottom(const char* markdown)
{
    for (int i = 0; i < 2; ++i) {  // the first frame loads the fonts
        ImGui_ImplNull_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(600.f, 400.f), ImGuiCond_Always);
        ImGui::Begin("Font scale", nullptr, ImGuiWindowFlags_NoTitleBar);
        RichMd::Render(markdown);
        ImGui::End();
        ImGui::Render();
        ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    }
    const ImVec2 white = ImGui::GetIO().Fonts->TexUvWhitePixel;  // the window's background and border
    float bottom = 0.f;
    for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists)
        for (const ImDrawVert& vertex : list->VtxBuffer)
            if (vertex.uv.x != white.x || vertex.uv.y != white.y)
                bottom = ImMax(bottom, vertex.pos.y);
    return bottom;
}

static bool Expect(const char* what, bool condition)
{
    if (!condition)
        printf("font scale test failed: %s\n", what);
    return condition;
}

static bool Near(float a, float b) { return ImAbs(a - b) <= 0.02f * b + 0.5f; }

int main(int, char**)
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplNull_Init();
    RichMd::CreateContext(RichMd::MarkdownOptions());
    RichMd::RegisterFencedBlockRenderer("nested", [](const std::string&) {
        RichMd::Render("Nested markdown.");
        gNested.fontSize = RichMd::GetFont(RichMd::MarkdownFontSpec()).size;
    });
    DrawFrame(1.f);  // the first frame loads the fonts
    Measures normal = DrawFrame(1.f);
    DrawFrame(0.5f);
    Measures half = DrawFrame(0.5f);
    DrawFrame(2.f);
    Measures twice = DrawFrame(2.f);

    printf("font %.1f / %.1f / %.1f, code font %.1f / %.1f, height %.1f / %.1f / %.1f, nested %.1f / %.1f\n",
           normal.fontSize, half.fontSize, twice.fontSize, normal.codeFontSize, half.codeFontSize,
           normal.height, half.height, twice.height, normal.nestedFontSize, half.nestedFontSize);
    bool ok = Expect("no push: the markdown's own size", normal.fontSize > 0.f);
    ok = Expect("half the size pushed: half the markdown font", Near(half.fontSize, normal.fontSize * 0.5f)) && ok;
    ok = Expect("twice the size pushed: twice the markdown font", Near(twice.fontSize, normal.fontSize * 2.f)) && ok;
    ok = Expect("the code font follows", Near(half.codeFontSize, normal.codeFontSize * 0.5f)) && ok;
    ok = Expect("a smaller paragraph", half.height < normal.height * 0.75f) && ok;
    ok = Expect("a taller paragraph", twice.height > normal.height * 1.5f) && ok;
    ok = Expect("a render inside a render keeps the outer scale (no push)", Near(normal.nestedFontSize, normal.fontSize))
         && ok;
    ok = Expect("a render inside a render keeps the outer scale (half)", Near(half.nestedFontSize, half.fontSize)) && ok;

    RichMd::Context* fileFonts = RichMd::GetCurrentContext();
    ImFont* hostFont = ImGui::GetIO().Fonts->AddFontDefault();
    RichMd::MarkdownOptions hostOptions;
    hostOptions.fontOptions.boldFont = hostFont;
    hostOptions.fontOptions.codeFont = hostFont;
    RichMd::Context* hostFonts = RichMd::CreateContext(hostOptions);
    RichMd::SetCurrentContext(hostFonts);
    DrawFrame(1.f);
    ok = Expect("no host regular font: the file's bold", RichMd::GetFont(RichMd::MarkdownFontSpec(false, true)).font != hostFont) && ok;
    ok = Expect("the host's code font", RichMd::GetCodeFont().font == hostFont) && ok;
    RichMd::DestroyContext(hostFonts);
    ImFont* hostBoldFont = ImGui::GetIO().Fonts->AddFontDefault();
    hostOptions.fontOptions = RichMd::MarkdownFontOptions();
    hostOptions.fontOptions.regularFont = hostFont;
    hostOptions.fontOptions.boldFont = hostBoldFont;
    hostFonts = RichMd::CreateContext(hostOptions);
    RichMd::SetCurrentContext(hostFonts);
    DrawFrame(1.f);
    ok = Expect("no host italic font: the host's regular", RichMd::GetFont(RichMd::MarkdownFontSpec(true, false)).font == hostFont) && ok;
    ok = Expect("no host bold italic font: the host's bold", RichMd::GetFont(RichMd::MarkdownFontSpec(true, true)).font == hostBoldFont) && ok;
    RichMd::DestroyContext(hostFonts);
    RichMd::SetCurrentContext(fileFonts);

    RichMd::DestroyContext();

    RichMd::MarkdownOptions smallCode;
    smallCode.fontOptions.codeSizeFactor = 0.6f;
    RichMd::CreateContext(smallCode);
    float textBottom = GlyphBottom("x");
    float codeBottom = GlyphBottom("`x`");
    Measures small = DrawFrame(1.f);
    printf("codeSizeFactor 0.6: code font %.1f, baseline %.1f / %.1f\n", small.codeFontSize, textBottom, codeBottom);
    ok = Expect("codeSizeFactor scales the code font", Near(small.codeFontSize, normal.codeFontSize * 0.6f)) && ok;
    ok = Expect("smaller inline code sits on the text's baseline", ImAbs(codeBottom - textBottom) <= 1.f) && ok;
    RichMd::DestroyContext();

    RichMd::MarkdownOptions noCode;
    noCode.fontOptions.codeSizeFactor = 0.f;
    RichMd::CreateContext(noCode);
    DrawFrame(1.f);
    ok = Expect("a codeSizeFactor of 0 still gives a code font", DrawFrame(1.f).codeFontSize > 0.f) && ok;
    RichMd::DestroyContext();

    RichMd::HostServices services;
    services.RenderCodeBlock = [](const std::string&, const std::string&) {
        gBlockCodeFontSize = RichMd::GetCodeFont().size;
    };
    RichMd::SetHostServices(services);
    RichMd::MarkdownOptions apart;
    apart.fontOptions.codeSizeFactor = 0.6f;
    apart.fontOptions.codeBlockSizeFactor = 1.2f;
    RichMd::CreateContext(apart);
    DrawFrame(1.f);
    float inlineSize = DrawFrame(1.f).codeFontSize, blockSize = BlockCodeFontSize();
    printf("codeSizeFactor 0.6, codeBlockSizeFactor 1.2: inline code %.1f, code blocks %.1f\n", inlineSize, blockSize);
    ok = Expect("codeSizeFactor sizes inline code", Near(inlineSize, normal.codeFontSize * 0.6f)) && ok;
    ok = Expect("codeBlockSizeFactor sizes the code blocks", Near(blockSize, normal.codeFontSize * 1.2f)) && ok;
    RichMd::DestroyContext();

    RichMd::MarkdownOptions same;
    same.fontOptions.codeSizeFactor = 0.6f;  // codeBlockSizeFactor stays negative
    RichMd::CreateContext(same);
    DrawFrame(1.f);
    ok = Expect("a negative codeBlockSizeFactor follows codeSizeFactor",
                Near(BlockCodeFontSize(), normal.codeFontSize * 0.6f)) && ok;
    RichMd::DestroyContext();
    RichMd::SetHostServices(RichMd::HostServices());

    RichMd::CreateContext(RichMd::MarkdownOptions());
    DrawFrame(1.f);
    RichMd::GetFontOptions().regularSize *= 1.5f;
    RichMd::GetFontOptions().codeSizeFactor = 0.5f;
    Measures live = DrawFrame(1.f);
    printf("GetFontOptions(), regularSize x 1.5 and codeSizeFactor 0.5: font %.1f, code font %.1f\n", live.fontSize,
           live.codeFontSize);
    ok = Expect("a regularSize changed at run time applies to the next frame",
                Near(live.fontSize, normal.fontSize * 1.5f)) && ok;
    ok = Expect("a codeSizeFactor changed at run time applies to the next frame",
                Near(live.codeFontSize, normal.codeFontSize * 1.5f * 0.5f)) && ok;
    RichMd::DestroyContext();

    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    printf(ok ? "font scale test: OK\n" : "font scale test: FAILED\n");
    return ok ? 0 : 1;
}
