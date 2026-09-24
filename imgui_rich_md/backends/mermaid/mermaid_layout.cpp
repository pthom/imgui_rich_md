// Part of ImGui Bundle - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_bundle
// Mermaid: the layout. Graphs are laid out in layers (back edges reversed, longest-path ranks, barycenter
// ordering, one band per subgraph); sizes come from the text, measured with the current font.
#include "rich_md_mermaid.h"

#include <algorithm>
#include <cfloat>
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

    std::vector<ImVec2> ShapeOutline(const Node& node, float em)
    {
        const float w = node.size.x, h = node.size.y;
        std::vector<ImVec2> pts;
        // An arc of the ellipse of center c, from angle a0 to a1 (radians, y down)
        auto arc = [&](ImVec2 c, ImVec2 radius, float a0, float a1, int segments) {
            for (int i = 0; i <= segments; ++i)
            {
                float a = a0 + (a1 - a0) * (float)i / (float)segments;
                pts.push_back(ImVec2(c.x + radius.x * std::cos(a), c.y + radius.y * std::sin(a)));
            }
        };
        const float pi = 3.14159265f;
        auto roundedRect = [&](float r) {
            r = std::min(r, std::min(w, h) / 2.f);
            arc(ImVec2(w - r, r), ImVec2(r, r), -pi / 2.f, 0.f, 6);
            arc(ImVec2(w - r, h - r), ImVec2(r, r), 0.f, pi / 2.f, 6);
            arc(ImVec2(r, h - r), ImVec2(r, r), pi / 2.f, pi, 6);
            arc(ImVec2(r, r), ImVec2(r, r), pi, 1.5f * pi, 6);
        };
        switch (node.shape)
        {
        case NodeShape::Rounded: roundedRect(0.6f * em); break;
        case NodeShape::Stadium: roundedRect(h / 2.f); break;
        case NodeShape::Circle:
        case NodeShape::DoubleCircle: arc(ImVec2(w / 2.f, h / 2.f), ImVec2(w / 2.f, h / 2.f), 0.f, 2.f * pi, 48); pts.pop_back(); break;
        case NodeShape::Cylinder:
        {
            float ry = 0.35f * em;  // the lids: the top half of the top ellipse, the bottom half of the bottom one
            arc(ImVec2(w / 2.f, ry), ImVec2(w / 2.f, ry), pi, 2.f * pi, 16);
            arc(ImVec2(w / 2.f, h - ry), ImVec2(w / 2.f, ry), 0.f, pi, 16);
            break;
        }
        case NodeShape::Diamond: pts = {ImVec2(w / 2.f, 0.f), ImVec2(w, h / 2.f), ImVec2(w / 2.f, h), ImVec2(0.f, h / 2.f)}; break;
        case NodeShape::Hexagon:
        {
            float s = h / 4.f;
            pts = {ImVec2(s, 0.f), ImVec2(w - s, 0.f), ImVec2(w, h / 2.f), ImVec2(w - s, h), ImVec2(s, h), ImVec2(0.f, h / 2.f)};
            break;
        }
        case NodeShape::Parallelogram: pts = {ImVec2(h / 2.f, 0.f), ImVec2(w, 0.f), ImVec2(w - h / 2.f, h), ImVec2(0.f, h)}; break;
        case NodeShape::ParallelogramAlt: pts = {ImVec2(0.f, 0.f), ImVec2(w - h / 2.f, 0.f), ImVec2(w, h), ImVec2(h / 2.f, h)}; break;
        case NodeShape::Trapezoid: pts = {ImVec2(h / 2.f, 0.f), ImVec2(w - h / 2.f, 0.f), ImVec2(w, h), ImVec2(0.f, h)}; break;
        case NodeShape::TrapezoidAlt: pts = {ImVec2(0.f, 0.f), ImVec2(w, 0.f), ImVec2(w - h / 2.f, h), ImVec2(h / 2.f, h)}; break;
        case NodeShape::Asymmetric: pts = {ImVec2(0.f, 0.f), ImVec2(w, 0.f), ImVec2(w, h), ImVec2(0.f, h), ImVec2(h / 4.f, h / 2.f)}; break;
        default: pts = {ImVec2(0.f, 0.f), ImVec2(w, 0.f), ImVec2(w, h), ImVec2(0.f, h)}; break;
        }
        return pts;
    }

    namespace
    {
        enum Side { Top, Bottom, Left, Right };

        // How far inside its box a node's outline is, on a side, at fraction t along that side
        float SideInset(const Node& node, float em, Side side, float t)
        {
            if (node.shape == NodeShape::Rect || node.shape == NodeShape::Subroutine || !node.compartments.empty())
                return 0.f;
            std::vector<ImVec2> outline = ShapeOutline(node, em);
            const bool alongX = side == Top || side == Bottom;  // top, bottom: a vertical line at x = t * width
            const float c = t * (alongX ? node.size.x : node.size.y);
            const bool nearest = side == Top || side == Left;
            float best = nearest ? FLT_MAX : -FLT_MAX;
            for (size_t i = 0; i < outline.size(); ++i)
            {
                ImVec2 p = outline[i], q = outline[(i + 1) % outline.size()];
                float a = alongX ? p.x : p.y, b = alongX ? q.x : q.y;
                if (a == b || c < std::min(a, b) || c > std::max(a, b))
                    continue;
                float u = (c - a) / (b - a);
                float v = alongX ? p.y + u * (q.y - p.y) : p.x + u * (q.x - p.x);
                best = nearest ? std::min(best, v) : std::max(best, v);
            }
            if (best == FLT_MAX || best == -FLT_MAX)
                return 0.f;
            return nearest ? best : (alongX ? node.size.y : node.size.x) - best;
        }

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
        // layer, along a lane past the graph, back through the gap before the target's layer, and into the target.
        // The ends are on the outlines of the shapes. BT and RL are laid out as TD and LR, then mirrored: the sides of
        // the shapes are those after the mirroring.
        std::vector<ImVec2> EdgePoints(const Graph& graph, const Edge& e, int lane, float em, std::pair<float, float> anchor)
        {
            const bool vertical = graph.vertical;
            const Node& a = graph.nodes[e.src];
            const Node& b = graph.nodes[e.dst];
            ImVec2 a0 = a.pos, a1(a.pos.x + a.size.x, a.pos.y + a.size.y);
            ImVec2 b0 = b.pos;
            ImVec2 pa, pb;
            if (vertical)
            {
                pa = ImVec2(a0.x + a.size.x * anchor.first, a1.y), pb = ImVec2(b0.x + b.size.x * anchor.second, b0.y);
                pa.y -= SideInset(a, em, graph.reversed ? Top : Bottom, anchor.first);
                pb.y += SideInset(b, em, graph.reversed ? Bottom : Top, anchor.second);
            }
            else
            {
                pa = ImVec2(a1.x, a0.y + a.size.y * anchor.first), pb = ImVec2(b0.x, b0.y + b.size.y * anchor.second);
                pa.x -= SideInset(a, em, graph.reversed ? Left : Right, anchor.first);
                pb.x += SideInset(b, em, graph.reversed ? Right : Left, anchor.second);
            }
            if (UsesLane(graph, e))
            {
                const float halfGap = 0.7f * em;  // not the middle of the gap, where the forward edges run
                if (vertical)
                {
                    float lx = graph.size.x - 1.2f * em * (float)graph.lanes + 1.2f * em * (float)lane;
                    return {pa, ImVec2(pa.x, a1.y + halfGap), ImVec2(lx, a1.y + halfGap),
                            ImVec2(lx, b0.y - halfGap), ImVec2(pb.x, b0.y - halfGap), pb};
                }
                float ly = graph.size.y - 1.2f * em * (float)graph.lanes + 1.2f * em * (float)lane;
                return {pa, ImVec2(a1.x + halfGap, pa.y), ImVec2(a1.x + halfGap, ly),
                        ImVec2(b0.x - halfGap, ly), ImVec2(b0.x - halfGap, pb.y), pb};
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

        // A node's size, from its text and its shape
        ImVec2 NodeSize(const Node& node, float em)
        {
            const float pad = 0.8f * em;
            if (!node.compartments.empty())  // a class: its widest line, a line of text per row, a padding per compartment
            {
                float w = 0.f, h = 0.f;
                for (const auto& compartment : node.compartments)
                {
                    for (const std::string& text : compartment)
                        w = std::max(w, ImGui::CalcTextSize(text.c_str()).x);
                    h += (float)compartment.size() * ImGui::GetTextLineHeight() + 0.6f * em;
                }
                return ImVec2(w + 2.f * pad, h);
            }
            ImVec2 text = ImGui::CalcTextSize(node.label.c_str());
            const float w = text.x + 2.f * pad, h = text.y + 2.f * pad;
            switch (node.shape)
            {
            case NodeShape::Diamond: return ImVec2(w + text.y, h + text.y);
            case NodeShape::Cylinder: return ImVec2(w, h + text.y);  // room for the lids
            case NodeShape::Subroutine: return ImVec2(w + 1.f * em, h);  // the inner lines, 0.5 em from the sides
            case NodeShape::Circle: { float d = std::max(text.x, text.y) + 2.f * pad; return ImVec2(d, d); }
            case NodeShape::DoubleCircle: { float d = std::max(text.x, text.y) + 2.f * pad + 0.7f * em; return ImVec2(d, d); }
            case NodeShape::Stadium:  // the round ends
            case NodeShape::Hexagon:
            case NodeShape::Parallelogram:
            case NodeShape::ParallelogramAlt:
            case NodeShape::Trapezoid:
            case NodeShape::TrapezoidAlt: return ImVec2(w + h / 2.f, h);  // the slanted sides
            case NodeShape::Asymmetric: return ImVec2(w + h / 4.f, h);  // the notch
            default: return ImVec2(w, h);
            }
        }

        // Subgraph boxes: the bounding box of the members, padded, with room for the title above them
        void ComputeSubgraphBoxes(Graph& graph, float boxPad, float titleHeight)
        {
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
        }

        // BT and RL: the layout of TD and LR, mirrored along the main axis (the titles of the boxes stay on top)
        void Mirror(Graph& graph, float boxPad, float titleHeight)
        {
            const bool vertical = graph.vertical;
            const float extent = vertical ? graph.size.y : graph.size.x;
            auto flip = [&](ImVec2 p) { return vertical ? ImVec2(p.x, extent - p.y) : ImVec2(extent - p.x, p.y); };
            auto flipBox = [&](ImVec2& p0, ImVec2& p1) {
                ImVec2 q0 = flip(p0), q1 = flip(p1);
                p0 = ImVec2(std::min(q0.x, q1.x), std::min(q0.y, q1.y));
                p1 = ImVec2(std::max(q0.x, q1.x), std::max(q0.y, q1.y));
            };
            for (Node& nd : graph.nodes)
            {
                ImVec2 p1(nd.pos.x + nd.size.x, nd.pos.y + nd.size.y);
                flipBox(nd.pos, p1);
            }
            for (Edge& e : graph.edges)
            {
                for (ImVec2& p : e.points)
                    p = flip(p);
                if (!e.label.empty())
                    flipBox(e.labelMin, e.labelMax);
            }
            ComputeSubgraphBoxes(graph, boxPad, titleHeight);
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
            const float gapX = 1.5f * em, gapY = 2.5f * em;
            const float lineHeight = ImGui::GetTextLineHeight();
            for (Node& node : graph.nodes)
                node.size = NodeSize(node, em);
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

            // The gap after each layer: wide enough for the labels of the edges that cross it
            std::map<int, float> gapAfter;
            for (const Edge& e : graph.edges)
                if (!e.label.empty() && !UsesLane(graph, e))
                {
                    ImVec2 ts = ImGui::CalcTextSize(e.label.c_str());
                    float needed = (vertical ? ts.y : ts.x + 4.f) + 1.5f * em;
                    gapAfter[graph.nodes[e.src].rank] = std::max(gapAfter[graph.nodes[e.src].rank], needed);
                }

            // Positions: the layers along the main axis, the nodes centered in their band
            float offsetMain = 0.f, lastGap = 0.f;
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
                lastGap = std::max(gapY, gapAfter[r]);
                offsetMain += thickness + lastGap;
            }
            ComputeSubgraphBoxes(graph, boxPad, titleHeight);

            // Everything shifted along the main axis: room for the titles of the boxes above the first layer (TD),
            // and for the back edges that come back in front of the first layer
            float shift = (!graph.subgraphs.empty() && vertical && !graph.reversed ? titleHeight + boxPad : 0.f) + (!graph.backEdges.empty() ? 1.5f * em : 0.f);
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
            const float mainTotal = offsetMain - lastGap + shift;
            graph.size = vertical ? ImVec2(totalCross + lanes, mainTotal) : ImVec2(mainTotal, totalCross + lanes);
            RouteEdges(graph, em);
            if (graph.reversed)
                Mirror(graph, boxPad, titleHeight);
        }

        // The bounding box of what a graph draws
        struct Extent
        {
            ImVec2 min = ImVec2(FLT_MAX, FLT_MAX), max = ImVec2(-FLT_MAX, -FLT_MAX);
            void Add(ImVec2 p0, ImVec2 p1)
            {
                min = ImVec2(std::min(min.x, p0.x), std::min(min.y, p0.y));
                max = ImVec2(std::max(max.x, p1.x), std::max(max.y, p1.y));
            }
        };

        // The translation that brings an extent inside [-em, size.x + em] x [-em / 2, size.y + em / 2] (the space
        // Draw reserves around the diagram), and the size that holds it
        ImVec2 FitDelta(const Extent& extent, ImVec2* size, float em)
        {
            if (extent.min.x > extent.max.x)
                return ImVec2(0.f, 0.f);
            ImVec2 delta(std::max(0.f, -em - extent.min.x), std::max(0.f, -em / 2.f - extent.min.y));
            size->x = std::max(size->x + delta.x, extent.max.x + delta.x - em);
            size->y = std::max(size->y + delta.y, extent.max.y + delta.y - em / 2.f);
            return delta;
        }

        void FitGraph(Graph& graph, std::vector<Relation>& relations, float em)
        {
            Extent extent;
            for (const Node& nd : graph.nodes)
                extent.Add(nd.pos, ImVec2(nd.pos.x + nd.size.x, nd.pos.y + nd.size.y));
            for (const Subgraph& sub : graph.subgraphs)
                if (sub.hasBox)
                    extent.Add(sub.boxMin, sub.boxMax);
            for (const Edge& e : graph.edges)
            {
                for (ImVec2 p : e.points)
                    extent.Add(p, p);
                if (!e.label.empty())
                    extent.Add(e.labelMin, e.labelMax);
            }
            for (const Relation& r : relations)
                for (const auto& [card, pos] : {std::make_pair(&r.cardinalitySrc, r.cardinalitySrcPos), std::make_pair(&r.cardinalityDst, r.cardinalityDstPos)})
                    if (!card->empty())
                    {
                        ImVec2 ts = ImGui::CalcTextSize(card->c_str());
                        extent.Add(pos, ImVec2(pos.x + ts.x, pos.y + ts.y));
                    }
            ImVec2 d = FitDelta(extent, &graph.size, em);
            if (d.x == 0.f && d.y == 0.f)
                return;
            auto move = [&](ImVec2& p) { p = ImVec2(p.x + d.x, p.y + d.y); };
            for (Node& nd : graph.nodes)
                move(nd.pos);
            for (Subgraph& sub : graph.subgraphs)
                move(sub.boxMin), move(sub.boxMax);
            for (Edge& e : graph.edges)
            {
                for (ImVec2& p : e.points)
                    move(p);
                move(e.labelMin), move(e.labelMax);
            }
            for (Relation& r : relations)
                move(r.cardinalitySrcPos), move(r.cardinalityDstPos);
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

            // What overflows (a wide note, the text of a message to itself) widens the diagram
            Extent extent;
            for (int i = 0; i < n; ++i)
                extent.Add(ImVec2(seq.xCenter[i] - seq.boxWidth[i] / 2.f, 0.f), ImVec2(seq.xCenter[i] + seq.boxWidth[i] / 2.f, seq.height));
            for (const SequenceRow& r : seq.rows)
            {
                extent.Add(r.textMin, r.textMax);
                if (r.isNote)
                    extent.Add(r.boxMin, r.boxMax);
            }
            for (const SequenceLoop& loop : seq.loops)
                extent.Add(loop.frameMin, loop.frameMax);
            ImVec2 size(seq.totalWidth, seq.height);
            ImVec2 d = FitDelta(extent, &size, em);
            seq.totalWidth = size.x;
            if (d.x == 0.f)
                return;
            for (float& xc : seq.xCenter)
                xc += d.x;
            for (SequenceRow& r : seq.rows)
                r.textMin.x += d.x, r.textMax.x += d.x, r.boxMin.x += d.x, r.boxMax.x += d.x;
            for (SequenceLoop& loop : seq.loops)
                loop.frameMin.x += d.x, loop.frameMax.x += d.x;
        }
    }

    void Layout(Diagram& diagram)
    {
        float em = ImGui::GetFontSize();
        if (diagram.kind == DiagramKind::Sequence)
            LayoutSequence(diagram.sequence, em);
        else
        {
            LayoutGraph(diagram.graph, em);
            if (diagram.kind == DiagramKind::Class)
                PlaceCardinalities(diagram.graph, diagram.relations, em);
            FitGraph(diagram.graph, diagram.relations, em);
        }
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
