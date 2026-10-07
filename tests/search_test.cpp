// Search test: the matching of the search (FindMatches), without ImGui: the case and the diacritics folded unless asked
// otherwise, whole words, the byte offsets of the matches in UTF-8 text.
#include "imgui_rich_md/internal/rich_md_search.h"

#include <cstdio>
#include <string>
#include <vector>

using RichMd::FindMatches;
using RichMd::SearchOptions;
using RichMd::TextMatch;

static bool ExpectCount(const char* text, const char* query, const SearchOptions& options, size_t expected)
{
    size_t count = FindMatches(text, query, options).size();
    if (count != expected)
        printf("search test failed: \"%s\" in \"%s\": %zu matches, expected %zu\n", query, text, count, expected);
    return count == expected;
}

static SearchOptions Options(bool matchCase, bool wholeWords, bool matchDiacritics)
{
    SearchOptions options;
    options.matchCase = matchCase;
    options.wholeWords = wholeWords;
    options.matchDiacritics = matchDiacritics;
    return options;
}

int main(int, char**)
{
    const SearchOptions loose;  // the case and the diacritics folded
    bool ok = ExpectCount("Hello hello HELLO", "hello", loose, 3);
    ok = ExpectCount("Hello hello HELLO", "hello", Options(true, false, false), 1) && ok;

    // Diacritics
    ok = ExpectCount("café cafe CAFÉ", "cafe", loose, 3) && ok;
    ok = ExpectCount("café cafe CAFÉ", "cafe", Options(false, false, true), 1) && ok;
    ok = ExpectCount("café cafe CAFÉ", "café", Options(false, false, true), 2) && ok;
    ok = ExpectCount("Łódź", "lodz", loose, 1) && ok;
    ok = ExpectCount("İstanbul", "istanbul", loose, 1) && ok;
    ok = ExpectCount("ŒUVRE œuvre", "œuvre", loose, 2) && ok;     // a ligature keeps its letter, folds its case
    ok = ExpectCount("Straße", "strasse", loose, 0) && ok;        // ß is a letter of its own

    // Greek and Cyrillic: the case
    ok = ExpectCount("ΣΟΦΙΑ σοφια", "σοφια", loose, 2) && ok;
    ok = ExpectCount("λόγος", "ΛΌΓΟΣ", loose, 1) && ok;            // the final sigma folds to sigma
    ok = ExpectCount("Москва МОСКВА", "москва", loose, 2) && ok;

    // Whole words
    ok = ExpectCount("cat concatenate cat_ cat.", "cat", Options(false, true, false), 2) && ok;
    ok = ExpectCount("été, Été", "ete", Options(false, true, false), 2) && ok;

    // Not overlapping, an empty query, a cut UTF-8 sequence
    ok = ExpectCount("aaaa", "aa", loose, 2) && ok;
    ok = ExpectCount("anything", "", loose, 0) && ok;
    ok = ExpectCount("abc\xC3", "c", loose, 1) && ok;

    // The bytes of a match, in the text: "é" is two bytes
    std::vector<TextMatch> m = FindMatches("a é b é", "e", loose);
    bool offsets = m.size() == 2 && m[0].begin == 2 && m[0].end == 4 && m[1].begin == 7 && m[1].end == 9;
    if (!offsets)
        printf("search test failed: the byte offsets of the matches\n");
    ok = offsets && ok;

    printf(ok ? "search test: ok\n" : "search test: FAILED\n");
    return ok ? 0 : 1;
}
