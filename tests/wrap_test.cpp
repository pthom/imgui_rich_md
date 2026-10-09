// Wrap test: a paragraph of short spans (bold, kbd) drawn at many widths, through Dear ImGui's null backend. Dear
// ImGui's log gives the drawn lines. No word is cut across two lines: the end of a line and the start of the next one
// do not form a word of the paragraph ("right-dra" / "g").
#include "imgui.h"
#include "imgui_internal.h"  // LogToBuffer, GImGui->LogBuffer
#include "imgui_impl_null.h"
#include "imgui_rich_md/rich_md.h"

#include <cstdio>
#include <set>
#include <sstream>
#include <string>
#include <vector>

static const char* kParagraph =
    "**Delete**: select, then <kbd>Delete</kbd>  ·  **Menus**: right-click  ·  **Pan**: right-drag  ·  "
    "**Zoom**: the wheel  ·  **See the whole graph**: <kbd>F</kbd>";
// The words of the paragraph, as drawn (no markup)
static const char* kWords = "Delete: select, then Delete · Menus: right-click · Pan: right-drag · Zoom: the wheel · "
                            "See the whole graph: F";

static std::vector<std::string> Split(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    std::istringstream stream(text);
    std::string part;
    while (std::getline(stream, part, separator))
        parts.push_back(part);
    return parts;
}

static std::vector<std::string> Words(const std::string& text)
{
    std::vector<std::string> words;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word)
        words.push_back(word);
    return words;
}

// The lines of the paragraph, drawn in a window of the given width
static std::vector<std::string> DrawFrame(float width)
{
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.f, 0.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, 600.f), ImGuiCond_Always);
    ImGui::Begin("Wrap", nullptr, ImGuiWindowFlags_NoTitleBar);
    ImGui::LogToBuffer();
    RichMd::Render(kParagraph);
    std::string log = GImGui->LogBuffer.c_str();
    ImGui::LogFinish();
    ImGui::End();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());

    std::vector<std::string> lines;
    for (const std::string& line : Split(log, '\n'))
        if (!Words(line).empty())
            lines.push_back(line);
    return lines;
}

int main(int, char**)
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplNull_Init();
    RichMd::CreateContext(RichMd::MarkdownOptions());
    DrawFrame(400.f);  // the first frame loads the fonts

    std::set<std::string> words;
    for (const std::string& word : Words(kWords))
        words.insert(word);

    bool ok = true;
    int nbWidths = 0, nbWrapped = 0;
    for (float width = 80.f; width <= 500.f; width += 1.f) {
        std::vector<std::string> lines = DrawFrame(width);
        ++nbWidths;
        if (lines.size() > 1)
            ++nbWrapped;
        for (size_t i = 0; i + 1 < lines.size(); ++i) {
            std::string joined = Words(lines[i]).back() + Words(lines[i + 1]).front();
            if (words.count(joined)) {
                printf("wrap test failed: at the width %.0f, \"%s\" is cut: \"%s\" / \"%s\"\n", width,
                       joined.c_str(), lines[i].c_str(), lines[i + 1].c_str());
                ok = false;
            }
        }
    }
    printf("%d widths, %d of them wrapped\n", nbWidths, nbWrapped);
    if (nbWrapped < nbWidths / 2) {
        printf("wrap test failed: too few widths wrap the paragraph\n");
        ok = false;
    }

    RichMd::DestroyContext();
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    printf(ok ? "wrap test: OK\n" : "wrap test: FAILED\n");
    return ok ? 0 : 1;
}
