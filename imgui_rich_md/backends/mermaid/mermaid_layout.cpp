// Part of ImGui Bundle - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_bundle
// Mermaid: the layout. Graphs are laid out in layers (back edges reversed, longest-path ranks, barycenter
// ordering, one band per subgraph); sizes come from the text, measured with the current font.
#include "rich_md_mermaid.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>

namespace RichMd::Mermaid
{
    bool UsesLane(const Graph& graph, const Edge& e)
    {
        return graph.backEdges.count({e.src, e.dst}) > 0 || graph.nodes[e.dst].rank - graph.nodes[e.src].rank > 1;
    }

    namespace
    {
        void LayoutGraph(Graph& graph, float em)
        {
            const int n = (int)graph.nodes.size();
            std::vector<std::vector<int>> succs(n);
            for (const Edge& e : graph.edges)
                succs[e.src].push_back(e.dst);

            // Back edges: found by a depth-first visit from the nodes in source order (as dagre does), and reversed
            // for the ranking, so that a cycle keeps its natural top-down order
            std::vector<int> state(n, -1);  // -1 not visited, 0 visiting, 1 done
            graph.backEdges.clear();
            std::function<void(int)> visit = [&](int v) {
                state[v] = 0;
                for (int m : succs[v])
                {
                    if (state[m] < 0)
                        visit(m);
                    else if (state[m] == 0)
                        graph.backEdges.insert({v, m});
                }
                state[v] = 1;
            };
            for (int v = 0; v < n; ++v)
                if (state[v] < 0)
                    visit(v);
            std::vector<std::vector<int>> forwardPreds(n);
            for (const Edge& e : graph.edges)
            {
                if (e.src == e.dst)
                    continue;  // a node linked to itself does not change its rank
                if (graph.backEdges.count({e.src, e.dst}))
                    forwardPreds[e.src].push_back(e.dst);
                else
                    forwardPreds[e.dst].push_back(e.src);
            }

            // Ranks: the longest path from a source
            std::vector<int> rank(n, -1);
            std::function<int(int)> rankOf = [&](int v) {
                if (rank[v] < 0)
                {
                    int r = 0;
                    for (int p : forwardPreds[v])
                        r = std::max(r, rankOf(p) + 1);
                    rank[v] = r;
                }
                return rank[v];
            };
            std::map<int, std::vector<int>> layers;
            for (int v = 0; v < n; ++v)
            {
                graph.nodes[v].rank = rankOf(v);
                layers[graph.nodes[v].rank].push_back(v);
            }
            for (auto& [r, layer] : layers)
                for (size_t i = 0; i < layer.size(); ++i)
                    graph.nodes[layer[i]].order = (int)i;

            // Ordering: a few barycenter sweeps
            for (int sweep = 0; sweep < 4; ++sweep)
                for (auto& [r, layer] : layers)
                {
                    std::map<int, float> key;
                    for (int v : layer)
                    {
                        float sum = 0.f;
                        int count = 0;
                        for (int p : forwardPreds[v])
                            if (graph.nodes[p].rank < r)
                                sum += (float)graph.nodes[p].order, ++count;
                        key[v] = count ? sum / (float)count : (float)graph.nodes[v].order;
                    }
                    std::stable_sort(layer.begin(), layer.end(), [&](int a, int b) { return key[a] < key[b]; });
                    for (size_t i = 0; i < layer.size(); ++i)
                        graph.nodes[layer[i]].order = (int)i;
                }

            // Sizes
            const float pad = 0.8f * em, gapX = 1.5f * em, gapY = 2.5f * em;
            const float lineHeight = ImGui::GetTextLineHeight();
            for (Node& node : graph.nodes)
            {
                if (!node.compartments.empty())  // a class: its widest line, a line of text per row, a padding per compartment
                {
                    float w = 0.f, h = 0.f;
                    for (const auto& compartment : node.compartments)
                    {
                        for (const std::string& text : compartment)
                            w = std::max(w, ImGui::CalcTextSize(text.c_str()).x);
                        h += (float)compartment.size() * lineHeight + 0.6f * em;
                    }
                    node.size = ImVec2(w + 2.f * pad, h);
                    continue;
                }
                ImVec2 text = ImGui::CalcTextSize(node.label.c_str());
                float extra = (node.shape == NodeShape::Diamond || node.shape == NodeShape::Cylinder) ? text.y : 0.f;
                node.size = ImVec2(text.x + 2.f * pad + (node.shape == NodeShape::Diamond ? extra : 0.f), text.y + 2.f * pad + extra);
            }
            const bool vertical = graph.vertical;
            auto cross = [&](const Node& nd) { return vertical ? nd.size.x : nd.size.y; };
            auto main = [&](const Node& nd) { return vertical ? nd.size.y : nd.size.x; };

            // Bands on the cross axis: one per subgraph (in source order), then one for the free nodes. A band is as
            // wide as its widest layer, so that the subgraph boxes never overlap
            const int freeBand = (int)graph.subgraphs.size();
            auto band = [&](int v) { return graph.nodes[v].subgraph >= 0 ? graph.nodes[v].subgraph : freeBand; };
            const float boxPad = 0.8f * em;
            const float titleHeight = lineHeight + 0.4f * em;
            std::vector<float> bandWidth(freeBand + 1, 0.f);
            auto membersWidth = [&](const std::vector<int>& members) {
                float w = gapX * (float)(members.size() - 1);
                for (int v : members)
                    w += cross(graph.nodes[v]);
                return w;
            };
            auto bandMembers = [&](const std::vector<int>& layer, int b) {
                std::vector<int> members;
                for (int v : layer)
                    if (band(v) == b)
                        members.push_back(v);
                return members;
            };
            for (auto& [r, layer] : layers)
            {
                std::stable_sort(layer.begin(), layer.end(), [&](int a, int b) {
                    return std::make_pair(band(a), graph.nodes[a].order) < std::make_pair(band(b), graph.nodes[b].order);
                });
                for (int b = 0; b <= freeBand; ++b)
                {
                    std::vector<int> members = bandMembers(layer, b);
                    if (members.empty())
                        continue;
                    float w = membersWidth(members);
                    if (b != freeBand)  // room for the box, and for its title when the cross axis is vertical (LR)
                        w += 2.f * boxPad + (vertical ? 0.f : titleHeight);
                    bandWidth[b] = std::max(bandWidth[b], w);
                }
            }
            std::vector<float> bandStart(freeBand + 1, 0.f);
            float x = 0.f;
            for (int b = 0; b <= freeBand; ++b)
                if (bandWidth[b] > 0.f)
                {
                    bandStart[b] = x;
                    x += bandWidth[b] + gapX;
                }
            const float totalCross = std::max(x - gapX, 0.f);

            // Positions: the layers along the main axis, the nodes centered in their band
            float offsetMain = 0.f;
            for (auto& [r, layer] : layers)
            {
                float thickness = 0.f;
                for (int v : layer)
                    thickness = std::max(thickness, main(graph.nodes[v]));
                for (int b = 0; b <= freeBand; ++b)
                {
                    std::vector<int> members = bandMembers(layer, b);
                    if (members.empty())
                        continue;
                    float cursor = bandStart[b] + (bandWidth[b] - membersWidth(members)) / 2.f;
                    if (b != freeBand && !vertical)
                        cursor += titleHeight / 2.f;  // the title is above the members
                    for (int v : members)
                    {
                        Node& nd = graph.nodes[v];
                        if (vertical)
                            nd.pos = ImVec2(cursor, offsetMain + (thickness - nd.size.y) / 2.f);
                        else
                            nd.pos = ImVec2(offsetMain + (thickness - nd.size.x) / 2.f, cursor);
                        cursor += cross(nd) + gapX;
                    }
                }
                offsetMain += thickness + gapY;
            }

            // Subgraph boxes: the bounding box of the members, padded, with room for the title
            for (size_t b = 0; b < graph.subgraphs.size(); ++b)
            {
                Subgraph& sub = graph.subgraphs[b];
                sub.hasBox = false;
                for (const Node& nd : graph.nodes)
                {
                    if (nd.subgraph != (int)b)
                        continue;
                    ImVec2 p0(nd.pos.x - boxPad, nd.pos.y - boxPad - titleHeight);
                    ImVec2 p1(nd.pos.x + nd.size.x + boxPad, nd.pos.y + nd.size.y + boxPad);
                    if (!sub.hasBox)
                        sub.boxMin = p0, sub.boxMax = p1, sub.hasBox = true;
                    sub.boxMin = ImVec2(std::min(sub.boxMin.x, p0.x), std::min(sub.boxMin.y, p0.y));
                    sub.boxMax = ImVec2(std::max(sub.boxMax.x, p1.x), std::max(sub.boxMax.y, p1.y));
                }
            }

            // Everything shifted along the main axis: room for the titles of the boxes above the first layer (TD),
            // and for the back edges that come back in front of the first layer
            float shift = (!graph.subgraphs.empty() && vertical ? titleHeight + boxPad : 0.f) + (!graph.backEdges.empty() ? 1.5f * em : 0.f);
            ImVec2 delta = vertical ? ImVec2(0.f, shift) : ImVec2(shift, 0.f);
            for (Node& nd : graph.nodes)
                nd.pos = ImVec2(nd.pos.x + delta.x, nd.pos.y + delta.y);
            for (Subgraph& sub : graph.subgraphs)
            {
                sub.boxMin = ImVec2(sub.boxMin.x + delta.x, sub.boxMin.y + delta.y);
                sub.boxMax = ImVec2(sub.boxMax.x + delta.x, sub.boxMax.y + delta.y);
            }
            graph.lanes = 0;
            for (const Edge& e : graph.edges)
                if (UsesLane(graph, e))
                    ++graph.lanes;
            const float lanes = 1.2f * em * (float)graph.lanes;  // room on the right (TD) or below (LR) for the lane edges
            const float mainTotal = offsetMain - gapY + shift;
            graph.size = vertical ? ImVec2(totalCross + lanes, mainTotal) : ImVec2(mainTotal, totalCross + lanes);
        }

        void LayoutSequence(Sequence& seq, float em)
        {
            const int n = (int)seq.participantIds.size();
            // A column per participant, wide enough for its label and for the messages to its neighbours
            seq.boxWidth.assign(n, 0.f);
            for (int i = 0; i < n; ++i)
                seq.boxWidth[i] = ImGui::CalcTextSize(seq.participantLabels[i].c_str()).x + 2.f * em;
            float columnGap = 3.f * em;
            for (const SequenceRow& row : seq.rows)
                if (!row.isNote && row.src != row.dst && std::abs(row.src - row.dst) == 1)
                    columnGap = std::max(columnGap, ImGui::CalcTextSize(row.text.c_str()).x + 2.f * em);
            seq.xCenter.assign(n, 0.f);
            float x = 0.f;
            for (int i = 0; i < n; ++i)
            {
                seq.xCenter[i] = x + seq.boxWidth[i] / 2.f;
                x += seq.boxWidth[i] + columnGap;
            }
            seq.totalWidth = x - columnGap;
            seq.boxHeight = ImGui::GetTextLineHeight() + em;
            seq.rowHeight = 2.2f * em;
            seq.height = seq.boxHeight + (float)(seq.rows.size() + 1) * seq.rowHeight + seq.boxHeight;
        }
    }

    void Layout(Diagram& diagram)
    {
        float em = ImGui::GetFontSize();
        if (diagram.kind == DiagramKind::Sequence)
            LayoutSequence(diagram.sequence, em);
        else
            LayoutGraph(diagram.graph, em);
        diagram.layoutFont = ImGui::GetFont();
        diagram.layoutFontSize = em;
    }

    ImVec2 DiagramSize(const Diagram& diagram)
    {
        float em = diagram.layoutFontSize;
        if (diagram.kind == DiagramKind::Sequence)
            return ImVec2(diagram.sequence.totalWidth + 2.f * em, diagram.sequence.height + em);
        return ImVec2(diagram.graph.size.x + 2.f * em, diagram.graph.size.y + em);
    }
}
