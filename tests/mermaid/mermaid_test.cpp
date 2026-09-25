// ::md Mermaid test
// The Mermaid test: each diagram of `tests/mermaid/corpus`, and each `mermaid` block of the library's docs
// (`docs/*.md`: our own docs must only show diagrams we draw well), is parsed and laid out with the markdown font
// (Dear ImGui's null backend: no window, no GPU), then checked (`mermaid_checks.h`). It fails on an unexpected issue,
// and on a known issue that no longer happens (its `%% known:` marker should then be removed).
// ::endmd
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

static std::string ReadFile(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::stringstream ss;
    ss << stream.rdbuf();
    return ss.str();
}

// The ```mermaid blocks of a markdown text (not those shown inside another fence)
static std::vector<std::string> MermaidBlocks(const std::string& markdown)
{
    std::vector<std::string> blocks;
    std::stringstream lines(markdown);
    std::string line, fence, block;
    bool inMermaid = false;
    while (std::getline(lines, line))
    {
        std::string t = line.substr(std::min(line.size(), line.find_first_not_of(" \t")));
        if (!t.empty() && t.back() == '\r')
            t.pop_back();
        if (fence.empty())
        {
            size_t n = t.find_first_not_of(t.empty() ? ' ' : t[0]);
            if (!t.empty() && (t[0] == '`' || t[0] == '~') && (n == std::string::npos ? t.size() : n) >= 3)
            {
                fence = t.substr(0, n == std::string::npos ? t.size() : n);
                inMermaid = t.substr(fence.size()) == "mermaid";
                block.clear();
            }
        }
        else if (t.rfind(fence, 0) == 0 && t.find_first_not_of(fence[0]) == std::string::npos)
        {
            if (inMermaid)
                blocks.push_back(block);
            fence.clear();
        }
        else if (inMermaid)
            block += line + "\n";
    }
    return blocks;
}

int main(int, char**)
{
    // The diagrams: the corpus, then the mermaid blocks of the docs
    std::vector<std::pair<std::string, std::string>> diagrams;  // name, source
    std::vector<fs::path> files, docs;
    for (const auto& entry : fs::directory_iterator(MERMAID_CORPUS_DIR))
        if (entry.path().extension() == ".mmd")
            files.push_back(entry.path());
    for (const auto& entry : fs::directory_iterator(MERMAID_DOCS_DIR))
    {
        std::string name = entry.path().filename().string();
        if (entry.path().extension() == ".md" && name.find(".src.md") == std::string::npos)
            docs.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    std::sort(docs.begin(), docs.end());
    for (const auto& file : files)
        diagrams.push_back({file.stem().string(), ReadFile(file)});
    for (const auto& doc : docs)
    {
        auto blocks = MermaidBlocks(ReadFile(doc));
        for (size_t k = 0; k < blocks.size(); ++k)
            diagrams.push_back({"docs/" + doc.filename().string() + " #" + std::to_string(k + 1), blocks[k]});
    }

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplNull_Init();
    RichMd::CreateContext();

    int failures = 0, known = 0, crossings = 0, bends = 0;
    for (size_t i = 0; i <= diagrams.size(); ++i)
    {
        ImGui_ImplNull_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("test");
        if (i == 0)
            RichMd::Render("The first frame loads the fonts.");
        else
        {
            const auto& [name, source] = diagrams[i - 1];
            RichMd::SizedFont font = RichMd::GetFont(RichMd::MarkdownFontSpec());
            ImGui::PushFont(font.font, font.size);
            MermaidChecks::Report report = MermaidChecks::Evaluate(source);
            ImGui::PopFont();
            printf("%-32s %-8s crossings: %d  bends: %d\n", name.c_str(), report.failures.empty() && report.fixedKnown.empty() ? "ok" : "FAILED",
                   report.crossings, report.bends);
            crossings += report.crossings, bends += report.bends;
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

    RichMd::DestroyContext();
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    printf("mermaid test: %zu diagrams, %d failures, %d known issues, %d crossings, %d bends\n", diagrams.size(), failures, known,
           crossings, bends);
    return failures == 0 ? 0 : 1;
}
