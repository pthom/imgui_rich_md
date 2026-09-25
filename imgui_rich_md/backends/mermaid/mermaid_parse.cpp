// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
// Mermaid: the parsers. A line is scanned by hand (no std::regex); a line that is not understood is an error,
// except the styling and interaction lines, which are skipped.
#include "mermaid_model.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <string_view>

namespace RichMd::Mermaid
{
    namespace
    {
        using std::string_view;

        string_view Trim(string_view s)
        {
            while (!s.empty() && std::isspace((unsigned char)s.front()))
                s.remove_prefix(1);
            while (!s.empty() && std::isspace((unsigned char)s.back()))
                s.remove_suffix(1);
            return s;
        }

        bool StartsWith(string_view s, string_view prefix) { return s.substr(0, prefix.size()) == prefix; }
        bool EndsWith(string_view s, string_view suffix)
        {
            return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
        }

        // Identifiers: letters, digits, '_', and any non-ASCII character (UTF-8)
        bool IsWordChar(char c) { return std::isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80; }

        // A cursor on a line
        struct Scanner
        {
            string_view s;
            size_t i = 0;

            bool AtEnd() const { return i >= s.size(); }
            char Peek(size_t offset = 0) const { return i + offset < s.size() ? s[i + offset] : '\0'; }
            void SkipSpaces()
            {
                while (i < s.size() && std::isspace((unsigned char)s[i]))
                    ++i;
            }
            string_view Word()
            {
                size_t start = i;
                while (i < s.size() && IsWordChar(s[i]))
                    ++i;
                return s.substr(start, i - start);
            }
            bool Eat(string_view token)
            {
                if (s.substr(i, token.size()) != token)
                    return false;
                i += token.size();
                return true;
            }
            string_view Rest() const { return i < s.size() ? s.substr(i) : string_view(); }
        };

        // The first word of a line, and what follows it
        string_view FirstWord(string_view line)
        {
            size_t n = 0;
            while (n < line.size() && !std::isspace((unsigned char)line[n]))
                ++n;
            return line.substr(0, n);
        }
        string_view AfterFirstWord(string_view line) { return Trim(line.substr(FirstWord(line).size())); }

        // The source split in lines, without the %% comments; lines are numbered from 1
        struct SourceLine
        {
            int number;
            string_view text;
        };
        std::vector<SourceLine> Lines(const std::string& source)
        {
            std::vector<SourceLine> lines;
            string_view rest(source);
            int number = 0;
            while (true)
            {
                ++number;
                size_t eol = rest.find('\n');
                string_view line = rest.substr(0, eol);
                size_t comment = line.find("%%");
                if (comment != string_view::npos)
                    line = line.substr(0, comment);
                line = Trim(line);
                if (!line.empty())
                    lines.push_back({number, line});
                if (eol == string_view::npos)
                    break;
                rest.remove_prefix(eol + 1);
            }
            return lines;
        }

        std::string LineError(int number, const std::string& message)
        {
            return "line " + std::to_string(number) + ": " + message;
        }

        // `key: text`, or `key {` (a block up to a line `}`), with spaces allowed before the colon or the brace
        bool IsKeyLine(string_view text, string_view key, char* opener)
        {
            if (!StartsWith(text, key))
                return false;
            string_view rest = Trim(text.substr(key.size()));
            *opener = rest.empty() ? '\0' : rest.front();
            return *opener == ':' || *opener == '{';
        }

        // The lines Mermaid reads but that are not drawn here: a YAML front matter at the start (--- ... ---: a title,
        // a configuration), and the accessibility lines (accTitle: ..., accDescr: ..., accDescr { ... })
        bool SkipUndrawnLines(std::vector<SourceLine>& lines, std::string* error)
        {
            if (!lines.empty() && lines[0].text == "---")
            {
                size_t close = 1;
                while (close < lines.size() && lines[close].text != "---")
                    ++close;
                if (close == lines.size())
                {
                    *error = LineError(lines[0].number, "the front matter is not closed (---)");
                    return false;
                }
                lines.erase(lines.begin(), lines.begin() + (long)close + 1);
            }
            std::vector<SourceLine> kept;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                char opener = 0;
                if (IsKeyLine(lines[i].text, "accTitle", &opener) || IsKeyLine(lines[i].text, "accDescr", &opener))
                {
                    if (opener == '{' && lines[i].text.back() != '}')  // a block: up to its closing brace
                        while (i + 1 < lines.size() && lines[i].text.back() != '}')
                            ++i;
                    continue;
                }
                kept.push_back(lines[i]);
            }
            lines = kept;
            return true;
        }

        bool IsStylingLine(string_view word)
        {
            for (string_view w : {"classDef", "class", "cssClass", "style", "linkStyle", "click", "callback", "link"})
                if (word == w)
                    return true;
            return false;
        }

        int FindNode(const Graph& graph, string_view id)
        {
            for (size_t i = 0; i < graph.nodes.size(); ++i)
                if (graph.nodes[i].id == id)
                    return (int)i;
            return -1;
        }

        int FindSubgraph(const Graph& graph, string_view id)
        {
            for (size_t i = 0; i < graph.subgraphs.size(); ++i)
                if (graph.subgraphs[i].id == id)
                    return (int)i;
            return -1;
        }

        // A subgraph (or a namespace), created at its first mention; its title is the last one given
        int AddSubgraph(Graph& graph, string_view id, const std::string& title, int parent = -1)
        {
            int index = FindSubgraph(graph, id);
            if (index < 0)
            {
                graph.subgraphs.emplace_back();
                graph.subgraphs.back().id = std::string(id);
                graph.subgraphs.back().parent = parent;
                index = (int)graph.subgraphs.size() - 1;
            }
            graph.subgraphs[index].title = title;
            return index;
        }

        // A node, created at its first mention; a node first seen outside a subgraph joins the first one that mentions
        // it
        int AddNode(Graph& graph, string_view id, int subgraph)
        {
            int index = FindNode(graph, id);
            if (index < 0)
            {
                Node node;
                node.id = node.label = std::string(id);
                node.subgraph = subgraph;
                graph.nodes.push_back(node);
                return (int)graph.nodes.size() - 1;
            }
            if (subgraph >= 0 && graph.nodes[index].subgraph < 0)
                graph.nodes[index].subgraph = subgraph;
            return index;
        }

        // ---------------------------------------------------------------------------------------------------------
        // Flowcharts
        // ---------------------------------------------------------------------------------------------------------

        // A label as written in the source: without its quotes, with its line breaks (<br>) and entity codes (#quot;,
        // #35;)
        std::string CleanLabel(string_view text)
        {
            text = Trim(text);
            if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
                text = text.substr(1, text.size() - 2);
            std::string out;
            for (size_t i = 0; i < text.size();)
            {
                if (text[i] == '<')  // <br>, <br/>, <br />
                {
                    size_t close = text.find('>', i);
                    std::string tag;
                    for (size_t k = i + 1; close != string_view::npos && k < close; ++k)
                        if (!std::isspace((unsigned char)text[k]) && text[k] != '/')
                            tag += (char)std::tolower((unsigned char)text[k]);
                    if (tag == "br")
                    {
                        out += '\n';
                        i = close + 1;
                        continue;
                    }
                }
                if (text[i] == '#')  // #quot; #35;
                {
                    size_t semicolon = text.find(';', i);
                    if (semicolon != string_view::npos && semicolon - i <= 8)
                    {
                        string_view name = text.substr(i + 1, semicolon - i - 1);
                        const char* named = name == "quot"   ? "\""
                                            : name == "amp"  ? "&"
                                            : name == "lt"   ? "<"
                                            : name == "gt"   ? ">"
                                            : name == "nbsp" ? " "
                                                             : nullptr;
                        unsigned code = 0;
                        bool numeric = !name.empty();
                        for (char c : name)
                            numeric = numeric && std::isdigit((unsigned char)c), code = code * 10 + (unsigned)(c - '0');
                        if (named || (numeric && code >= 32 && code < 0x110000))
                        {
                            if (named)
                                out += named;
                            else if (code < 0x80)
                                out += (char)code;
                            else if (code < 0x800)
                                out += (char)(0xC0 | (code >> 6)), out += (char)(0x80 | (code & 0x3F));
                            else if (code < 0x10000)
                                out += (char)(0xE0 | (code >> 12)), out += (char)(0x80 | ((code >> 6) & 0x3F)),
                                    out += (char)(0x80 | (code & 0x3F));
                            else
                                out += (char)(0xF0 | (code >> 18)), out += (char)(0x80 | ((code >> 12) & 0x3F)),
                                    out += (char)(0x80 | ((code >> 6) & 0x3F)), out += (char)(0x80 | (code & 0x3F));
                            i = semicolon + 1;
                            continue;
                        }
                    }
                }
                out += text[i++];
            }
            return out;
        }

        // A node reference: an id, optionally its shape and label (A[label], A(label), A{label}, ...; a label may be
        // quoted), and a :::class suffix (ignored)
        bool ScanNodeRef(Scanner& sc, Graph& graph, int subgraph, int* nodeIndex)
        {
            sc.SkipSpaces();
            string_view id = sc.Word();
            if (id.empty())
                return false;
            *nodeIndex = AddNode(graph, id, subgraph);
            const size_t afterId = sc.i;
            sc.SkipSpaces();
            const size_t opener = sc.i;
            struct Bracket
            {
                string_view open, close;
                NodeShape shape;
            };
            static const Bracket brackets[] = {
                // the longest openers first
                {"(((", ")))", NodeShape::DoubleCircle},
                {"((", "))", NodeShape::Circle},
                {"([", "])", NodeShape::Stadium},
                {"[[", "]]", NodeShape::Subroutine},
                {"[(", ")]", NodeShape::Cylinder},
                {"{{", "}}", NodeShape::Hexagon},
                {"[/", "/]", NodeShape::Parallelogram},
                {"[/", "\\]", NodeShape::Trapezoid},
                {"[\\", "\\]", NodeShape::ParallelogramAlt},
                {"[\\", "/]", NodeShape::TrapezoidAlt},
                {"[", "]", NodeShape::Rect},
                {"(", ")", NodeShape::Rounded},
                {"{", "}", NodeShape::Diamond},
                {">", "]", NodeShape::Asymmetric},
            };
            bool matched = false;
            for (const Bracket& b : brackets)
            {
                sc.i = opener;
                if (!sc.Eat(b.open))
                    continue;
                size_t labelStart = sc.i, labelEnd;
                sc.SkipSpaces();
                if (sc.Peek() == '"')  // a quoted label may contain the closing bracket
                {
                    size_t quote = sc.s.find('"', sc.i + 1);
                    if (quote == string_view::npos)
                        continue;
                    sc.i = quote + 1;
                    sc.SkipSpaces();
                    if (sc.s.substr(sc.i, b.close.size()) != b.close)
                        continue;
                    labelEnd = sc.i;
                }
                else
                {
                    labelEnd = sc.s.find(b.close, labelStart);
                    if (labelEnd == string_view::npos)
                        continue;
                }
                Node& node = graph.nodes[*nodeIndex];
                node.label = CleanLabel(sc.s.substr(labelStart, labelEnd - labelStart));
                node.shape = b.shape;
                sc.i = labelEnd + b.close.size();
                matched = true;
                break;
            }
            if (!matched)
                sc.i = afterId;  // no shape (an opener without its closer is left for the caller to reject)
            if (sc.Eat(":::"))
                sc.Word();
            return true;
        }

        // A group of nodes: A, or A & B & C
        bool ScanGroup(Scanner& sc, Graph& graph, int subgraph, std::vector<int>* nodes)
        {
            do
            {
                int node;
                if (!ScanNodeRef(sc, graph, subgraph, &node))
                    return false;
                nodes->push_back(node);
                sc.SkipSpaces();
            } while (sc.Eat("&"));
            return true;
        }

        // The end of a link's line: >, o or x (an o or an x followed by a letter is the next node, unless `strict`)
        bool ScanLinkEnd(Scanner& sc, Edge* edge, bool strict)
        {
            char c = sc.Peek();
            if (c == '>')
                edge->end = EdgeEnd::Arrow;
            else if ((c == 'o' || c == 'x') && (!strict || !IsWordChar(sc.Peek(1))))
                edge->end = c == 'o' ? EdgeEnd::Circle : EdgeEnd::Cross;
            else
                return false;
            ++sc.i;
            return true;
        }

        // A link: an optional start marker (<, o, x), a line (-- solid, == thick, -.- dotted, ~~~ invisible, of any
        // length), an optional end marker (>, o, x). Its text goes inside (A -- text --> B) or after (A -->|text| B).
        bool ScanLink(Scanner& sc, Edge* edge)
        {
            sc.SkipSpaces();
            edge->end = EdgeEnd::None;
            if (sc.Eat("~~~"))
            {
                while (sc.Eat("~"))
                {}
                edge->style = LineStyle::Invisible;
            }
            else
            {
                char c = sc.Peek(), next = sc.Peek(1);
                if ((c == '<' || c == 'o' || c == 'x') && (next == '-' || next == '=' || next == '.'))
                {
                    edge->start = c == '<' ? EdgeEnd::Arrow : c == 'o' ? EdgeEnd::Circle : EdgeEnd::Cross;
                    ++sc.i;
                    c = next;
                }
                if (c == '-' && sc.Peek(1) == '.')  // dotted: -.-, -.->, -..->, or -. text .->
                {
                    ++sc.i;
                    int dots = 0;
                    while (sc.Eat("."))
                        ++dots;
                    edge->style = LineStyle::Dotted;
                    if (sc.Eat("-"))
                        ScanLinkEnd(sc, edge, true);
                    else if (dots == 1 && std::isspace((unsigned char)sc.Peek()))
                    {
                        size_t close = sc.s.find(".-", sc.i);
                        if (close == string_view::npos)
                            return false;
                        edge->label = CleanLabel(sc.s.substr(sc.i, close - sc.i));
                        sc.i = close;
                        while (sc.Eat("."))
                        {}
                        if (!sc.Eat("-"))
                            return false;
                        ScanLinkEnd(sc, edge, true);
                    }
                    else
                        return false;
                }
                else if (c == '-' || c == '=')  // solid or thick, of any length, or -- text -->
                {
                    int n = 0;
                    while (sc.Peek() == c)
                        ++sc.i, ++n;
                    if (n < 2)
                        return false;
                    edge->style = c == '=' ? LineStyle::Thick : LineStyle::Solid;
                    if (!ScanLinkEnd(sc, edge, n > 2) && n == 2)
                    {
                        if (!std::isspace((unsigned char)sc.Peek()))
                            return false;
                        size_t close = sc.s.find(std::string(2, c), sc.i);  // the closing line
                        if (close == string_view::npos)
                            return false;
                        edge->label = CleanLabel(sc.s.substr(sc.i, close - sc.i));
                        sc.i = close;
                        n = 0;
                        while (sc.Peek() == c)
                            ++sc.i, ++n;
                        if (!ScanLinkEnd(sc, edge, true) && n < 3)
                            return false;
                    }
                }
                else
                    return false;
            }
            sc.SkipSpaces();
            if (sc.Eat("|"))
            {
                size_t close = sc.s.find('|', sc.i);
                if (close == string_view::npos)
                    return false;
                edge->label = CleanLabel(sc.s.substr(sc.i, close - sc.i));
                sc.i = close + 1;
            }
            return true;
        }

        void ParseFlowchart(const std::vector<SourceLine>& lines, Diagram& d)
        {
            Graph& graph = d.graph;
            std::vector<int> open;  // the open subgraphs, the innermost last
            for (const SourceLine& line : lines)
            {
                const int subgraph = open.empty() ? -1 : open.back();
                string_view word = FirstWord(line.text);
                if (word == "graph" || word == "flowchart" || word == "flowchart-elk")
                {
                    string_view direction = AfterFirstWord(line.text);
                    if (direction.empty() || direction == "TD" || direction == "TB" || direction == "BT")
                        graph.vertical = true, graph.reversed = direction == "BT";
                    else if (direction == "LR" || direction == "RL")
                        graph.vertical = false, graph.reversed = direction == "RL";
                    else
                    {
                        d.error = LineError(line.number, "unknown direction `" + std::string(direction) + "`");
                        return;
                    }
                    continue;
                }
                if (word == "subgraph")
                {
                    // subgraph id, subgraph id[title], or subgraph some title
                    string_view rest = AfterFirstWord(line.text);
                    Scanner sc{rest};
                    string_view id = sc.Word();
                    sc.SkipSpaces();
                    std::string title;
                    if (!id.empty() && sc.AtEnd())
                        title = std::string(id);
                    else if (!id.empty() && sc.Eat("[") && EndsWith(rest, "]"))
                        title = CleanLabel(rest.substr(sc.i, rest.size() - 1 - sc.i));
                    else
                        id = rest, title = CleanLabel(rest);
                    open.push_back(AddSubgraph(graph, id, title, subgraph));
                    continue;
                }
                if (line.text == "end")
                {
                    if (!open.empty())
                        open.pop_back();
                    continue;
                }
                if (word == "direction" && subgraph >= 0)
                {
                    Subgraph& sg = graph.subgraphs[subgraph];
                    string_view direction = AfterFirstWord(line.text);
                    if (direction == "TD" || direction == "TB" || direction == "BT")
                        sg.vertical = true, sg.reversed = direction == "BT";
                    else if (direction == "LR" || direction == "RL")
                        sg.vertical = false, sg.reversed = direction == "RL";
                    else
                    {
                        d.error = LineError(line.number, "unknown direction `" + std::string(direction) + "`");
                        return;
                    }
                    sg.hasDirection = true;
                    continue;
                }
                if (IsStylingLine(word))
                    continue;
                // Nodes, and links between them: A --> B & C --> D (every node of a group to every node of the next)
                Scanner sc{line.text};
                std::vector<int> left;
                bool ok = ScanGroup(sc, graph, subgraph, &left);
                while (ok && !sc.AtEnd())
                {
                    Edge link;
                    std::vector<int> right;
                    ok = ScanLink(sc, &link) && ScanGroup(sc, graph, subgraph, &right);
                    for (int a : left)
                        for (int b : right)
                        {
                            Edge edge = link;
                            edge.src = a, edge.dst = b;
                            graph.edges.push_back(edge);
                        }
                    left = right;
                }
                if (!ok)
                {
                    d.error = LineError(line.number, "cannot parse `" + std::string(line.text) + "`");
                    return;
                }
            }

            // A link to (or from) the id of a subgraph that has nodes ends on its box: the node the id made goes away
            for (size_t b = 0; b < graph.subgraphs.size(); ++b)
            {
                int phantom = FindNode(graph, graph.subgraphs[b].id);
                bool hasMembers = false;
                for (const Node& nd : graph.nodes)
                    for (int s = nd.subgraph; s >= 0 && !hasMembers; s = graph.subgraphs[s].parent)
                        hasMembers = s == (int)b;
                if (phantom < 0 || !hasMembers)
                    continue;
                for (Edge& e : graph.edges)
                {
                    if (e.src == phantom)
                        e.srcBox = (int)b, e.src = -1;
                    if (e.dst == phantom)
                        e.dstBox = (int)b, e.dst = -1;
                    e.src -= e.src > phantom ? 1 : 0;
                    e.dst -= e.dst > phantom ? 1 : 0;
                }
                graph.nodes.erase(graph.nodes.begin() + phantom);
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // Sequence diagrams
        // ---------------------------------------------------------------------------------------------------------

        int AddParticipant(Sequence& seq, string_view id, string_view label = {}, bool actor = false)
        {
            for (size_t i = 0; i < seq.participantIds.size(); ++i)
                if (seq.participantIds[i] == id)
                {
                    if (!label.empty())
                        seq.participantLabels[i] = CleanLabel(label);
                    seq.participantIsActor[i] = seq.participantIsActor[i] || actor;
                    return (int)i;
                }
            seq.participantIds.emplace_back(id);
            seq.participantLabels.push_back(CleanLabel(label.empty() ? id : label));
            seq.participantIsActor.push_back(actor);
            return (int)seq.participantIds.size() - 1;
        }

        bool EqualsNoCase(string_view a, string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
                    return false;
            return true;
        }

        // Note over A,B: text / Note left of A: text / Note right of A: text
        bool ScanNote(string_view line, Sequence& seq)
        {
            if (!EqualsNoCase(FirstWord(line), "note"))
                return false;
            string_view rest = AfterFirstWord(line);
            SequenceRow row;
            row.isNote = true;
            bool found = false;
            const std::pair<string_view, int> placements[] = {{"over", 0}, {"right of", 1}, {"left of", -1}};
            for (const auto& [where, placement] : placements)
                if (rest.size() > where.size() && EqualsNoCase(rest.substr(0, where.size()), where)
                    && std::isspace((unsigned char)rest[where.size()]))
                {
                    rest = Trim(rest.substr(where.size()));
                    row.notePlacement = placement;
                    found = true;
                    break;
                }
            size_t colon = rest.find(':');
            if (!found || colon == string_view::npos)
                return false;
            string_view names = rest.substr(0, colon);
            while (!names.empty())
            {
                size_t comma = names.find(',');
                string_view name = Trim(names.substr(0, comma));
                if (name.empty())
                    return false;
                row.over.push_back(AddParticipant(seq, name));
                if (comma == string_view::npos)
                    break;
                names.remove_prefix(comma + 1);
            }
            if (row.notePlacement != 0 && row.over.size() != 1)
                return false;
            row.text = CleanLabel(rest.substr(colon + 1));
            seq.rows.push_back(row);
            return true;
        }

        // The state of the parser of a sequence diagram
        struct SequenceParser
        {
            Sequence& seq;
            std::vector<std::pair<int, int>> openFrames;      // index in seq.frames, line of the opening
            std::map<int, std::vector<int>> openActivations;  // participant -> indices in seq.activations
            int nextNumber = 0, numberStep = 1;               // autonumber (0: off)

            void Activate(int participant, int row)
            {
                SequenceActivation a;
                a.participant = participant;
                a.startRow = row;
                openActivations[participant].push_back((int)seq.activations.size());
                seq.activations.push_back(a);
            }
            bool Deactivate(int participant, int row)
            {
                auto& open = openActivations[participant];
                if (open.empty())
                    return false;
                seq.activations[open.back()].endRow = row;
                open.pop_back();
                return true;
            }
        };

        // A->>B: text, A-->>+B: text (activates B), B-->>-A: text (deactivates B)
        // The arrows: ->, --> (no head), ->>, -->> (filled), -x, --x (cross), -), --) (open), <<->>, <<-->> (both)
        bool ScanMessage(string_view line, SequenceParser& parser, std::string* error)
        {
            Sequence& seq = parser.seq;
            struct ArrowToken
            {
                string_view token;
                bool dashed;
                SequenceArrow start, end;
            };
            static const ArrowToken arrows[] = {
                {"<<-->>", true, SequenceArrow::Filled, SequenceArrow::Filled},
                {"<<->>", false, SequenceArrow::Filled, SequenceArrow::Filled},
                {"-->>", true, SequenceArrow::None, SequenceArrow::Filled},
                {"->>", false, SequenceArrow::None, SequenceArrow::Filled},
                {"--x", true, SequenceArrow::None, SequenceArrow::Cross},
                {"-x", false, SequenceArrow::None, SequenceArrow::Cross},
                {"--)", true, SequenceArrow::None, SequenceArrow::Open},
                {"-)", false, SequenceArrow::None, SequenceArrow::Open},
                {"-->", true, SequenceArrow::None, SequenceArrow::None},
                {"->", false, SequenceArrow::None, SequenceArrow::None},
            };
            Scanner sc{line};
            string_view src = sc.Word();
            sc.SkipSpaces();
            const ArrowToken* found = nullptr;
            for (const ArrowToken& a : arrows)  // the longest token that matches
                if (sc.Rest().substr(0, a.token.size()) == a.token && (!found || a.token.size() > found->token.size()))
                    found = &a;
            if (src.empty() || !found)
                return false;
            sc.i += found->token.size();
            sc.SkipSpaces();
            bool activate = sc.Eat("+"), deactivate = !activate && sc.Eat("-");
            sc.SkipSpaces();
            string_view dst = sc.Word();
            sc.SkipSpaces();
            if (dst.empty() || !sc.Eat(":"))
                return false;
            SequenceRow row;
            row.dashed = found->dashed;
            row.arrowStart = found->start;
            row.arrowEnd = found->end;
            row.src = AddParticipant(seq, src);
            row.dst = AddParticipant(seq, dst);
            row.text = CleanLabel(sc.Rest());
            if (parser.nextNumber > 0)
            {
                row.number = parser.nextNumber;
                parser.nextNumber += parser.numberStep;
            }
            int index = (int)seq.rows.size();
            seq.rows.push_back(row);
            if (activate)
                parser.Activate(row.dst, index);
            if (deactivate && !parser.Deactivate(row.src, index))
            {
                *error = "`" + std::string(src) + "` is not active";
                return false;
            }
            return true;
        }

        // rgb(r, g, b) or rgba(r, g, b, a); 0 for another color (drawn with a neutral tint)
        ImU32 ParseColor(string_view text)
        {
            text = Trim(text);
            size_t open = text.find('('), close = text.rfind(')');
            if (open == string_view::npos || close == string_view::npos || close < open)
                return 0;
            std::string numbers(text.substr(open + 1, close - open - 1));
            float v[4] = {0.f, 0.f, 0.f, 1.f};
            int count = 0;
            const char* p = numbers.c_str();
            while (count < 4)
            {
                char* end = nullptr;
                float value = std::strtof(p, &end);
                if (end == p)
                    break;
                v[count++] = value;
                p = end;
                while (*p == ',' || std::isspace((unsigned char)*p))
                    ++p;
            }
            if (count < 3)
                return 0;
            return ImGui::ColorConvertFloat4ToU32(ImVec4(v[0] / 255.f, v[1] / 255.f, v[2] / 255.f, v[3]));
        }

        void ParseSequence(const std::vector<SourceLine>& lines, Diagram& d)
        {
            Sequence& seq = d.sequence;
            SequenceParser parser{seq, {}, {}};
            for (const SourceLine& line : lines)
            {
                string_view word = FirstWord(line.text);
                string_view rest = AfterFirstWord(line.text);
                if (word == "sequenceDiagram" || word == "title" || StartsWith(word, "title:"))  // a title: not drawn
                    continue;
                if (word == "participant" || word == "actor")
                {
                    size_t as = rest.find(" as ");
                    if (as == string_view::npos)
                        AddParticipant(seq, Trim(rest), {}, word == "actor");
                    else
                        AddParticipant(seq, Trim(rest.substr(0, as)), Trim(rest.substr(as + 4)), word == "actor");
                    continue;
                }
                if (word == "autonumber")
                {
                    Scanner sc{rest};
                    string_view start = sc.Word();
                    sc.SkipSpaces();
                    string_view step = sc.Word();
                    parser.nextNumber = start.empty() ? 1 : std::atoi(std::string(start).c_str());
                    parser.numberStep = step.empty() ? 1 : std::atoi(std::string(step).c_str());
                    continue;
                }
                if (word == "activate" || word == "deactivate")
                {
                    int participant = AddParticipant(seq, Trim(rest));
                    int row = (int)seq.rows.size() - 1;  // the message just before
                    if (word == "activate")
                        parser.Activate(participant, row);
                    else if (!parser.Deactivate(participant, row))
                    {
                        d.error = LineError(line.number, "`" + std::string(Trim(rest)) + "` is not active");
                        return;
                    }
                    continue;
                }
                if (word == "loop" || word == "alt" || word == "opt" || word == "par" || word == "critical"
                    || word == "break" || word == "rect")
                {
                    SequenceFrame frame;
                    frame.kind = std::string(word);
                    if (word == "rect")
                        frame.color = ParseColor(rest);
                    else
                        frame.label = CleanLabel(rest);
                    frame.first = (int)seq.rows.size();
                    parser.openFrames.push_back({(int)seq.frames.size(), line.number});
                    seq.frames.push_back(frame);
                    continue;
                }
                if (word == "else" || word == "and" || word == "option")
                {
                    const char* owner = word == "else" ? "alt" : word == "and" ? "par" : "critical";
                    if (parser.openFrames.empty() || seq.frames[parser.openFrames.back().first].kind != owner)
                    {
                        d.error = LineError(line.number, "`" + std::string(word) + "` outside of " + owner);
                        return;
                    }
                    seq.frames[parser.openFrames.back().first].sections.push_back(
                        {(int)seq.rows.size(), CleanLabel(rest)});
                    continue;
                }
                if (line.text == "end")
                {
                    if (parser.openFrames.empty())
                    {
                        d.error = LineError(line.number, "`end` without a block to close");
                        return;
                    }
                    seq.frames[parser.openFrames.back().first].last = (int)seq.rows.size() - 1;
                    parser.openFrames.pop_back();
                    continue;
                }
                if (IsStylingLine(word) || word == "links")
                    continue;
                std::string error;
                if (ScanNote(line.text, seq) || ScanMessage(line.text, parser, &error))
                    continue;
                d.error =
                    LineError(line.number, error.empty() ? "cannot parse `" + std::string(line.text) + "`" : error);
                return;
            }
            if (!parser.openFrames.empty())
            {
                d.error = LineError(parser.openFrames.back().second,
                                    "`" + seq.frames[parser.openFrames.back().first].kind + "` without its `end`");
                return;
            }
            // still active at the end: up to the last row
            for (const auto& [participant, open] : parser.openActivations)
                for (int index : open)
                    seq.activations[index].endRow = std::max((int)seq.rows.size() - 1, seq.activations[index].startRow);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Class diagrams
        // ---------------------------------------------------------------------------------------------------------

        // Generics: List~int~ shown as List<int>, nested ones too (a ~ before a name opens, any other closes)
        std::string Generics(string_view text)
        {
            std::string out;
            for (size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] != '~')
                    out += text[i];
                else
                    out += (i + 1 < text.size() && IsWordChar(text[i + 1]) && i > 0 && IsWordChar(text[i - 1])) ? '<'
                                                                                                                : '>';
            }
            return out;
        }

        int AddClass(Diagram& d, string_view name, int ns)
        {
            int index = FindNode(d.graph, name);
            if (index < 0)
            {
                index = AddNode(d.graph, name, ns);
                d.graph.nodes[index].compartments = {{ClassLine{std::string(name)}}, {}, {}};
            }
            else if (ns >= 0 && d.graph.nodes[index].subgraph < 0)
                d.graph.nodes[index].subgraph = ns;
            return index;
        }

        // <<annotation>> above the name, a member with parentheses in the methods, any other in the attributes. A $ at
        // the end (or after the parentheses) makes a member static, a * abstract.
        void AddMember(Node& node, string_view member)
        {
            member = Trim(member);
            if (StartsWith(member, "<<") && EndsWith(member, ">>"))
            {
                std::string annotation =
                    "\xC2\xAB" + std::string(Trim(member.substr(2, member.size() - 4))) + "\xC2\xBB";
                node.compartments[0].insert(node.compartments[0].end() - 1, ClassLine{annotation});
                return;
            }
            ClassLine line;
            std::string text(member);
            size_t paren = text.find(')');
            for (size_t at :
                 {text.empty() ? std::string::npos : text.size() - 1, paren == std::string::npos ? paren : paren + 1})
            {
                if (at >= text.size() || (text[at] != '$' && text[at] != '*'))
                    continue;
                (text[at] == '$' ? line.isStatic : line.isAbstract) = true;
                text.erase(at, 1);
                break;
            }
            line.text = Generics(Trim(text));
            node.compartments[member.find('(') != string_view::npos ? 2 : 1].push_back(line);
        }

        // The ends of a relation: <|, *, o, <, () before the line (-- or ..), |>, *, o, >, () after it
        Marker ScanRelationEnd(Scanner& sc, bool before)
        {
            struct EndToken
            {
                string_view token;
                Marker marker;
            };
            static const EndToken beforeTokens[] = {{"<|", Marker::Triangle},
                                                    {"()", Marker::Lollipop},
                                                    {"*", Marker::DiamondFilled},
                                                    {"o", Marker::Diamond},
                                                    {"<", Marker::Arrow}};
            static const EndToken afterTokens[] = {{"|>", Marker::Triangle},
                                                   {"()", Marker::Lollipop},
                                                   {"*", Marker::DiamondFilled},
                                                   {"o", Marker::Diamond},
                                                   {">", Marker::Arrow}};
            for (const EndToken& t : before ? beforeTokens : afterTokens)
            {
                if (sc.Rest().substr(0, t.token.size()) != t.token)
                    continue;
                char next = sc.Peek(t.token.size());
                if (before && next != '-' && next != '.')  // an o or a * before the line must touch it
                    continue;
                sc.i += t.token.size();
                return t.marker;
            }
            return Marker::None;
        }

        // An optional "cardinality"
        bool ScanQuoted(Scanner& sc, std::string* text)
        {
            sc.SkipSpaces();
            if (!sc.Eat("\""))
                return true;
            size_t end = sc.s.find('"', sc.i);
            if (end == string_view::npos)
                return false;
            *text = std::string(sc.s.substr(sc.i, end - sc.i));
            sc.i = end + 1;
            return true;
        }

        // A "1" *-- "many" B : label. The markers of a hierarchy (triangle, diamonds) go to the source, an arrow to
        // the target, so that the parents and the wholes are laid out first.
        bool ScanRelation(string_view line, Diagram& d, int ns)
        {
            Scanner sc{line};
            string_view src = sc.Word();
            std::string cardSrc, cardDst;
            if (src.empty() || !ScanQuoted(sc, &cardSrc))
                return false;
            sc.SkipSpaces();
            Relation relation;
            relation.markerSrc = ScanRelationEnd(sc, true);
            if (sc.Eat(".."))
                relation.dashed = true;
            else if (!sc.Eat("--"))
                return false;
            relation.markerDst = ScanRelationEnd(sc, false);
            if (!ScanQuoted(sc, &cardDst))
                return false;
            sc.SkipSpaces();
            string_view dst = sc.Word();
            sc.SkipSpaces();
            std::string label;
            if (sc.Eat(":"))
                label = CleanLabel(sc.Rest());
            else if (!sc.AtEnd())
                return false;
            if (dst.empty())
                return false;
            auto hierarchy = [](Marker m) {
                return m == Marker::Triangle || m == Marker::Diamond || m == Marker::DiamondFilled;
            };
            if ((relation.markerSrc == Marker::None && hierarchy(relation.markerDst))
                || (relation.markerSrc == Marker::Arrow && relation.markerDst == Marker::None))
            {
                std::swap(src, dst), std::swap(cardSrc, cardDst);
                std::swap(relation.markerSrc, relation.markerDst);
            }
            relation.cardinalitySrc = cardSrc;
            relation.cardinalityDst = cardDst;
            Edge edge;
            edge.src = AddClass(d, src, ns);
            edge.dst = AddClass(d, dst, ns);
            edge.label = label;
            edge.end = EdgeEnd::None;
            edge.style = relation.dashed ? LineStyle::Dotted : LineStyle::Solid;
            d.graph.edges.push_back(edge);
            d.relations.push_back(relation);
            return true;
        }

        // A note: its own node, linked to its class by a dotted line
        bool ScanClassNote(string_view line, Diagram& d, int ns)
        {
            Scanner sc{line};
            if (sc.Word() != "note")
                return false;
            sc.SkipSpaces();
            int target = -1;
            if (sc.Rest().substr(0, 4) == "for ")
            {
                sc.i += 4;
                sc.SkipSpaces();
                string_view name = sc.Word();
                if (name.empty())
                    return false;
                target = AddClass(d, name, ns);
                sc.SkipSpaces();
            }
            string_view text = Trim(sc.Rest());
            if (text.size() < 2 || text.front() != '"' || text.back() != '"')
                return false;
            std::string label;
            for (string_view rest = text.substr(1, text.size() - 2); !rest.empty();)  // \n: a line break
            {
                size_t escape = rest.find("\\n");
                label += std::string(rest.substr(0, escape));
                if (escape == string_view::npos)
                    break;
                label += "<br>";
                rest.remove_prefix(escape + 2);
            }
            Node note;
            note.id = "note " + std::to_string(d.graph.nodes.size());
            note.label = CleanLabel(label);
            note.shape = NodeShape::Note;
            note.subgraph = target >= 0 ? d.graph.nodes[target].subgraph : ns;
            d.graph.nodes.push_back(note);
            if (target >= 0)
            {
                Edge edge;
                edge.src = (int)d.graph.nodes.size() - 1;
                edge.dst = target;
                edge.end = EdgeEnd::None;
                edge.style = LineStyle::Dotted;
                Relation relation;
                relation.dashed = true;
                d.graph.edges.push_back(edge);
                d.relations.push_back(relation);
            }
            return true;
        }

        // class Name, class Name~T~, class Name["Label"], class Name <<Annotation>>, class Name:::style, then maybe {
        bool ScanClassDeclaration(string_view line, Diagram& d, int ns, int* opened)
        {
            Scanner sc{AfterFirstWord(line)};
            string_view name = sc.Word();
            if (name.empty())
                return false;
            int index = AddClass(d, name, ns);
            Node& node = d.graph.nodes[index];
            std::string display(name);
            if (sc.Peek() == '~')  // generic: up to the last ~ of the line
            {
                size_t last = sc.s.rfind('~');
                display = Generics(std::string(name) + std::string(sc.s.substr(sc.i, last + 1 - sc.i)));
                sc.i = last + 1;
            }
            sc.SkipSpaces();
            if (sc.Eat("["))
            {
                size_t close = sc.s.find(']', sc.i);
                if (close == string_view::npos)
                    return false;
                display = CleanLabel(sc.s.substr(sc.i, close - sc.i));
                sc.i = close + 1;
                sc.SkipSpaces();
            }
            if (sc.Eat(":::"))
                sc.Word(), sc.SkipSpaces();
            if (sc.Rest().substr(0, 2) == "<<")
            {
                size_t close = sc.s.find(">>", sc.i);
                if (close == string_view::npos)
                    return false;
                AddMember(node, sc.s.substr(sc.i, close + 2 - sc.i));
                sc.i = close + 2;
                sc.SkipSpaces();
            }
            node.compartments[0].back().text = display;
            if (sc.Eat("{"))
                *opened = index;
            sc.SkipSpaces();
            return sc.AtEnd();
        }

        void ParseClass(const std::vector<SourceLine>& lines, Diagram& d)
        {
            std::vector<std::pair<int, int>> namespaces;  // the open ones: subgraph, line of the opening
            auto ns = [&]() { return namespaces.empty() ? -1 : namespaces.back().first; };
            int current = -1, currentLine = 0;  // inside `class X {`
            for (const SourceLine& line : lines)
            {
                string_view word = FirstWord(line.text);
                if (current >= 0)
                {
                    if (line.text == "}")
                        current = -1;
                    else
                        AddMember(d.graph.nodes[current], line.text);
                    continue;
                }
                if (word == "classDiagram" || word == "classDiagram-v2")
                    continue;
                if (word == "direction")
                {
                    string_view direction = AfterFirstWord(line.text);
                    d.graph.vertical = direction == "TB" || direction == "TD" || direction == "BT";
                    d.graph.reversed = direction == "BT" || direction == "RL";
                    if (!d.graph.vertical && direction != "LR" && direction != "RL")
                    {
                        d.error = LineError(line.number, "unknown direction `" + std::string(direction) + "`");
                        return;
                    }
                    continue;
                }
                if (word == "namespace")  // namespace Name, namespace Name["Label"], namespace A.B.C (A contains B, B
                                          // contains C)
                {
                    string_view rest = AfterFirstWord(line.text);
                    if (EndsWith(rest, "{"))
                        rest = Trim(rest.substr(0, rest.size() - 1));
                    string_view name = rest;
                    std::string title;
                    size_t bracket = rest.find('[');
                    if (bracket != string_view::npos && EndsWith(rest, "]"))
                    {
                        name = Trim(rest.substr(0, bracket));
                        title = CleanLabel(rest.substr(bracket + 1, rest.size() - bracket - 2));
                    }
                    int parent = ns();
                    // each level inside the previous one, named by its own part (its id: the path)
                    while (!name.empty())
                    {
                        size_t dot = name.find('.');
                        string_view part = name.substr(0, dot);
                        bool last = dot == string_view::npos;
                        std::string id =
                            parent < 0 ? std::string(part) : d.graph.subgraphs[parent].id + "." + std::string(part);
                        parent = AddSubgraph(d.graph, id, last && !title.empty() ? title : std::string(part), parent);
                        name = last ? string_view() : name.substr(dot + 1);
                    }
                    namespaces.push_back({parent, line.number});
                    continue;
                }
                if (line.text == "}")
                {
                    if (namespaces.empty())
                    {
                        d.error = LineError(line.number, "`}` without a block to close");
                        return;
                    }
                    namespaces.pop_back();
                    continue;
                }
                if (word == "class")
                {
                    int opened = -1;
                    if (!ScanClassDeclaration(line.text, d, ns(), &opened))
                    {
                        d.error = LineError(line.number, "cannot parse `" + std::string(line.text) + "`");
                        return;
                    }
                    if (opened >= 0)
                        current = opened, currentLine = line.number;
                    continue;
                }
                if (StartsWith(line.text, "<<") && line.text.find(">>") != string_view::npos)  // <<interface>> Shape
                {
                    size_t close = line.text.find(">>");
                    int index = AddClass(d, Trim(line.text.substr(close + 2)), ns());
                    AddMember(d.graph.nodes[index], line.text.substr(0, close + 2));
                    continue;
                }
                if (IsStylingLine(word))
                    continue;
                if (ScanClassNote(line.text, d, ns()) || ScanRelation(line.text, d, ns()))
                    continue;
                // Name : member
                Scanner sc{line.text};
                string_view name = sc.Word();
                sc.SkipSpaces();
                if (!name.empty() && sc.Eat(":") && !Trim(sc.Rest()).empty())
                {
                    AddMember(d.graph.nodes[AddClass(d, name, ns())], sc.Rest());
                    continue;
                }
                d.error = LineError(line.number, "cannot parse `" + std::string(line.text) + "`");
                return;
            }
            if (current >= 0)
                d.error = LineError(currentLine, "the block of `" + d.graph.nodes[current].id + "` is not closed");
            else if (!namespaces.empty())
                d.error =
                    LineError(namespaces.back().second,
                              "the namespace `" + d.graph.subgraphs[namespaces.back().first].id + "` is not closed");
        }
    }

    Diagram Parse(const std::string& source)
    {
        Diagram d;
        std::vector<SourceLine> lines = Lines(source);
        if (!SkipUndrawnLines(lines, &d.error))
            return d;
        if (lines.empty())
        {
            d.error = "line 1: empty diagram";
            return d;
        }
        // the variant headers (another layout engine, a renderer version) are read as the plain ones
        string_view type = FirstWord(lines[0].text);
        if (type == "graph" || type == "flowchart" || type == "flowchart-elk")
        {
            d.kind = DiagramKind::Flowchart;
            ParseFlowchart(lines, d);
        }
        else if (type == "sequenceDiagram")
        {
            d.kind = DiagramKind::Sequence;
            ParseSequence(lines, d);
        }
        else if (type == "classDiagram" || type == "classDiagram-v2")
        {
            d.kind = DiagramKind::Class;
            ParseClass(lines, d);
        }
        else
            d.error = LineError(lines[0].number, "`" + std::string(type) + "` diagrams are not supported");
        return d;
    }
}
