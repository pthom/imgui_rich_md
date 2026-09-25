// The narrative programming cases: each folder of tests/narrative/cases holds source files, a doc.md with
// transclusions, and expected.md, the result of resolving doc.md. The cases are data, so that another
// implementation of the specification can run them too.
// On a mismatch, the result is printed and written to narrative_actual/<case>.md in the working directory.
#include "imgui_rich_md/rich_md.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

static std::optional<std::string> ReadFile(const fs::path& path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        return std::nullopt;
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

int main()
{
    std::vector<fs::path> cases;
    for (const auto& entry : fs::directory_iterator(NARRATIVE_CASES_DIR))
        if (entry.is_directory())
            cases.push_back(entry.path());
    std::sort(cases.begin(), cases.end());

    int failures = 0;
    for (const auto& dir : cases)
    {
        auto readFile = [&](const std::string& path) { return ReadFile(dir / path); };
        std::string actual = RichMd::ResolveTransclusions(ReadFile(dir / "doc.md").value_or(""), readFile, "doc.md");
        std::string expected = ReadFile(dir / "expected.md").value_or("");
        std::string name = dir.filename().string();
        if (actual == expected)
        {
            printf("ok    %s\n", name.c_str());
            continue;
        }
        ++failures;
        fs::create_directories("narrative_actual");
        std::ofstream("narrative_actual/" + name + ".md", std::ios::binary) << actual;
        printf("FAIL  %s: expected.md differs from narrative_actual/%s.md:\n%s\n", name.c_str(), name.c_str(), actual.c_str());
    }
    printf("%d case(s), %d failure(s)\n", (int)cases.size(), failures);
    return failures == 0 && !cases.empty() ? 0 : 1;
}
