// Spans test: md4c's ~sub~, ^sup^ and ==mark== draw as the HTML tags <sub>, <sup> and <mark> do, each with its option
// (by default: ~x~ strikes through as on GitHub, ^x^ is no superscript, ==x== is highlighted), through Dear ImGui's null
// backend. And the unindent of code removes a common prefix of tabs (md4c keeps a code block's tabs).
#include "imgui.h"
#include "imgui_impl_null.h"
#include "imgui_rich_md/rich_md.h"
#include "imgui_rich_md/internal/rich_md_internal.h"  // Internal::Unindent

#include <cstdio>
#include <string>
#include <vector>

// The positions of the vertices a render draws: two renders that look the same give the same list
static std::vector<float> DrawnVertices(const char* markdown)
{
    for (int i = 0; i < 2; ++i) {  // the first frame loads the fonts
        ImGui_ImplNull_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(600.f, 400.f), ImGuiCond_Always);
        ImGui::Begin("Spans", nullptr, ImGuiWindowFlags_NoTitleBar);
        RichMd::Render(markdown);
        ImGui::End();
        ImGui::Render();
        ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    }
    std::vector<float> positions;
    for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists)
        for (const ImDrawVert& vertex : list->VtxBuffer) {
            positions.push_back(vertex.pos.x);
            positions.push_back(vertex.pos.y);
        }
    return positions;
}

static bool ExpectSame(const char* what, const char* markdown, const char* expected)
{
    bool ok = DrawnVertices(markdown) == DrawnVertices(expected);
    if (!ok)
        printf("spans test failed: %s: \"%s\" does not draw as \"%s\"\n", what, markdown, expected);
    return ok;
}

static bool ExpectDifferent(const char* what, const char* markdown, const char* other)
{
    bool ok = DrawnVertices(markdown) != DrawnVertices(other);
    if (!ok)
        printf("spans test failed: %s: \"%s\" draws as \"%s\"\n", what, markdown, other);
    return ok;
}

static bool ExpectUnindent(const char* text, bool isCode, const char* expected)
{
    std::string result = RichMd::Internal::Unindent(text, isCode);
    bool ok = result == expected;
    if (!ok)
        printf("spans test failed: Unindent(isCode=%d) gave \"%s\", expected \"%s\"\n", isCode, result.c_str(), expected);
    return ok;
}

int main(int, char**)
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplNull_Init();
    bool ok = true;

    // The default options
    RichMd::CreateContext(RichMd::MarkdownOptions());
    ok = ExpectSame("~x~ strikes through, as on GitHub", "a ~x~ b", "a ~~x~~ b") && ok;
    ok = ExpectDifferent("~x~ strikes through", "a ~x~ b", "a x b") && ok;
    ok = ExpectSame("==x== is highlighted", "a ==x== b", "a <mark>x</mark> b") && ok;
    ok = ExpectDifferent("^x^ is no superscript", "x^2^", "x<sup>2</sup>") && ok;
    ok = ExpectDifferent("~x~ is no subscript", "H~2~O", "H<sub>2</sub>O") && ok;
    RichMd::DestroyContext();

    // Subscripts and superscripts on, highlight off
    RichMd::MarkdownOptions options;
    options.subscripts = true;
    options.superscripts = true;
    options.highlight = false;
    RichMd::CreateContext(options);
    ok = ExpectSame("~x~ is a subscript", "H~2~O", "H<sub>2</sub>O") && ok;
    ok = ExpectSame("^x^ is a superscript", "E = mc^2^", "E = mc<sup>2</sup>") && ok;
    ok = ExpectDifferent("~~x~~ still strikes through", "a ~~x~~ b", "a <sub>x</sub> b") && ok;
    ok = ExpectDifferent("~~x~~ strikes through", "a ~~x~~ b", "a x b") && ok;
    ok = ExpectDifferent("==x== is not highlighted", "a ==x== b", "a <mark>x</mark> b") && ok;
    RichMd::DestroyContext();

    // The unindent of code: a common prefix of tabs goes too, and the tabs after it stay. Markdown keeps its tabs.
    ok = ExpectUnindent("\tint a;\n\t\tint b;\n", true, "int a;\n\tint b;") && ok;
    ok = ExpectUnindent("\n  \tint a;\n  \tint b;", true, "int a;\nint b;") && ok;
    ok = ExpectUnindent("    int a;\n\tint b;", true, "int a;\n\tint b;") && ok;
    ok = ExpectUnindent("\tText", false, "\tText") && ok;

    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    printf(ok ? "spans test: ok\n" : "spans test: FAILED\n");
    return ok ? 0 : 1;
}
