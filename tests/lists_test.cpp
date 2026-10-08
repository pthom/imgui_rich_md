// Lists test: the blocks of a list item, each on its own line, through Dear ImGui's null backend. A quote or a second
// paragraph below the item's text takes a line of its own (it did not: it was drawn over the text), and the block
// after a list keeps its gap (a quote in an item no longer leaves a "skip the next gap" behind).
#include "imgui.h"
#include "imgui_impl_null.h"
#include "imgui_rich_md/rich_md.h"

#include <cstdio>

// The height of a render, in a window wide enough for each line
static float RenderHeight(const char* markdown)
{
    float height = 0.f;
    for (int i = 0; i < 2; ++i) {  // the first frame loads the fonts
        ImGui_ImplNull_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(600.f, 400.f), ImGuiCond_Always);
        ImGui::Begin("Lists", nullptr, ImGuiWindowFlags_NoTitleBar);
        float top = ImGui::GetCursorPosY();
        RichMd::Render(markdown);
        RichMd::Render("End.");  // a block after the markdown: its line ends, or not
        height = ImGui::GetCursorPosY() - top;
        ImGui::End();
        ImGui::Render();
        ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    }
    return height;
}

// The markdown takes at least this many lines (each line as tall as a paragraph of one line)
static bool ExpectLines(const char* what, const char* markdown, int lines, float lineHeight)
{
    float height = RenderHeight(markdown);
    bool ok = height >= lineHeight * (float)lines - 1.f;
    if (!ok)
        printf("lists test failed: %s: %.1f px, expected at least %d lines of %.1f px\n", what, height, lines,
               lineHeight);
    return ok;
}

int main(int, char**)
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplNull_Init();
    RichMd::CreateContext(RichMd::MarkdownOptions());

    float lineHeight = RenderHeight("One line.") / 2.f;  // "One line.", then "End."
    bool ok = ExpectLines("a quote below the item's text", "1. **Pythonic Wisdom**:\n    > They say", 3, lineHeight);
    ok = ExpectLines("a quote in a tight item", "* An item\n  > a quote\n* Another item", 4, lineHeight) && ok;
    ok = ExpectLines("the second paragraph of a loose item", "1. First paragraph.\n\n   Second paragraph.", 3,
                     lineHeight) && ok;
    ok = ExpectLines("the block after a list with a quote", "* An item\n  > a quote\n\nAfter the list.", 4,
                     lineHeight) && ok;

    RichMd::DestroyContext();
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    printf(ok ? "lists test: ok\n" : "lists test: FAILED\n");
    return ok ? 0 : 1;
}
