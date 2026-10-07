// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
// The text and the query are folded code point by code point (UTF-8), and the folded text keeps, for each of its bytes,
// the offset of its code point in the text: a match found in the folded text maps back to the text's bytes.
// Folding: the diacritics of Latin-1 and Latin Extended-A ("é" -> "e", "Ł" -> "L"; the ligatures and the letters of
// their own, as "æ" or "ß", are kept), then the case of the Latin, Greek and Cyrillic letters ("Σ" -> "σ", and the final
// "ς" -> "σ").
#include "rich_md_search.h"

namespace RichMd
{
namespace
{
    // One code point at i (i advances); an invalid byte is read as it is
    unsigned DecodeUtf8(const std::string& s, size_t& i)
    {
        unsigned char c = (unsigned char)s[i];
        int n = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
        if (c < 0x80 || n == 0 || i + (size_t)n >= s.size()) {  // ASCII, a continuation byte, a cut sequence
            i += 1;
            return c;
        }
        unsigned cp = c & (0x3Fu >> n);
        for (int k = 1; k <= n; ++k) {
            unsigned char cc = (unsigned char)s[i + (size_t)k];
            if ((cc & 0xC0) != 0x80) {
                i += 1;
                return c;
            }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        i += (size_t)n + 1;
        return cp;
    }

    void EncodeUtf8(unsigned cp, std::string& out)
    {
        if (cp < 0x80)
            out += (char)cp;
        else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    // The base letter of U+00C0..U+00FF and U+0100..U+017F ('.': kept as it is)
    const char* kLatin1Bases = "AAAAAA.CEEEEIIII.NOOOOO.OUUUUY..aaaaaa.ceeeeiiii.nooooo.ouuuuy.y";
    const char* kLatinExtABases = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIi..JjKk.LlLlLlLlLlNnNnNnn..OoOoOo.."
                                  "RrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";

    unsigned WithoutDiacritics(unsigned cp)
    {
        char base = '.';
        if (cp >= 0xC0 && cp <= 0xFF)
            base = kLatin1Bases[cp - 0xC0];
        else if (cp >= 0x100 && cp <= 0x17F)
            base = kLatinExtABases[cp - 0x100];
        return base == '.' ? cp : (unsigned)base;
    }

    unsigned Lower(unsigned cp)
    {
        if (cp >= 'A' && cp <= 'Z')
            return cp + 32;
        if (cp < 0xC0)
            return cp;
        if (cp <= 0xDE)
            return cp == 0xD7 ? cp : cp + 32;  // Latin-1, but the multiplication sign
        if (cp >= 0x100 && cp <= 0x17F) {      // Latin Extended-A: pairs of an upper case and its lower case
            if (cp == 0x130)
                return 'i';
            if (cp == 0x178)
                return 0xFF;
            bool upperEven = (cp <= 0x137) || (cp >= 0x14A && cp <= 0x177);
            bool upperOdd = (cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E);
            if ((upperEven && cp % 2 == 0) || (upperOdd && cp % 2 == 1))
                return cp + 1;
            return cp;
        }
        if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2)  // Greek
            return cp + 32;
        if (cp == 0x386)
            return 0x3AC;
        if (cp >= 0x388 && cp <= 0x38A)
            return cp + 37;
        if (cp == 0x38C)
            return 0x3CC;
        if (cp == 0x38E || cp == 0x38F)
            return cp + 63;
        if (cp == 0x3C2)  // the final sigma
            return 0x3C3;
        if (cp >= 0x410 && cp <= 0x42F)  // Cyrillic
            return cp + 32;
        if (cp >= 0x400 && cp <= 0x40F)
            return cp + 80;
        return cp;
    }

    // A letter, a digit or '_' (any code point beyond ASCII, but the punctuation of Latin-1, the general punctuation
    // and that of CJK)
    bool IsWordChar(unsigned cp)
    {
        if (cp < 0x80)
            return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') || cp == '_';
        if (cp <= 0xBF || cp == 0xD7 || cp == 0xF7)
            return false;
        if ((cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x3000 && cp <= 0x303F))
            return false;
        return true;
    }

    struct Folded
    {
        std::string text;
        std::vector<size_t> origin;  // for each byte of text, the offset of its code point in the original; then its size
    };

    Folded Fold(const std::string& s, const SearchOptions& options)
    {
        Folded folded;
        folded.text.reserve(s.size());
        size_t i = 0;
        while (i < s.size()) {
            size_t start = i;
            unsigned cp = DecodeUtf8(s, i);
            if (!options.matchDiacritics)
                cp = WithoutDiacritics(cp);
            if (!options.matchCase)
                cp = Lower(cp);
            size_t before = folded.text.size();
            EncodeUtf8(cp, folded.text);
            folded.origin.insert(folded.origin.end(), folded.text.size() - before, start);
        }
        folded.origin.push_back(s.size());
        return folded;
    }

    // Whether the code point that ends at i (or starts at i) is a word character
    bool WordCharBefore(const std::string& s, size_t i)
    {
        if (i == 0)
            return false;
        size_t start = i - 1;
        while (start > 0 && ((unsigned char)s[start] & 0xC0) == 0x80)
            --start;
        return IsWordChar(DecodeUtf8(s, start));
    }
    bool WordCharAt(const std::string& s, size_t i)
    {
        return i < s.size() && IsWordChar(DecodeUtf8(s, i));
    }
}  // namespace

std::vector<TextMatch> FindMatches(const std::string& text, const std::string& query, const SearchOptions& options)
{
    std::vector<TextMatch> matches;
    if (query.empty())
        return matches;
    Folded folded = Fold(text, options);
    const std::string q = Fold(query, options).text;
    size_t at = folded.text.find(q);
    while (at != std::string::npos) {
        size_t end = at + q.size();
        if (!options.wholeWords || (!WordCharBefore(folded.text, at) && !WordCharAt(folded.text, end))) {
            matches.push_back({folded.origin[at], folded.origin[end]});
            at = folded.text.find(q, end);
        } else
            at = folded.text.find(q, at + 1);
    }
    return matches;
}

}  // namespace RichMd
