// Folding text test: RenderFolding() through Dear ImGui's null backend. Folded, the text takes the height of its first
// paragraph (its heading included), less than open; it returns its state; a text of one paragraph cannot fold.
#include "imgui.h"
#include "imgui_impl_null.h"
#include "imgui_rich_md/rich_md.h"

#include <cstdio>

static const char* kText = R"(# A title
The first paragraph, shown when the text is folded.

A second paragraph, hidden when the text is folded.

A third one.
)";
// The same, with a setext heading (underlined with =): the fold keeps the first paragraph below it too
static const char* kSetextText = R"(A title
=======

The first paragraph, shown when the text is folded.

A second paragraph, hidden when the text is folded.
)";

struct Result
{
    bool open = false;
    float height = 0.f;  // what the text and its link take in the window
};

// One frame: the text rendered with these options, in a window of its own (its state lives there)
static Result DrawFrame(const char* window, const char* text, const RichMd::FoldingTextOptions& options)
{
    Result r;
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(600.f, 600.f), ImGuiCond_Always);
    ImGui::Begin(window, nullptr, ImGuiWindowFlags_NoTitleBar);
    float y0 = ImGui::GetCursorPosY();
    r.open = RichMd::RenderFolding("text", text, options);
    r.height = ImGui::GetCursorPosY() - y0;
    ImGui::End();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    return r;
}

static bool Expect(const char* what, bool condition)
{
    if (!condition)
        printf("folding text test failed: %s\n", what);
    return condition;
}

int main(int, char**)
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplNull_Init();
    RichMd::CreateContext(RichMd::MarkdownOptions());

    RichMd::FoldingTextOptions openOptions, foldedOptions;
    foldedOptions.startFolded = true;
    Result open, folded, single, setext;
    for (int frame = 0; frame < 4; ++frame)  // the first frames load the fonts and measure the parts
    {
        open = DrawFrame("Open", kText, openOptions);
        folded = DrawFrame("Folded", kText, foldedOptions);
        single = DrawFrame("Single", "Only one paragraph.", foldedOptions);
        setext = DrawFrame("Setext", kSetextText, foldedOptions);
    }

    printf("open: %d, %.1f; folded: %d, %.1f; single: %d\n", open.open, open.height, folded.open, folded.height,
           single.open);
    bool ok = Expect("open at first by default", open.open);
    ok = Expect("folded at first with startFolded", !folded.open) && ok;
    ok = Expect("folded, it takes less height", folded.height < open.height * 0.75f) && ok;
    ok = Expect("folded, it keeps its first paragraph", folded.height > ImGui::GetFontSize() * 2.f) && ok;
    ok = Expect("a single paragraph cannot fold", single.open) && ok;
    ok = Expect("under a setext heading, the fold keeps the first paragraph", !setext.open
                && setext.height > folded.height * 0.9f && setext.height < folded.height * 1.3f) && ok;

    RichMd::DestroyContext();
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    printf(ok ? "folding text test: OK\n" : "folding text test: FAILED\n");
    return ok ? 0 : 1;
}
