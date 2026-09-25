// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
// Narrative programming: the annotations of source files (::md, ::code) and the transclusions (![[file#name]]).
// Specification: docs/narrative_programming/narrative_programming_spec.md. No ImGui here: files are read through
// a callback.
#include "narrative_programming.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

namespace RichMd
{
namespace
{
    constexpr int kMaxDepth = 8;
    constexpr size_t npos = std::string::npos;

    std::string Trim(const std::string& s)
    {
        size_t b = s.find_first_not_of(" \t\r");
        if (b == npos)
            return "";
        size_t e = s.find_last_not_of(" \t\r");
        return s.substr(b, e - b + 1);
    }

    bool IsBlank(const std::string& s) { return s.find_first_not_of(" \t\r") == npos; }

    std::vector<std::string> Split(const std::string& text, char separator)
    {
        std::vector<std::string> parts(1);
        for (char c : text)
        {
            if (c == separator)
                parts.emplace_back();
            else
                parts.back() += c;
        }
        return parts;
    }

    std::vector<std::string> SplitLines(const std::string& text)
    {
        std::vector<std::string> lines = Split(text, '\n');
        if (!text.empty() && text.back() == '\n')
            lines.pop_back();
        for (auto& line : lines)
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
        return lines;
    }

    std::string JoinLines(const std::vector<std::string>& lines)
    {
        std::string r;
        for (const auto& line : lines)
            r += line + "\n";
        return r;
    }

    void TrimBlankLines(std::vector<std::string>& lines)
    {
        while (!lines.empty() && IsBlank(lines.back()))
            lines.pop_back();
        auto first = std::find_if(lines.begin(), lines.end(), [](const std::string& l) { return !IsBlank(l); });
        lines.erase(lines.begin(), first);
    }

    // Removes the longest common leading whitespace of the non-blank lines, compared char by char
    // (a tab never matches a space). Blank lines become empty.
    void Dedent(std::vector<std::string>& lines)
    {
        bool first = true;
        std::string common;
        for (const auto& line : lines)
        {
            if (IsBlank(line))
                continue;
            std::string indent = line.substr(0, line.find_first_not_of(" \t"));
            if (first)
                common = indent;
            size_t n = 0;
            while (n < common.size() && n < indent.size() && common[n] == indent[n])
                ++n;
            common.resize(n);
            first = false;
        }
        for (auto& line : lines)
            line = IsBlank(line) ? "" : line.substr(common.size());
    }

    // Files
    // -----

    enum class Syntax { Python, CLike, Markdown, Other };

    struct FileKind
    {
        Syntax syntax = Syntax::Other;
        std::string language;  // of its code blocks ("" if unknown)
    };

    FileKind KindOfFile(const std::string& path)
    {
        size_t dot = path.find_last_of('.'), slash = path.find_last_of("/\\");
        std::string ext = (dot == npos || (slash != npos && dot < slash)) ? "" : path.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        static const std::map<std::string, FileKind> table = {
            {"py", {Syntax::Python, "python"}}, {"pyi", {Syntax::Python, "python"}},
            {"c", {Syntax::CLike, "c"}}, {"h", {Syntax::CLike, "cpp"}}, {"cpp", {Syntax::CLike, "cpp"}},
            {"cc", {Syntax::CLike, "cpp"}}, {"cxx", {Syntax::CLike, "cpp"}}, {"hpp", {Syntax::CLike, "cpp"}},
            {"hh", {Syntax::CLike, "cpp"}}, {"glsl", {Syntax::CLike, "glsl"}}, {"vert", {Syntax::CLike, "glsl"}},
            {"frag", {Syntax::CLike, "glsl"}}, {"js", {Syntax::CLike, "javascript"}}, {"ts", {Syntax::CLike, "typescript"}},
            {"md", {Syntax::Markdown, ""}}, {"markdown", {Syntax::Markdown, ""}},
            {"json", {Syntax::Other, "json"}}, {"cmake", {Syntax::Other, "cmake"}}, {"txt", {Syntax::Other, ""}}};
        auto it = table.find(ext);
        return it == table.end() ? FileKind{Syntax::Other, ext} : it->second;
    }

    std::string DirectoryOf(const std::string& path)
    {
        size_t slash = path.find_last_of("/\\");
        return slash == npos ? "" : path.substr(0, slash + 1);
    }

    bool IsAbsolute(const std::string& path)
    {
        return (!path.empty() && (path[0] == '/' || path[0] == '\\')) || (path.size() > 1 && path[1] == ':');
    }

    // "a/b/../c/./d" -> "a/c/d", lexically (a leading ".." stays); the separators become '/'
    std::string NormalizePath(std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        std::vector<std::string> parts;
        for (const auto& part : Split(path, '/'))
        {
            if (part.empty() || part == ".")
                continue;
            if (part == ".." && !parts.empty() && parts.back() != "..")
                parts.pop_back();
            else
                parts.push_back(part);
        }
        std::string r = (!path.empty() && path[0] == '/') ? "/" : "";
        for (size_t i = 0; i < parts.size(); ++i)
            r += (i ? "/" : "") + parts[i];
        return r;
    }

    // Annotations
    // -----------

    enum class DirectiveKind { None, Md, Code, EndCode, EndMd };

    struct Directive
    {
        DirectiveKind kind = DirectiveKind::None;
        std::string name;
    };

    // `text` is a line without its comment token or its container opener: a directive is its first token
    Directive ParseDirective(const std::string& text)
    {
        static const std::pair<const char*, DirectiveKind> keywords[] = {
            {"::md", DirectiveKind::Md}, {"::code", DirectiveKind::Code},
            {"::endcode", DirectiveKind::EndCode}, {"::endmd", DirectiveKind::EndMd}};
        std::string t = Trim(text);
        for (const auto& [keyword, kind] : keywords)
        {
            size_t n = strlen(keyword);
            if (t.compare(0, n, keyword) == 0 && (t.size() == n || t[n] == ' ' || t[n] == '\t'))
                return {kind, Trim(t.substr(n))};
        }
        return {};
    }

    // The text after the line comment token ("#" or "//"), when the line is a line comment
    bool LineCommentText(const std::string& line, Syntax syntax, std::string& text)
    {
        const char* token = syntax == Syntax::Python ? "#" : syntax == Syntax::CLike ? "//" : nullptr;
        size_t b = line.find_first_not_of(" \t");
        if (!token || b == npos || line.compare(b, strlen(token), token) != 0)
            return false;
        text = line.substr(b + strlen(token));
        return true;
    }

    // A string or block comment that starts the line (after its indentation): its closing delimiter, and the text
    // after the opener. Python: """ or ''', with an optional prefix such as r. C-like: /* or /**.
    bool ContainerOpener(const std::string& line, Syntax syntax, std::string& closer, std::string& rest)
    {
        size_t b = line.find_first_not_of(" \t");
        if (b == npos)
            return false;
        if (syntax == Syntax::CLike && line.compare(b, 2, "/*") == 0)
        {
            closer = "*/";
            rest = line.substr(line.compare(b, 3, "/**") == 0 ? b + 3 : b + 2);
            return true;
        }
        if (syntax == Syntax::Python)
        {
            size_t q = b;
            while (q < line.size() && q - b < 2 && std::string("rRbBuUfF").find(line[q]) != npos)
                ++q;
            for (const char* quotes : {"\"\"\"", "'''"})
            {
                if (line.compare(q, 3, quotes) == 0)
                {
                    closer = quotes;
                    rest = line.substr(q + 3);
                    return true;
                }
            }
        }
        return false;
    }

    struct MdSection
    {
        std::string name;
        size_t line = 0;                 // of its ::md (0-based)
        std::vector<std::string> prose;  // extracted
        int code = -1;                   // its associated code region
    };

    struct CodeRegion
    {
        std::string name;            // empty: the associated code of a section
        int section = -1;            // the section it belongs to, for an associated code
        size_t line = 0;             // of its ::code
        size_t begin = 0, end = 0;   // its source lines: [begin, end)
    };

    struct ParsedFile
    {
        std::vector<std::string> lines;
        std::vector<bool> directiveOnly;  // the lines removed from the extracted code
        std::vector<MdSection> sections;
        std::vector<CodeRegion> regions;
        std::string error;                // the first annotation error: "file:line: message"
    };

    // Reads the annotations of a source file, line by line. Stops at the first error.
    struct Parser
    {
        const std::string& file;
        Syntax syntax;
        ParsedFile r;
        std::vector<int> openRegions;       // innermost last
        int proseSection = -1;              // a section whose prose (in line comments) is open
        std::vector<std::string> prose;     // its lines, comment tokens removed
        int redundantEndmd = -1;            // a section closed by the ::endcode just above: an ::endmd may follow

        Parser(const std::string& file_, Syntax syntax_) : file(file_), syntax(syntax_) {}

        bool Fail(size_t line, const std::string& message)
        {
            r.error = file + ":" + std::to_string(line + 1) + ": " + message;
            return false;
        }

        bool CheckName(size_t line, const std::string& name)
        {
            if (name.find_first_of("#|[]") != npos)
                return Fail(line, "'" + name + "': the characters # | [ ] are not allowed in names");
            return true;
        }

        bool NewSection(size_t line, const std::string& name)
        {
            if (name.empty())
                return Fail(line, "::md needs a name");
            if (!CheckName(line, name))
                return false;
            r.sections.push_back({name, line, {}, -1});
            return true;
        }

        void SetProse(MdSection& section, std::vector<std::string> lines)
        {
            Dedent(lines);
            TrimBlankLines(lines);
            section.prose = std::move(lines);
        }

        // `line` is the line of the ::code; the region starts at `begin`
        bool OpenRegion(size_t line, const std::string& name, int section, size_t begin)
        {
            if (name.empty() && section < 0)
                return Fail(line, "an unnamed ::code must follow the prose of a ::md section: name this region");
            if (!CheckName(line, name))
                return false;
            r.regions.push_back({name, section, line, begin, 0});
            if (section >= 0)
                r.sections[section].code = (int)r.regions.size() - 1;
            openRegions.push_back((int)r.regions.size() - 1);
            return true;
        }

        bool CloseRegion(size_t line, const std::string& name)
        {
            if (openRegions.empty())
                return Fail(line, "::endcode without an open ::code");
            CodeRegion& region = r.regions[openRegions.back()];
            if (!name.empty() && region.name.empty())
                return Fail(line, "the code of ::md " + r.sections[region.section].name + " closes with an unnamed ::endcode");
            if (!name.empty() && name != region.name)
                return Fail(line, "::endcode " + name + " closes ::code " + region.name);
            region.end = line;
            openRegions.pop_back();
            if (region.section >= 0)
                redundantEndmd = region.section;
            return true;
        }

        // A section in a string or block comment: its prose goes up to the end of the container, or up to ::code,
        // whose region starts after the container. `i` moves to the line of the closing delimiter.
        bool ContainerSection(size_t& i, const std::string& closer, const std::string& afterOpener)
        {
            size_t close = afterOpener.find(closer);
            std::string name = ParseDirective(afterOpener.substr(0, close)).name;
            if (!NewSection(i, name))
                return false;
            if (close != npos)
                return true;  // opened and closed on one line: an empty section
            int section = (int)r.sections.size() - 1;
            std::vector<std::string> lines;
            size_t codeLine = npos;
            size_t j = i + 1;
            for (; j < r.lines.size(); ++j)
            {
                size_t c = r.lines[j].find(closer);
                std::string content = r.lines[j].substr(0, c);
                if (codeLine == npos)
                {
                    Directive d = ParseDirective(content);
                    if (d.kind == DirectiveKind::Code && d.name.empty())
                    {
                        codeLine = j;
                        r.directiveOnly[j] = true;
                    }
                    else if (d.kind != DirectiveKind::None)
                        return Fail(j, "only an unnamed ::code may appear in the prose of ::md " + name);
                    else if (c == npos || !IsBlank(content))
                        lines.push_back(content);
                }
                if (c != npos)
                    break;
            }
            if (j == r.lines.size())
                return Fail(i, "::md " + name + ": its string or block comment is not closed");
            SetProse(r.sections[section], lines);
            i = j;
            return codeLine == npos || OpenRegion(codeLine, "", section, j + 1);
        }

        // A line inside the prose of a section written in line comments
        bool ProseLine(size_t i)
        {
            const std::string& name = r.sections[proseSection].name;
            std::string text;
            if (IsBlank(r.lines[i]))
            {
                prose.push_back("");
                return true;
            }
            if (!LineCommentText(r.lines[i], syntax, text))
                return Fail(i, "::md " + name + ": its prose is interrupted by source code (missing ::endmd or ::code?)");
            Directive d = ParseDirective(text);
            if (d.kind == DirectiveKind::None)
            {
                prose.push_back(text);
                return true;
            }
            r.directiveOnly[i] = true;
            if (d.kind == DirectiveKind::Md)
                return Fail(i, "::md " + d.name + " inside the prose of ::md " + name + " (missing ::endmd?)");
            if (d.kind == DirectiveKind::EndCode)
                return Fail(i, "::endcode inside the prose of ::md " + name);
            if (d.kind == DirectiveKind::Code && !d.name.empty())
                return Fail(i, "::code " + d.name + " inside the prose of ::md " + name + " (missing ::endmd?)");
            if (d.kind == DirectiveKind::EndMd && !d.name.empty() && d.name != name)
                return Fail(i, "::endmd " + d.name + " closes ::md " + name);
            int section = proseSection;
            SetProse(r.sections[section], prose);
            proseSection = -1;
            return d.kind == DirectiveKind::EndMd || OpenRegion(i, "", section, i + 1);
        }

        bool Line(size_t& i)
        {
            if (proseSection >= 0)
                return ProseLine(i);
            const std::string& line = r.lines[i];
            int redundant = redundantEndmd;
            if (!IsBlank(line))
                redundantEndmd = -1;
            std::string text, closer;
            if (LineCommentText(line, syntax, text))
            {
                Directive d = ParseDirective(text);
                if (d.kind == DirectiveKind::None)
                    return true;
                r.directiveOnly[i] = true;
                switch (d.kind)
                {
                case DirectiveKind::Md:
                    if (!NewSection(i, d.name))
                        return false;
                    proseSection = (int)r.sections.size() - 1;
                    prose.clear();
                    return true;
                case DirectiveKind::Code:
                    return OpenRegion(i, d.name, -1, i + 1);
                case DirectiveKind::EndCode:
                    return CloseRegion(i, d.name);
                default:  // ::endmd: only as a redundant close, right after the ::endcode of a section's code
                    if (redundant < 0)
                        return Fail(i, "::endmd without an open ::md");
                    if (!d.name.empty() && d.name != r.sections[redundant].name)
                        return Fail(i, "::endmd " + d.name + " closes ::md " + r.sections[redundant].name);
                    return true;
                }
            }
            if (ContainerOpener(line, syntax, closer, text))
            {
                Directive d = ParseDirective(text);
                if (d.kind == DirectiveKind::None)
                    return true;
                if (d.kind != DirectiveKind::Md)
                    return Fail(i, "a string or block comment may only start with ::md");
                return ContainerSection(i, closer, text);
            }
            return true;
        }

        bool Run()
        {
            for (size_t i = 0; i < r.lines.size(); ++i)
                if (!Line(i))
                    return false;
            if (proseSection >= 0)
                return Fail(r.sections[proseSection].line, "::md " + r.sections[proseSection].name + " is not closed (missing ::endmd)");
            if (!openRegions.empty())
            {
                const CodeRegion& region = r.regions[openRegions.back()];
                std::string what = region.name.empty() ? "the code of ::md " + r.sections[region.section].name : "::code " + region.name;
                return Fail(region.line, what + " is not closed (missing ::endcode)");
            }
            std::map<std::string, size_t> names;  // name -> line
            auto unique = [&](const std::string& name, size_t line) {
                auto [it, inserted] = names.emplace(name, line);
                return inserted || Fail(line, "the name '" + name + "' is already used, line " + std::to_string(it->second + 1));
            };
            for (const auto& s : r.sections)
                if (!unique(s.name, s.line))
                    return false;
            for (const auto& region : r.regions)
                if (!region.name.empty() && !unique(region.name, region.line))
                    return false;
            return true;
        }
    };

    ParsedFile Parse(const std::string& file, std::vector<std::string> lines, Syntax syntax)
    {
        Parser parser(file, syntax);
        parser.r.directiveOnly.assign(lines.size(), false);
        parser.r.lines = std::move(lines);
        parser.Run();
        return std::move(parser.r);
    }

    std::vector<std::string> ExtractCode(const ParsedFile& file, const CodeRegion& region)
    {
        std::vector<std::string> code;
        for (size_t i = region.begin; i < region.end; ++i)
            if (!file.directiveOnly[i])
                code.push_back(file.lines[i]);
        TrimBlankLines(code);
        Dedent(code);
        return code;
    }

    // A fenced code block, with a fence longer than any backtick run starting a line of the code
    std::string CodeBlock(const std::vector<std::string>& code, const std::string& language)
    {
        size_t longest = 0;
        for (const auto& line : code)
        {
            std::string t = Trim(line);
            longest = std::max(longest, std::min(t.size(), t.find_first_not_of('`')));
        }
        std::string fence(longest >= 3 ? longest + 1 : 3, '`');
        return fence + language + "\n" + JoinLines(code) + fence + "\n";
    }

    // Markdown documents
    // ------------------

    // Follows the fenced code blocks of markdown lines: Update() is true for a fence line or a line inside a fence
    struct FenceTracker
    {
        char fenceChar = 0;
        size_t fenceLength = 0;

        bool Update(const std::string& line)
        {
            std::string t = Trim(line);
            char c = t.empty() ? 0 : t[0];
            size_t n = (c == '`' || c == '~') ? std::min(t.size(), t.find_first_not_of(c)) : 0;
            if (fenceChar == 0)
            {
                if (n < 3)
                    return false;
                fenceChar = c;
                fenceLength = n;
                return true;
            }
            if (c == fenceChar && n >= fenceLength && IsBlank(t.substr(n)))
                fenceChar = 0;
            return true;
        }
    };

    struct Heading
    {
        size_t level;
        std::string text;
        size_t line;
    };

    // The ATX headings (# Title), outside the fenced code blocks
    std::vector<Heading> HeadingsOf(const std::vector<std::string>& lines)
    {
        std::vector<Heading> headings;
        FenceTracker fence;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const std::string& l = lines[i];
            if (fence.Update(l))
                continue;
            size_t b = l.find_first_not_of(' ');
            if (b == npos || b > 3 || l[b] != '#')
                continue;
            size_t e = std::min(l.size(), l.find_first_not_of('#', b));
            if (e - b > 6 || (e < l.size() && l[e] != ' ' && l[e] != '\t'))
                continue;
            std::string text = Trim(l.substr(e));
            size_t last = text.find_last_not_of('#');  // an optional closing sequence: "## Title ##"
            if (last == npos)
                text = "";
            else if (last + 1 < text.size() && (text[last] == ' ' || text[last] == '\t'))
                text = Trim(text.substr(0, last));
            headings.push_back({e - b, text, i});
        }
        return headings;
    }

    // The lines under a path of headings (Setup, then Linux under it): [begin, end). `missing`: the step not found.
    bool FindHeading(const std::vector<std::string>& lines, const std::vector<std::string>& path, size_t& begin, size_t& end,
                     std::string& missing)
    {
        auto headings = HeadingsOf(lines);
        size_t from = 0;
        begin = 0;
        end = lines.size();
        for (const auto& step : path)
        {
            auto h = std::find_if(headings.begin(), headings.end(),
                                  [&](const Heading& x) { return x.line >= from && x.line < end && x.text == step; });
            if (h == headings.end())
            {
                missing = step;
                return false;
            }
            auto next = std::find_if(h + 1, headings.end(), [&](const Heading& x) { return x.level <= h->level; });
            begin = h->line;
            end = std::min(end, next == headings.end() ? lines.size() : next->line);
            from = h->line + 1;
        }
        return true;
    }

    // Transclusions
    // -------------

    // An embed alone on its line: ![[target]]
    bool IsEmbed(const std::string& line, std::string& target)
    {
        std::string t = Trim(line);
        if (t.size() < 5 || t.compare(0, 3, "![[") != 0 || t.compare(t.size() - 2, 2, "]]") != 0)
            return false;
        target = t.substr(3, t.size() - 5);
        return target.find("[[") == npos && target.find("]]") == npos;
    }

    std::string ErrorSpan(const std::string& line, const std::string& reason)
    {
        std::string title = reason;
        std::replace(title.begin(), title.end(), '"', '\'');
        return "<md-error title=\"" + title + "\">`" + Trim(line) + "`</md-error>\n";
    }

    struct Resolver
    {
        const ReadTextFile& readFile;
        std::map<std::string, std::optional<std::string>> files;  // read once
        std::vector<std::string> visiting;                        // the transclusions being expanded ("file#target")

        const std::optional<std::string>& Read(const std::string& path)
        {
            auto it = files.find(path);
            if (it == files.end())
                it = files.emplace(path, readFile(path)).first;
            return it->second;
        }

        // Markdown lines, their embeds replaced. A transcluded block is separated from its neighbours by a blank line.
        std::string Resolve(const std::vector<std::string>& lines, const std::string& currentFile, int depth)
        {
            std::string out;
            bool blankNeeded = false;
            FenceTracker fence;
            for (const auto& line : lines)
            {
                std::string target;
                if (fence.Update(line) || !IsEmbed(line, target))
                {
                    if (blankNeeded && !IsBlank(line))
                        out += "\n";
                    blankNeeded = false;
                    out += line + "\n";
                    continue;
                }
                std::string block = Transclude(target, line, currentFile, depth);
                if (block.empty())
                    continue;
                bool afterBlankLine = out == "\n" || (out.size() >= 2 && out.compare(out.size() - 2, 2, "\n\n") == 0);
                if (!out.empty() && !afterBlankLine)
                    out += "\n";
                out += block;
                blankNeeded = true;
            }
            return out;
        }

        std::string Transclude(const std::string& target, const std::string& line, const std::string& currentFile, int depth)
        {
            auto error = [&](const std::string& reason) { return ErrorSpan(line, reason); };
            std::vector<std::string> steps = Split(target, '#');
            std::string path = steps.front();
            steps.erase(steps.begin());
            bool codeSelector = !steps.empty() && steps.back() == "code";
            if (codeSelector)
                steps.pop_back();
            if (codeSelector && steps.empty())
                return error("#code follows the name of a Markdown section");
            if (std::find(steps.begin(), steps.end(), "") != steps.end() || (path.empty() && steps.empty()))
                return error("empty name");
            if (path.empty() && currentFile.empty())
                return error("![[#...]] refers to the current file, and this text comes from no file");
            if (depth >= kMaxDepth)
                return error("transclusions nested too deeply");
            std::string file = path.empty() ? currentFile : (IsAbsolute(path) ? path : DirectoryOf(currentFile) + path);
            file = NormalizePath(file);
            std::string key = file + target.substr(path.size());
            if (std::find(visiting.begin(), visiting.end(), key) != visiting.end())
                return error("transclusion cycle: " + key);
            const auto& content = Read(file);
            if (!content)
                return error("file not found: " + file);
            visiting.push_back(key);
            std::string block = TranscludeFrom(file, SplitLines(*content), steps, codeSelector, line, depth);
            visiting.pop_back();
            return block;
        }

        std::string TranscludeFrom(const std::string& file, std::vector<std::string> lines, const std::vector<std::string>& steps,
                                   bool codeSelector, const std::string& line, int depth)
        {
            auto error = [&](const std::string& reason) { return ErrorSpan(line, reason); };
            FileKind kind = KindOfFile(file);
            if (kind.syntax == Syntax::Markdown)
            {
                if (codeSelector)
                    return error("#code: a Markdown document has no associated code");
                if (!steps.empty())
                {
                    size_t begin, end;
                    std::string missing;
                    if (!FindHeading(lines, steps, begin, end, missing))
                        return error("no heading '" + missing + "' in " + file);
                    lines = std::vector<std::string>(lines.begin() + (long)begin, lines.begin() + (long)end);
                }
                TrimBlankLines(lines);
                return Resolve(lines, file, depth + 1);
            }

            ParsedFile parsed = Parse(file, std::move(lines), kind.syntax);
            if (!parsed.error.empty())
                return error(parsed.error);
            if (steps.empty())  // the whole file, as it is (with its annotations)
            {
                std::vector<std::string> code = parsed.lines;
                TrimBlankLines(code);
                return CodeBlock(code, kind.language);
            }
            if (steps.size() > 1)
                return error("a source file has no headings: its targets are single names");
            const std::string& name = steps[0];
            for (const auto& s : parsed.sections)
            {
                if (s.name != name)
                    continue;
                if (!codeSelector)
                    return Resolve(s.prose, file, depth + 1);
                if (s.code < 0)
                    return error("::md " + name + " has no associated code");
                return CodeBlock(ExtractCode(parsed, parsed.regions[s.code]), kind.language);
            }
            for (const auto& region : parsed.regions)
            {
                if (region.name != name)
                    continue;
                if (codeSelector)
                    return error("#code selects the code of a Markdown section, and '" + name + "' is a code region");
                return CodeBlock(ExtractCode(parsed, region), kind.language);
            }
            return error("no section or code region named '" + name + "' in " + file);
        }
    };
}  // namespace

std::string ResolveTransclusions(const std::string& markdown, const ReadTextFile& readFile, const std::string& currentFile)
{
    if (markdown.find("![[") == std::string::npos)
        return markdown;
    Resolver resolver{readFile, {}, {}};
    return resolver.Resolve(SplitLines(markdown), currentFile, 0);
}

}  // namespace RichMd
