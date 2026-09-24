// Part of ImGui Bundle - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_bundle
// Mermaid: the parsers. A line is scanned by hand (no std::regex); a line that is not understood is an error,
// except the styling and interaction lines, which are skipped.
#include "rich_md_mermaid.h"

#include <cctype>
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
        bool EndsWith(string_view s, string_view suffix) { return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix; }

        // Identifiers: letters, digits, '_', and any non-ASCII character (UTF-8)
        bool IsWordChar(char c) { return std::isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80; }

        // A cursor on a line
        struct Scanner
        {
            string_view s;
            size_t i = 0;

            bool AtEnd() const { return i >= s.size(); }
            void SkipSpaces() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
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
        struct SourceLine { int number; string_view text; };
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

        std::string LineError(int number, const std::string& message) { return "line " + std::to_string(number) + ": " + message; }

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
        int AddSubgraph(Graph& graph, string_view id, const std::string& title)
        {
            int index = FindSubgraph(graph, id);
            if (index < 0)
            {
                graph.subgraphs.emplace_back();
                graph.subgraphs.back().id = std::string(id);
                index = (int)graph.subgraphs.size() - 1;
            }
            graph.subgraphs[index].title = title;
            return index;
        }

        // A node, created at its first mention; a node first seen outside a subgraph joins the first one that mentions it
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

        // A node reference: an id, and optionally its shape and label: A, A[label], A(label), A{label}, A[(label)]
        bool ScanNodeRef(Scanner& sc, Graph& graph, int subgraph, int* nodeIndex)
        {
            sc.SkipSpaces();
            string_view id = sc.Word();
            if (id.empty())
                return false;
            *nodeIndex = AddNode(graph, id, subgraph);
            sc.SkipSpaces();
            struct Bracket { string_view open, close; NodeShape shape; };
            static const Bracket brackets[] = {
                {"[(", ")]", NodeShape::Cylinder}, {"[", "]", NodeShape::Rect},
                {"(", ")", NodeShape::Rounded}, {"{", "}", NodeShape::Diamond},
            };
            for (const Bracket& b : brackets)
            {
                if (!sc.Eat(b.open))
                    continue;
                size_t end = sc.s.find(b.close, sc.i);
                if (end == string_view::npos)
                    return false;
                Node& node = graph.nodes[*nodeIndex];
                node.label = std::string(sc.s.substr(sc.i, end - sc.i));
                node.shape = b.shape;
                sc.i = end + b.close.size();
                break;
            }
            return true;
        }

        // A link: -->, ---, -.->, -. label .->, ==>, then an optional |label|
        bool ScanLink(Scanner& sc, Edge* edge)
        {
            sc.SkipSpaces();
            if (sc.Eat("-->"))
                edge->arrow = true;
            else if (sc.Eat("---"))
                edge->arrow = false;
            else if (sc.Eat("==>"))
                edge->style = LineStyle::Thick;
            else if (sc.Eat("-."))
            {
                edge->style = LineStyle::Dotted;
                if (!sc.Eat("->"))
                {
                    size_t end = sc.s.find(".->", sc.i);  // -. label .->
                    if (end == string_view::npos)
                        return false;
                    edge->label = std::string(Trim(sc.s.substr(sc.i, end - sc.i)));
                    sc.i = end + 3;
                }
            }
            else
                return false;
            sc.SkipSpaces();
            if (sc.Eat("|"))
            {
                size_t end = sc.s.find('|', sc.i);
                if (end == string_view::npos)
                    return false;
                edge->label = std::string(Trim(sc.s.substr(sc.i, end - sc.i)));
                sc.i = end + 1;
            }
            return true;
        }

        void ParseFlowchart(const std::vector<SourceLine>& lines, Diagram& d)
        {
            Graph& graph = d.graph;
            int subgraph = -1;
            for (const SourceLine& line : lines)
            {
                string_view word = FirstWord(line.text);
                if (word == "graph" || word == "flowchart")
                {
                    string_view direction = AfterFirstWord(line.text);
                    if (direction.empty() || direction == "TD" || direction == "TB" || direction == "BT")
                        graph.vertical = true;
                    else if (direction == "LR" || direction == "RL")
                        graph.vertical = false;
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
                        title = std::string(rest.substr(sc.i, rest.size() - 1 - sc.i));
                    else
                        id = rest, title = std::string(rest);
                    subgraph = AddSubgraph(graph, id, title);
                    continue;
                }
                if (line.text == "end")
                {
                    subgraph = -1;
                    continue;
                }
                if (IsStylingLine(word))
                    continue;
                // A node, or two nodes and a link
                Scanner sc{line.text};
                int src = -1, dst = -1;
                if (!ScanNodeRef(sc, graph, subgraph, &src))
                {
                    d.error = LineError(line.number, "cannot parse `" + std::string(line.text) + "`");
                    return;
                }
                sc.SkipSpaces();
                if (!sc.AtEnd())
                {
                    Edge edge;
                    if (!ScanLink(sc, &edge) || !ScanNodeRef(sc, graph, subgraph, &dst))
                    {
                        d.error = LineError(line.number, "cannot parse `" + std::string(line.text) + "`");
                        return;
                    }
                    sc.SkipSpaces();
                    if (!sc.AtEnd())
                    {
                        d.error = LineError(line.number, "unexpected `" + std::string(sc.Rest()) + "`");
                        return;
                    }
                    edge.src = src;
                    edge.dst = dst;
                    graph.edges.push_back(edge);
                }
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // Sequence diagrams
        // ---------------------------------------------------------------------------------------------------------

        int AddParticipant(Sequence& seq, string_view id, string_view label = {})
        {
            for (size_t i = 0; i < seq.participantIds.size(); ++i)
                if (seq.participantIds[i] == id)
                {
                    if (!label.empty())
                        seq.participantLabels[i] = std::string(label);
                    return (int)i;
                }
            seq.participantIds.emplace_back(id);
            seq.participantLabels.emplace_back(label.empty() ? id : label);
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
            bool found = false;
            for (string_view where : {"over", "right of", "left of"})
                if (rest.size() > where.size() && EqualsNoCase(rest.substr(0, where.size()), where) && std::isspace((unsigned char)rest[where.size()]))
                {
                    rest = Trim(rest.substr(where.size()));
                    found = true;
                    break;
                }
            size_t colon = rest.find(':');
            if (!found || colon == string_view::npos)
                return false;
            SequenceRow row;
            row.isNote = true;
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
            row.text = std::string(Trim(rest.substr(colon + 1)));
            seq.rows.push_back(row);
            return true;
        }

        // A->>B: text, with the arrows ->, -->, ->>, -->>
        bool ScanMessage(string_view line, Sequence& seq)
        {
            Scanner sc{line};
            string_view src = sc.Word();
            sc.SkipSpaces();
            SequenceRow row;
            if (sc.Eat("-->>"))
                row.dashed = true, row.arrowhead = true;
            else if (sc.Eat("->>"))
                row.dashed = false, row.arrowhead = true;
            else if (sc.Eat("-->"))
                row.dashed = true, row.arrowhead = false;
            else if (sc.Eat("->"))
                row.dashed = false, row.arrowhead = false;
            else
                return false;
            sc.SkipSpaces();
            string_view dst = sc.Word();
            sc.SkipSpaces();
            if (src.empty() || dst.empty() || !sc.Eat(":"))
                return false;
            row.src = AddParticipant(seq, src);
            row.dst = AddParticipant(seq, dst);
            row.text = std::string(Trim(sc.Rest()));
            seq.rows.push_back(row);
            return true;
        }

        void ParseSequence(const std::vector<SourceLine>& lines, Diagram& d)
        {
            Sequence& seq = d.sequence;
            std::vector<SequenceLoop> openLoops;
            for (const SourceLine& line : lines)
            {
                string_view word = FirstWord(line.text);
                if (word == "sequenceDiagram" || word == "activate" || word == "deactivate")
                    continue;
                if (word == "participant" || word == "actor")
                {
                    string_view rest = AfterFirstWord(line.text);
                    size_t as = rest.find(" as ");
                    if (as == string_view::npos)
                        AddParticipant(seq, Trim(rest));
                    else
                        AddParticipant(seq, Trim(rest.substr(0, as)), Trim(rest.substr(as + 4)));
                    continue;
                }
                if (word == "loop")
                {
                    openLoops.push_back(SequenceLoop{std::string(AfterFirstWord(line.text)), (int)seq.rows.size()});
                    continue;
                }
                if (line.text == "end")
                {
                    if (openLoops.empty())
                    {
                        d.error = LineError(line.number, "`end` without a block to close");
                        return;
                    }
                    SequenceLoop loop = openLoops.back();
                    openLoops.pop_back();
                    loop.last = (int)seq.rows.size() - 1;
                    seq.loops.push_back(loop);
                    continue;
                }
                if (IsStylingLine(word))
                    continue;
                if (ScanNote(line.text, seq) || ScanMessage(line.text, seq))
                    continue;
                d.error = LineError(line.number, "cannot parse `" + std::string(line.text) + "`");
                return;
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // Class diagrams
        // ---------------------------------------------------------------------------------------------------------

        int AddClass(Diagram& d, string_view name, int ns)
        {
            int index = FindNode(d.graph, name);
            if (index < 0)
            {
                index = AddNode(d.graph, name, ns);
                d.graph.nodes[index].compartments = {{std::string(name)}, {}, {}};
            }
            else if (ns >= 0 && d.graph.nodes[index].subgraph < 0)
                d.graph.nodes[index].subgraph = ns;
            return index;
        }

        // <<annotation>> above the name, a member with parentheses in the methods, any other in the attributes
        void AddMember(Node& node, string_view member)
        {
            member = Trim(member);
            if (StartsWith(member, "<<") && EndsWith(member, ">>"))
                node.compartments[0].insert(node.compartments[0].begin(), "\xC2\xAB" + std::string(member.substr(2, member.size() - 4)) + "\xC2\xBB");
            else if (member.find('(') != string_view::npos)
                node.compartments[2].emplace_back(member);
            else
                node.compartments[1].emplace_back(member);
        }

        // The relation tokens, with their markers; a mirrored token swaps the two ends
        struct RelationToken { string_view token; Marker src, dst; bool dashed, mirrored; };
        const RelationToken kRelationTokens[] = {
            {"<|--", Marker::Triangle, Marker::None, false, false},
            {"*--", Marker::DiamondFilled, Marker::None, false, false},
            {"o--", Marker::Diamond, Marker::None, false, false},
            {"-->", Marker::None, Marker::Arrow, false, false},
            {"--|>", Marker::Triangle, Marker::None, false, true},
            {"--*", Marker::DiamondFilled, Marker::None, false, true},
            {"--o", Marker::Diamond, Marker::None, false, true},
            {"<--", Marker::None, Marker::Arrow, false, true},
            {"..>", Marker::None, Marker::Arrow, true, false},
            {"<..", Marker::None, Marker::Arrow, true, true},
            {"..|>", Marker::Triangle, Marker::None, true, true},
            {"<|..", Marker::Triangle, Marker::None, true, false},
            {"--", Marker::None, Marker::None, false, false},
            {"..", Marker::None, Marker::None, true, false},
        };

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

        // A "1" *-- "many" B : label
        bool ScanRelation(string_view line, Diagram& d, int ns)
        {
            Scanner sc{line};
            string_view src = sc.Word();
            std::string cardSrc, cardDst;
            if (src.empty() || !ScanQuoted(sc, &cardSrc))
                return false;
            sc.SkipSpaces();
            const RelationToken* found = nullptr;
            for (const RelationToken& t : kRelationTokens)  // the longest token that matches
                if (sc.Rest().substr(0, t.token.size()) == t.token && (!found || t.token.size() > found->token.size()))
                    found = &t;
            if (!found)
                return false;
            sc.i += found->token.size();
            if (!ScanQuoted(sc, &cardDst))
                return false;
            sc.SkipSpaces();
            string_view dst = sc.Word();
            sc.SkipSpaces();
            std::string label;
            if (sc.Eat(":"))
                label = std::string(Trim(sc.Rest()));
            else if (!sc.AtEnd())
                return false;
            if (dst.empty())
                return false;
            if (found->mirrored)
                std::swap(src, dst), std::swap(cardSrc, cardDst);
            Relation relation;
            relation.markerSrc = found->src;
            relation.markerDst = found->dst;
            relation.dashed = found->dashed;
            relation.label = label;
            relation.cardinalitySrc = cardSrc;
            relation.cardinalityDst = cardDst;
            Edge edge;
            edge.src = AddClass(d, src, ns);
            edge.dst = AddClass(d, dst, ns);
            edge.arrow = false;
            edge.style = found->dashed ? LineStyle::Dotted : LineStyle::Solid;
            d.graph.edges.push_back(edge);
            d.relations.push_back(relation);
            return true;
        }

        void ParseClass(const std::vector<SourceLine>& lines, Diagram& d)
        {
            int ns = -1;
            int current = -1;  // inside `class X {`
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
                if (word == "classDiagram")
                    continue;
                if (word == "namespace")
                {
                    string_view name = AfterFirstWord(line.text);
                    if (EndsWith(name, "{"))
                        name = Trim(name.substr(0, name.size() - 1));
                    ns = AddSubgraph(d.graph, name, std::string(name));
                    continue;
                }
                if (line.text == "}")
                {
                    ns = -1;
                    continue;
                }
                if (word == "class")
                {
                    string_view name = AfterFirstWord(line.text);
                    bool opens = EndsWith(name, "{");
                    if (opens)
                        name = Trim(name.substr(0, name.size() - 1));
                    int index = AddClass(d, name, ns);
                    if (opens)
                        current = index;
                    continue;
                }
                if (StartsWith(line.text, "<<") && line.text.find(">>") != string_view::npos)  // <<interface>> Shape
                {
                    size_t close = line.text.find(">>");
                    int index = AddClass(d, Trim(line.text.substr(close + 2)), ns);
                    AddMember(d.graph.nodes[index], line.text.substr(0, close + 2));
                    continue;
                }
                if (IsStylingLine(word))
                    continue;
                if (ScanRelation(line.text, d, ns))
                    continue;
                // Name : member
                Scanner sc{line.text};
                string_view name = sc.Word();
                sc.SkipSpaces();
                if (!name.empty() && sc.Eat(":") && !Trim(sc.Rest()).empty())
                {
                    AddMember(d.graph.nodes[AddClass(d, name, ns)], sc.Rest());
                    continue;
                }
                d.error = LineError(line.number, "cannot parse `" + std::string(line.text) + "`");
                return;
            }
        }
    }

    Diagram Parse(const std::string& source)
    {
        Diagram d;
        std::vector<SourceLine> lines = Lines(source);
        if (lines.empty())
        {
            d.error = "line 1: empty diagram";
            return d;
        }
        string_view type = FirstWord(lines[0].text);
        if (type == "graph" || type == "flowchart")
        {
            d.kind = DiagramKind::Flowchart;
            ParseFlowchart(lines, d);
        }
        else if (type == "sequenceDiagram")
        {
            d.kind = DiagramKind::Sequence;
            ParseSequence(lines, d);
        }
        else if (type == "classDiagram")
        {
            d.kind = DiagramKind::Class;
            ParseClass(lines, d);
        }
        else
            d.error = LineError(lines[0].number, "`" + std::string(type) + "` diagrams are not supported");
        return d;
    }
}
