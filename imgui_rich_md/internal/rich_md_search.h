// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
// The matching of the search: the matches of a query in a text (UTF-8). The case and the diacritics are folded unless
// asked otherwise; whole words on demand. No ImGui. Internal: the search is drawn by the documents (rich_md.h).
#pragma once
#include <string>
#include <vector>

namespace RichMd
{
    struct SearchOptions
    {
        bool matchCase = false;
        bool wholeWords = false;
        bool matchDiacritics = false;  // false: "e" finds "é" (Latin-1 and Latin Extended-A)
    };

    // A match: its bytes in the text
    struct TextMatch
    {
        size_t begin = 0, end = 0;
    };

    // The matches of a query in a text, in order, not overlapping. An empty query has none.
    std::vector<TextMatch> FindMatches(const std::string& text, const std::string& query, const SearchOptions& options);
}
