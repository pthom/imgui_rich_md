// The Mermaid test: each diagram of tests/mermaid/corpus is parsed and laid out with the markdown font (Dear ImGui's
// null backend: no window, no GPU), then checked (mermaid_checks.h). It fails on an unexpected issue, and on a known
// issue that no longer happens (its `%% known:` marker should then be removed).
#include "imgui.h"
#include "imgui_impl_null.h"
#include "imgui_rich_md/rich_md.h"
#include "mermaid_checks.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

int main(int, char**)
{
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(MERMAID_CORPUS_DIR))
        if (entry.path().extension() == ".mmd")
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplNull_Init();
    RichMd::InitializeMarkdown();

    int failures = 0, known = 0, crossings = 0;
    for (size_t i = 0; i <= files.size(); ++i)
    {
        ImGui_ImplNull_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("test");
        if (i == 0)
            RichMd::Render("The first frame loads the fonts.");
        else
        {
            const fs::path& file = files[i - 1];
            std::ifstream stream(file, std::ios::binary);
            std::stringstream source;
            source << stream.rdbuf();
            RichMd::SizedFont font = RichMd::GetFont(RichMd::MarkdownFontSpec());
            ImGui::PushFont(font.font, font.size);
            MermaidChecks::Report report = MermaidChecks::Evaluate(source.str());
            ImGui::PopFont();
            std::string name = file.stem().string();
            printf("%-32s %-8s crossings: %d\n", name.c_str(), report.failures.empty() && report.fixedKnown.empty() ? "ok" : "FAILED", report.crossings);
            crossings += report.crossings;
            for (const auto& issue : report.failures)
                printf("    %s: %s\n", issue.check.c_str(), issue.message.c_str());
            for (const auto& check : report.fixedKnown)
                printf("    %s is marked as known, but passes: remove it from `%%%% known:`\n", check.c_str());
            for (const auto& issue : report.known)
                printf("    (known) %s: %s\n", issue.check.c_str(), issue.message.c_str());
            failures += (int)(report.failures.size() + report.fixedKnown.size());
            known += (int)report.known.size();
        }
        ImGui::End();
        ImGui::Render();
        ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    }

    RichMd::DeInitializeMarkdown();
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    printf("mermaid test: %zu diagrams, %d failures, %d known issues, %d crossings\n", files.size(), failures, known, crossings);
    return failures == 0 ? 0 : 1;
}
