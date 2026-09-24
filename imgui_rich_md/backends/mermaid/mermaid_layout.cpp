// Part of ImGui Bundle - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_bundle
// Mermaid: the layout. Graphs are laid out in layers (back edges reversed, longest-path ranks, barycenter
// ordering, one band per subgraph); sizes come from the text, measured with the current font.
#include "rich_md_mermaid.h"

#include <algorithm>
#include <cmath>
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
        // For each edge: where it leaves its source and enters its target, as fractions of the node's side, spread
        // so that the edges of a node do not share a point (ordered by the position of their other end)
        std::vector<std::pair<float, float>> Anchors(const Graph& graph)
        {
            auto cross = [&](int v) { return graph.vertical ? graph.nodes[v].pos.x : graph.nodes[v].pos.y; };
            std::map<int, std::vector<int>> out, in;
            for (size_t i = 0; i < graph.edges.size(); ++i)
            {
                out[graph.edges[i].src].push_back((int)i);
                in[graph.edges[i].dst].push_back((int)i);
            }
            std::vector<std::pair<float, float>> result(graph.edges.size(), {0.5f, 0.5f});
            for (auto& [v, edges] : out)
            {
                std::stable_sort(edges.begin(), edges.end(), [&](int a, int b) { return cross(graph.edges[a].dst) < cross(graph.edges[b].dst); });
                for (size_t k = 0; k < edges.size(); ++k)
                    result[edges[k]].first = (float)(k + 1) / (float)(edges.size() + 1);
            }
            for (auto& [v, edges] : in)
            {
                std::stable_sort(edges.begin(), edges.end(), [&](int a, int b) { return cross(graph.edges[a].src) < cross(graph.edges[b].src); });
                for (size_t k = 0; k < edges.size(); ++k)
                    result[edges[k]].second = (float)(k + 1) / (float)(edges.size() + 1);
            }
            return result;
        }

        // The elbow polyline of an edge: a forward edge crosses the middle of the gap between the layers; a lane edge
        // (back edge, edge skipping a layer) leaves its source in the flow direction, goes into the gap after its
        // layer, along a lane past the graph, back through the gap before the target's layer, and into the target
        std::vector<ImVec2> EdgePoints(const Graph& graph, const Edge& e, int lane, float em, std::pair<float, float> anchor)
        {
            const bool vertical = graph.vertical;
            const Node& a = graph.nodes[e.src];
            const Node& b = graph.nodes[e.dst];
            ImVec2 a0 = a.pos, a1(a.pos.x + a.size.x, a.pos.y + a.size.y);
            ImVec2 b0 = b.pos;
            ImVec2 pa, pb;
            if (vertical)
                pa = ImVec2(a0.x + a.size.x * anchor.first, a1.y), pb = ImVec2(b0.x + b.size.x * anchor.second, b0.y);
            else
                pa = ImVec2(a1.x, a0.y + a.size.y * anchor.first), pb = ImVec2(b0.x, b0.y + b.size.y * anchor.second);
            if (UsesLane(graph, e))
            {
                const float halfGap = 0.7f * em;  // not the middle of the gap, where the forward edges run
                if (vertical)
                {
                    float lx = graph.size.x - 1.2f * em * (float)graph.lanes + 1.2f * em * (float)lane;
                    return {pa, ImVec2(pa.x, pa.y + halfGap), ImVec2(lx, pa.y + halfGap),
                            ImVec2(lx, pb.y - halfGap), ImVec2(pb.x, pb.y - halfGap), pb};
                }
                float ly = graph.size.y - 1.2f * em * (float)graph.lanes + 1.2f * em * (float)lane;
                return {pa, ImVec2(pa.x + halfGap, pa.y), ImVec2(pa.x + halfGap, ly),
                        ImVec2(pb.x - halfGap, ly), ImVec2(pb.x - halfGap, pb.y), pb};
            }
            if (vertical)
            {
                float midY = (a1.y + b0.y) / 2.f;
                if (std::fabs(pa.x - pb.x) < 1.f)
                    return {pa, pb};
                return {pa, ImVec2(pa.x, midY), ImVec2(pb.x, midY), pb};
            }
            float midX = (a1.x + b0.x) / 2.f;
            if (std::fabs(pa.y - pb.y) < 1.f)
                return {pa, pb};
            return {pa, ImVec2(midX, pa.y), ImVec2(midX, pb.y), pb};
        }

        // The polylines, and the labels on the middle segment when there is one, else at the middle of the edge
        void RouteEdges(Graph& graph, float em)
        {
            std::vector<std::pair<float, float>> anchors = Anchors(graph);
            int lane = 0;
            for (size_t i = 0; i < graph.edges.size(); ++i)
            {
                Edge& e = graph.edges[i];
                if (UsesLane(graph, e))
                    ++lane;
                e.points = EdgePoints(graph, e, lane, em, anchors[i]);
                if (e.label.empty())
                    continue;
                size_t k = e.points.size() > 2 ? e.points.size() / 2 - 1 : 0;
                ImVec2 s0 = e.points[k], s1 = e.points[k + 1];
                ImVec2 ts = ImGui::CalcTextSize(e.label.c_str());
                ImVec2 mid((s0.x + s1.x) / 2.f - ts.x / 2.f, (s0.y + s1.y) / 2.f - ts.y / 2.f);
                e.labelMin = ImVec2(mid.x - 2.f, mid.y);
                e.labelMax = ImVec2(mid.x + ts.x + 2.f, mid.y + ts.y);
            }
        }

        // Class diagrams: the cardinalities, beside the line, near its ends
        void PlaceCardinalities(const Graph& graph, std::vector<Relation>& relations, float em)
        {
            for (size_t i = 0; i < relations.size(); ++i)
            {
                const std::vector<ImVec2>& pts = graph.edges[i].points;
                Relation& r = relations[i];
                auto place = [&](const std::string& card, ImVec2 p, ImVec2 q) {
                    float dx = q.x - p.x, dy = q.y - p.y;
                    float n = std::max(std::sqrt(dx * dx + dy * dy), 1e-3f);
                    ImVec2 ts = ImGui::CalcTextSize(card.c_str());
                    float along = 1.3f * em, side = 0.4f * em;
                    return ImVec2(p.x + dx / n * along + (dy != 0.f ? side : -ts.x / 2.f), p.y + dy / n * along + (dx != 0.f ? side : -ts.y / 2.f));
                };
                r.cardinalitySrcPos = place(r.cardinalitySrc, pts[0], pts[1]);
                r.cardinalityDstPos = place(r.cardinalityDst, pts.back(), pts[pts.size() - 2]);
            }
        }

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
            RouteEdges(graph, em);
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

            // Rows: a note centered over its participants, a message's text above its line, or on the right of the
            // hook of a message to itself
            for (size_t k = 0; k < seq.rows.size(); ++k)
            {
                SequenceRow& r = seq.rows[k];
                r.y = seq.boxHeight + (float)(k + 1) * seq.rowHeight;
                ImVec2 ts = ImGui::CalcTextSize(r.text.c_str());
                if (r.isNote)
                {
                    float xMin = 1e30f, xMax = -1e30f, sum = 0.f;
                    for (int v : r.over)
                    {
                        float xv = seq.xCenter[v];
                        xMin = std::min(xMin, xv), xMax = std::max(xMax, xv), sum += xv;
                    }
                    float cx = sum / (float)r.over.size();
                    float half = std::max((xMax - xMin) / 2.f + em, ts.x / 2.f + 0.5f * em);
                    r.boxMin = ImVec2(cx - half, r.y - ts.y / 2.f - 0.3f * em);
                    r.boxMax = ImVec2(cx + half, r.y + ts.y / 2.f + 0.3f * em);
                    r.textMin = ImVec2(cx - ts.x / 2.f, r.y - ts.y / 2.f);
                }
                else if (r.src == r.dst)
                    r.textMin = ImVec2(seq.xCenter[r.src] + 1.5f * em + 0.4f * em, r.y - ts.y / 2.f);
                else
                    r.textMin = ImVec2((seq.xCenter[r.src] + seq.xCenter[r.dst]) / 2.f - ts.x / 2.f, r.y - ts.y - 0.2f * em);
                r.textMax = ImVec2(r.textMin.x + ts.x, r.textMin.y + ts.y);
            }
            for (SequenceLoop& loop : seq.loops)
            {
                loop.frameMin = ImVec2(-0.5f * em, seq.boxHeight + ((float)loop.first + 0.4f) * seq.rowHeight);
                loop.frameMax = ImVec2(seq.totalWidth + 0.5f * em, seq.boxHeight + ((float)loop.last + 1.3f) * seq.rowHeight);
            }
        }
    }

    void Layout(Diagram& diagram)
    {
        float em = ImGui::GetFontSize();
        if (diagram.kind == DiagramKind::Sequence)
            LayoutSequence(diagram.sequence, em);
        else
            LayoutGraph(diagram.graph, em);
        if (diagram.kind == DiagramKind::Class)
            PlaceCardinalities(diagram.graph, diagram.relations, em);
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
