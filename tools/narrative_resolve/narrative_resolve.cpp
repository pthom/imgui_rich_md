// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
// narrative_resolve: resolves the transclusions (![[file#name]]) of a markdown file into plain markdown, e.g. the
// library's docs/api.md from docs/api.src.md and the headers. No ImGui: it builds with narrative_programming.cpp alone.
//     narrative_resolve input.src.md output.md            writes output.md
//     narrative_resolve --check input.src.md output.md    fails when output.md differs from a new resolution
// A failed transclusion (an <md-error> in the result) is reported, and makes it fail.
#include "imgui_rich_md/narrative_programming.h"

#include <cstdio>
#include <fstream>
#include <sstream>

static std::optional<std::string> ReadFile(const std::string& path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        return std::nullopt;
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

static std::string FileName(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

int main(int argc, char** argv)
{
    bool check = argc == 4 && std::string(argv[1]) == "--check";
    if (argc != 3 && !check)
    {
        fprintf(stderr, "usage: narrative_resolve [--check] input.src.md output.md\n");
        return 2;
    }
    std::string input = argv[check ? 2 : 1], output = argv[check ? 3 : 2];
    auto source = ReadFile(input);
    if (!source)
    {
        fprintf(stderr, "narrative_resolve: cannot read %s\n", input.c_str());
        return 2;
    }
    std::string result = "<!-- Generated from " + FileName(input) + " by narrative_resolve: do not edit -->\n"
                         + RichMd::ResolveTransclusions(*source, ReadFile, input);

    // The errors are rendered as <md-error title="reason">`![[...]]`</md-error>, one per line
    int errors = 0;
    const std::string errorTag = "<md-error title=\"";
    for (size_t pos = result.find(errorTag); pos != std::string::npos; pos = result.find(errorTag, pos + 1))
    {
        fprintf(stderr, "%s: %s\n", input.c_str(), result.substr(pos, result.find('\n', pos) - pos).c_str());
        ++errors;
    }

    if (check)
    {
        auto current = ReadFile(output);
        if (!current || *current != result)
        {
            fprintf(stderr, "%s is not up to date: run narrative_resolve without --check\n", output.c_str());
            return 1;
        }
        return errors ? 1 : 0;
    }
    std::ofstream(output, std::ios::binary) << result;
    return errors ? 1 : 0;
}
