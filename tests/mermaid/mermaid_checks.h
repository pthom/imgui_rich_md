// The checks of the Mermaid test corpus, shared by the test (mermaid_test.cpp) and the review tool.
// A corpus file starts with comment lines:
//   %% <what the diagram shows>
//   %% expect: nodes=6 edges=6 subgraphs=3       (or participants=, messages=, notes=, frames=; error_line=N)
//   %% known: label-on-node (<why>)              the checks known to fail until the layout improves
// The layout checks need an ImGui frame, with the font the diagram is laid out with.
#pragma once
#include "imgui_rich_md/backends/mermaid/mermaid_model.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace MermaidChecks
{
    using namespace RichMd::Mermaid;

    struct Expectation
    {
        std::string description;
        std::map<std::string, int> counts;
        std::set<std::string> known;
    };

    struct Issue
    {
        std::string check, message;
    };

    struct Report
    {
        std::vector<Issue> failures;          // unexpected
        std::vector<Issue> known;             // expected (listed in `%% known:`)
        std::vector<std::string> fixedKnown;  // listed in `%% known:`, but they pass: the marker should go
        int crossings = 0;                    // how many times two edges cross (a measure)
        int bends = 0;                        // how many edges between neighbouring layers are not straight (a measure)
    };

    inline Expectation ReadExpectation(const std::string& source)
    {
        Expectation e;
        std::istringstream lines(source);
        std::string line;
        bool first = true;
        std::streampos start = lines.tellg();
        if (std::getline(lines, line) && line == "---")  // a front matter first (Mermaid wants it at the very start)
            while (std::getline(lines, line) && line != "---")
            {}
        else
            lines.seekg(start);
        while (std::getline(lines, line) && line.rfind("%%", 0) == 0)
        {
            std::string text = line.substr(line.find_first_not_of("% "));
            std::istringstream words(text);
            std::string word;
            words >> word;
            if (word == "expect:")
            {
                while (words >> word)
                    if (size_t eq = word.find('='); eq != std::string::npos)
                        e.counts[word.substr(0, eq)] = std::stoi(word.substr(eq + 1));
            }
            else if (word == "known:")
            {
                while (words >> word && word[0] != '(')
                    e.known.insert(word);
            }
            else if (first)
                e.description = text;
            first = false;
        }
        return e;
    }

    namespace detail
    {
        struct Rect
        {
            ImVec2 min, max;
            std::string name;
        };

        // Overlap of the interiors (touching is fine)
        inline bool Overlap(const Rect& a, const Rect& b, float tolerance = 0.5f)
        {
            return a.min.x + tolerance < b.max.x && b.min.x + tolerance < a.max.x
                && a.min.y + tolerance < b.max.y && b.min.y + tolerance < a.max.y;
        }

        inline bool Inside(const Rect& inner, const Rect& outer, float tolerance = 0.5f)
        {
            return inner.min.x + tolerance >= outer.min.x && inner.min.y + tolerance >= outer.min.y
                && inner.max.x <= outer.max.x + tolerance && inner.max.y <= outer.max.y + tolerance;
        }

        // Whether the segment [p, q] goes through the interior of the rect (Liang-Barsky clipping)
        inline bool SegmentThroughRect(ImVec2 p, ImVec2 q, const Rect& r, float tolerance = 1.f)
        {
            float x0 = r.min.x + tolerance, y0 = r.min.y + tolerance, x1 = r.max.x - tolerance, y1 = r.max.y - tolerance;
            float t0 = 0.f, t1 = 1.f, dx = q.x - p.x, dy = q.y - p.y;
            const float pp[4] = {-dx, dx, -dy, dy};
            const float qq[4] = {p.x - x0, x1 - p.x, p.y - y0, y1 - p.y};
            for (int i = 0; i < 4; ++i)
            {
                if (pp[i] == 0.f)
                {
                    if (qq[i] < 0.f)
                        return false;
                    continue;
                }
                float t = qq[i] / pp[i];
                if (pp[i] < 0.f)
                    t0 = std::max(t0, t);
                else
                    t1 = std::min(t1, t);
                if (t0 > t1)
                    return false;
            }
            return true;
        }

        inline Rect TextRect(ImVec2 pos, const std::string& text, const std::string& name)
        {
            ImVec2 ts = ImGui::CalcTextSize(text.c_str());
            return Rect{pos, ImVec2(pos.x + ts.x, pos.y + ts.y), name};
        }

        inline std::string Describe(const Rect& r) { return "`" + r.name + "`"; }
    }

    inline std::vector<Issue> CheckParse(const Diagram& d, const Expectation& e)
    {
        std::vector<Issue> issues;
        auto it = e.counts.find("error_line");
        if (it != e.counts.end())
        {
            std::string expected = "line " + std::to_string(it->second) + ":";
            if (d.error.rfind(expected, 0) != 0)
                issues.push_back({"parse", "expected an error at " + expected + " got `" + d.error + "`"});
            return issues;
        }
        if (!d.error.empty())
        {
            issues.push_back({"parse", d.error});
            return issues;
        }
        std::map<std::string, int> counts;
        if (d.kind == DiagramKind::Sequence)
        {
            counts["participants"] = (int)d.sequence.participantIds.size();
            counts["messages"] = counts["notes"] = 0;
            for (const SequenceRow& r : d.sequence.rows)
                ++counts[r.isNote ? "notes" : "messages"];
            counts["frames"] = (int)d.sequence.frames.size();
        }
        else
        {
            counts["nodes"] = (int)d.graph.nodes.size();
            counts["edges"] = (int)d.graph.edges.size();
            counts["subgraphs"] = (int)d.graph.subgraphs.size();
        }
        for (const auto& [key, value] : e.counts)
        {
            auto found = counts.find(key);
            if (found == counts.end())
                issues.push_back({"parse", "unknown count `" + key + "`"});
            else if (found->second != value)
                issues.push_back({"parse", key + ": expected " + std::to_string(value) + ", got " + std::to_string(found->second)});
        }
        return issues;
    }

    // The layout invariants: what the eye would otherwise have to catch
    inline std::vector<Issue> CheckLayout(const Diagram& d)
    {
        using namespace detail;
        std::vector<Issue> issues;
        const float em = d.layoutFontSize;
        // Draw reserves DiagramSize, and draws at (em, em / 2) inside it
        ImVec2 size = DiagramSize(d);
        const Rect bounds{ImVec2(-em, -em / 2.f), ImVec2(size.x - em, size.y - em / 2.f), "bounds"};
        auto checkBounds = [&](const Rect& r) {
            if (!Inside(r, bounds))
                issues.push_back({"out-of-bounds", Describe(r) + " is outside the space reserved for the diagram"});
        };

        if (d.kind == DiagramKind::Sequence)
        {
            const Sequence& seq = d.sequence;
            for (size_t i = 0; i < seq.participantIds.size(); ++i)
                checkBounds(Rect{ImVec2(seq.xCenter[i] - seq.boxWidth[i] / 2.f, 0.f),
                                 ImVec2(seq.xCenter[i] + seq.boxWidth[i] / 2.f, seq.height), seq.participantLabels[i]});
            for (const SequenceRow& r : seq.rows)
            {
                checkBounds(Rect{r.textMin, r.textMax, r.text});
                if (r.isNote)
                    checkBounds(Rect{r.boxMin, r.boxMax, "note " + r.text});
            }
            for (const SequenceFrame& f : seq.frames)
                checkBounds(Rect{f.frameMin, f.frameMax, f.kind + " " + f.label});
            for (const SequenceActivation& a : seq.activations)
                checkBounds(Rect{a.boxMin, a.boxMax, "activation of " + seq.participantIds[a.participant]});
            return issues;
        }

        const Graph& g = d.graph;
        for (size_t i = 0; i < g.edges.size(); ++i)  // the other checks need the points
            if (g.edges[i].points.size() < 2)
                issues.push_back({"edge-without-points", "the edge " + std::to_string(i) + " was not laid out"});
        if (!issues.empty())
            return issues;
        std::vector<Rect> nodes;
        for (const Node& n : g.nodes)
            nodes.push_back(Rect{n.pos, ImVec2(n.pos.x + n.size.x, n.pos.y + n.size.y), n.id});
        std::vector<Rect> labels;  // edge labels and cardinalities, with the edge they belong to
        std::vector<int> labelEdge;
        for (size_t i = 0; i < g.edges.size(); ++i)
        {
            const Edge& e = g.edges[i];
            if (!e.label.empty())
                labels.push_back(Rect{e.labelMin, e.labelMax, e.label}), labelEdge.push_back((int)i);
            if (d.kind == DiagramKind::Class)
            {
                const Relation& r = d.relations[i];
                if (!r.cardinalitySrc.empty())
                    labels.push_back(TextRect(r.cardinalitySrcPos, r.cardinalitySrc, r.cardinalitySrc)), labelEdge.push_back((int)i);
                if (!r.cardinalityDst.empty())
                    labels.push_back(TextRect(r.cardinalityDstPos, r.cardinalityDst, r.cardinalityDst)), labelEdge.push_back((int)i);
            }
        }

        for (size_t i = 0; i < nodes.size(); ++i)
        {
            checkBounds(nodes[i]);
            for (size_t j = i + 1; j < nodes.size(); ++j)
                if (Overlap(nodes[i], nodes[j]))
                    issues.push_back({"node-overlap", Describe(nodes[i]) + " and " + Describe(nodes[j]) + " overlap"});
            int sub = g.nodes[i].subgraph;
            if (sub >= 0 && g.subgraphs[sub].hasBox)
                if (!Inside(nodes[i], Rect{g.subgraphs[sub].boxMin, g.subgraphs[sub].boxMax, g.subgraphs[sub].id}))
                    issues.push_back({"subgraph-member", Describe(nodes[i]) + " is outside its subgraph `" + g.subgraphs[sub].id + "`"});
        }
        for (size_t a = 0; a < g.subgraphs.size(); ++a)
        {
            if (!g.subgraphs[a].hasBox)
                continue;
            Rect ra{g.subgraphs[a].boxMin, g.subgraphs[a].boxMax, "subgraph " + g.subgraphs[a].id};
            checkBounds(ra);
            int parent = g.subgraphs[a].parent;
            if (parent >= 0 && g.subgraphs[parent].hasBox && !Inside(ra, Rect{g.subgraphs[parent].boxMin, g.subgraphs[parent].boxMax, ""}))
                issues.push_back({"subgraph-nesting", "subgraph `" + g.subgraphs[a].id + "` is not inside `" + g.subgraphs[parent].id + "`"});
            auto isAncestor = [&](size_t outer, size_t inner) {
                for (int s = g.subgraphs[inner].parent; s >= 0; s = g.subgraphs[s].parent)
                    if (s == (int)outer)
                        return true;
                return false;
            };
            for (size_t b = a + 1; b < g.subgraphs.size(); ++b)  // boxes overlap only when one is inside the other
                if (g.subgraphs[b].hasBox && !isAncestor(a, b) && !isAncestor(b, a)
                    && Overlap(ra, Rect{g.subgraphs[b].boxMin, g.subgraphs[b].boxMax, ""}))
                    issues.push_back({"subgraph-overlap", "subgraphs `" + g.subgraphs[a].id + "` and `" + g.subgraphs[b].id + "` overlap"});
        }
        std::vector<Rect> titles;  // the titles of the subgraphs (the tab of a namespace)
        for (const Subgraph& sub : g.subgraphs)
            if (sub.hasBox)
                titles.push_back(TextRect(ImVec2(sub.boxMin.x + sub.titleX, sub.boxMin.y + GetMetrics(em).titleInset), sub.title, sub.title));
        for (size_t i = 0; i < g.edges.size(); ++i)
        {
            const Edge& e = g.edges[i];
            std::string edgeName = g.nodes[e.src].id + " -> " + g.nodes[e.dst].id;
            for (size_t k = 0; k + 1 < e.points.size(); ++k)
                for (const Rect& title : titles)
                    if (SegmentThroughRect(e.points[k], e.points[k + 1], title))
                        issues.push_back({"edge-through-title", "edge " + edgeName + " goes through the title " + Describe(title)});
            for (ImVec2 p : e.points)
                checkBounds(Rect{p, p, "edge " + edgeName});
            for (size_t k = 0; k + 1 < e.points.size(); ++k)
            {
                for (size_t v = 0; v < nodes.size(); ++v)
                    if (!((int)v == e.src && e.srcBox < 0) && !((int)v == e.dst && e.dstBox < 0)  // its own ends (not a box's member)
                        && SegmentThroughRect(e.points[k], e.points[k + 1], nodes[v]))
                    {
                        issues.push_back({"edge-through-node", "edge " + edgeName + " goes through " + Describe(nodes[v])});
                        break;
                    }
                for (size_t l = 0; l < labels.size(); ++l)
                    if (labelEdge[l] != (int)i && SegmentThroughRect(e.points[k], e.points[k + 1], labels[l]))
                        issues.push_back({"label-on-edge", "label " + Describe(labels[l]) + " hides a part of edge " + edgeName});
            }
        }
        // Two edges that share a stretch of line, or run closer than a quarter of em along it (not the fans: edges from
        // the same node, or into the same node; an end on a box is not an end on the member that stands for it)
        for (size_t i = 0; i < g.edges.size(); ++i)
            for (size_t j = i + 1; j < g.edges.size(); ++j)
            {
                const Edge& e = g.edges[i];
                const Edge& f = g.edges[j];
                if ((e.src == f.src && e.srcBox == f.srcBox) || (e.dst == f.dst && e.dstBox == f.dstBox)
                    || e.style == LineStyle::Invisible || f.style == LineStyle::Invisible)
                    continue;
                bool overlap = false;
                for (size_t a = 0; a + 1 < e.points.size() && !overlap; ++a)
                    for (size_t b = 0; b + 1 < f.points.size() && !overlap; ++b)
                    {
                        ImVec2 p0 = e.points[a], p1 = e.points[a + 1], q0 = f.points[b], q1 = f.points[b + 1];
                        bool horizontal = std::fabs(p0.y - p1.y) < 0.5f && std::fabs(q0.y - q1.y) < 0.5f && std::fabs(p0.y - q0.y) < 0.25f * em;
                        bool vertical = std::fabs(p0.x - p1.x) < 0.5f && std::fabs(q0.x - q1.x) < 0.5f && std::fabs(p0.x - q0.x) < 0.25f * em;
                        auto shared = [](float a0, float a1, float b0, float b1) {
                            return std::min(std::max(a0, a1), std::max(b0, b1)) - std::max(std::min(a0, a1), std::min(b0, b1));
                        };
                        overlap = (horizontal && shared(p0.x, p1.x, q0.x, q1.x) > 1.f) || (vertical && shared(p0.y, p1.y, q0.y, q1.y) > 1.f);
                    }
                if (overlap)
                    issues.push_back({"edge-overlap", "edges " + g.nodes[e.src].id + " -> " + g.nodes[e.dst].id + " and "
                                      + g.nodes[f.src].id + " -> " + g.nodes[f.dst].id + " share a stretch of line"});
            }
        for (size_t l = 0; l < labels.size(); ++l)
        {
            checkBounds(labels[l]);
            for (const Rect& n : nodes)
                if (Overlap(labels[l], n))
                    issues.push_back({"label-on-node", "label " + Describe(labels[l]) + " overlaps " + Describe(n)});
            for (size_t m = l + 1; m < labels.size(); ++m)
                if (Overlap(labels[l], labels[m]))
                    issues.push_back({"label-on-label", "labels " + Describe(labels[l]) + " and " + Describe(labels[m]) + " overlap"});
        }
        return issues;
    }

    // A quality measure, not a check: how many times two edges cross
    inline int CountCrossings(const Diagram& d)
    {
        if (d.kind == DiagramKind::Sequence || !d.error.empty())
            return 0;
        const Graph& g = d.graph;
        auto crosses = [](ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 e) {  // proper intersection of [a, b] and [c, e]
            auto side = [](ImVec2 p, ImVec2 q, ImVec2 r) { return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x); };
            float d1 = side(c, e, a), d2 = side(c, e, b), d3 = side(a, b, c), d4 = side(a, b, e);
            return ((d1 > 0.5f && d2 < -0.5f) || (d1 < -0.5f && d2 > 0.5f)) && ((d3 > 0.5f && d4 < -0.5f) || (d3 < -0.5f && d4 > 0.5f));
        };
        auto cross = [&](ImVec2 p) { return g.vertical ? p.x : p.y; };
        int count = 0;
        for (size_t i = 0; i < g.edges.size(); ++i)
            for (size_t j = i + 1; j < g.edges.size(); ++j)
            {
                const Edge& e = g.edges[i];
                const Edge& f = g.edges[j];
                const auto& p = e.points;
                const auto& q = f.points;
                if (p.size() < 2 || q.size() < 2)  // not laid out (reported by edge-without-points)
                    continue;
                // between neighbouring layers (not an edge to a subgraph laid out on its own: its ends' ranks are apart)
                auto neighbouring = [&](const Edge& k) { return k.lane == 0 && g.nodes[k.dst].rank == g.nodes[k.src].rank + 1; };
                bool sameGap = neighbouring(e) && neighbouring(f) && g.nodes[e.src].rank == g.nodes[f.src].rank;
                if (sameGap)  // two edges between the same layers (their segments may share the channel): ends in opposite orders
                {
                    float starts = cross(p.front()) - cross(q.front()), ends = cross(p.back()) - cross(q.back());
                    count += (starts > 0.5f && ends < -0.5f) || (starts < -0.5f && ends > 0.5f) ? 1 : 0;
                    continue;
                }
                for (size_t a = 0; a + 1 < p.size(); ++a)
                    for (size_t b = 0; b + 1 < q.size(); ++b)
                        count += crosses(p[a], p[a + 1], q[b], q[b + 1]) ? 1 : 0;
            }
        return count;
    }

    // A quality measure, not a check: the edges between neighbouring layers that bend (an elbow where a straight line
    // could do, if the nodes were aligned)
    inline int CountBends(const Diagram& d)
    {
        if (d.kind == DiagramKind::Sequence || !d.error.empty())
            return 0;
        int count = 0;
        for (const Edge& e : d.graph.edges)
            count += (e.lane == 0 && e.points.size() > 2) ? 1 : 0;
        return count;
    }

    // Parses the source, lays it out with the current font, and checks it against its expectation
    inline Report Evaluate(const std::string& source)
    {
        Expectation expectation = ReadExpectation(source);
        Diagram d = Parse(source);
        std::vector<Issue> issues = CheckParse(d, expectation);
        Report report;
        if (d.error.empty())
        {
            Layout(d);
            std::vector<Issue> layoutIssues = CheckLayout(d);
            issues.insert(issues.end(), layoutIssues.begin(), layoutIssues.end());
            report.crossings = CountCrossings(d);
            report.bends = CountBends(d);
        }
        std::set<std::string> failing;
        for (const Issue& issue : issues)
        {
            failing.insert(issue.check);
            (expectation.known.count(issue.check) ? report.known : report.failures).push_back(issue);
        }
        for (const std::string& check : expectation.known)
            if (!failing.count(check))
                report.fixedKnown.push_back(check);
        return report;
    }
}
