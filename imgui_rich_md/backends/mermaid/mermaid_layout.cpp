// Part of ImGui Bundle - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_bundle
// Mermaid: the layout. Graphs are laid out in layers (back edges reversed, longest-path ranks, barycenter
// ordering, one band per subgraph); sizes come from the text, measured with the current font.
#include "rich_md_mermaid.h"

#include <algorithm>
#include <cfloat>
#include <climits>
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
        // so that the edges of a node do not share a point (ordered by the position of their other end; the lane edges
        // last, on the side of the lanes)
        std::vector<std::pair<float, float>> Anchors(const Graph& graph, float em)
        {
            // the lane edges after the others, on the side of the lanes: those that skip layers first, the outer lanes
            // on the right; then the back edges, the outer lanes on the left (so that the lane routes do not cross)
            auto cross = [&](int edge, int v) {
                const Edge& e = graph.edges[edge];
                if (e.lane > 0)
                    return graph.backEdges.count({e.src, e.dst}) ? 3e6f - (float)e.lane : 1e6f + (float)e.lane;  // exact in float
                return graph.vertical ? graph.nodes[v].pos.x : graph.nodes[v].pos.y;
            };
            std::map<int, std::vector<int>> out, in;
            for (size_t i = 0; i < graph.edges.size(); ++i)
            {
                if (graph.edges[i].srcBox < 0)
                    out[graph.edges[i].src].push_back((int)i);
                if (graph.edges[i].dstBox < 0)
                    in[graph.edges[i].dst].push_back((int)i);
            }
            std::vector<std::pair<float, float>> result(graph.edges.size(), {0.5f, 0.5f});
            for (auto& [v, edges] : out)
            {
                std::stable_sort(edges.begin(), edges.end(), [&](int a, int b) { return cross(a, graph.edges[a].dst) < cross(b, graph.edges[b].dst); });
                for (size_t k = 0; k < edges.size(); ++k)
                    result[edges[k]].first = (float)(k + 1) / (float)(edges.size() + 1);
            }
            for (auto& [v, edges] : in)
            {
                std::stable_sort(edges.begin(), edges.end(), [&](int a, int b) { return cross(a, graph.edges[a].src) < cross(b, graph.edges[b].src); });
                for (size_t k = 0; k < edges.size(); ++k)
                    result[edges[k]].second = (float)(k + 1) / (float)(edges.size() + 1);
            }
            // An edge must not enter a node where another one leaves the layer before (their lines would overlap in the
            // gap between the layers): the entries of the node then move together along its side
            auto sideAt = [&](int v, float t) {
                const Node& nd = graph.nodes[v];
                return graph.vertical ? nd.pos.x + nd.size.x * t : nd.pos.y + nd.size.y * t;
            };
            for (auto& [v, edges] : in)
            {
                std::vector<float> exits;
                for (size_t i = 0; i < graph.edges.size(); ++i)
                {
                    const Edge& f = graph.edges[i];
                    if (f.srcBox < 0 && f.dst != v && f.lane == 0 && graph.nodes[f.src].rank == graph.nodes[v].rank - 1)
                        exits.push_back(sideAt(f.src, result[i].first));
                }
                const float length = graph.vertical ? graph.nodes[v].size.x : graph.nodes[v].size.y;
                const float step = 0.45f * em;
                for (float delta : {0.f, step, -step, 2.f * step, -2.f * step})
                {
                    bool clear = true;
                    for (int i : edges)
                    {
                        float t = result[i].second + delta / length;
                        clear = clear && t >= 0.1f && t <= 0.9f;
                        for (float x : exits)
                            clear = clear && std::fabs(sideAt(v, t) - x) >= 0.4f * em;
                    }
                    if (clear)
                    {
                        for (int i : edges)
                            result[i].second += delta / length;
                        break;
                    }
                }
            }
            return result;
        }

        // The elbow polyline of an edge: a forward edge crosses the middle of the gap between the layers; a lane edge
        // (back edge, edge skipping a layer) leaves its source in the flow direction, goes into the gap after its
        // layer, along a lane past the graph, back through the gap before the target's layer, and into the target.
        // The ends are on the outlines of the shapes. BT and RL are laid out as TD and LR, then mirrored: the sides of
        // the shapes are those after the mirroring.
        // Where an edge's end on a subgraph's box is, across: in front of the other end, inside the box (LR: below
        // its title)
        float BoxCross(const Graph& graph, int box, float other, float em)
        {
            const Subgraph& sub = graph.subgraphs[box];
            float lo = graph.vertical ? sub.boxMin.x : sub.boxMin.y + ImGui::GetTextLineHeight() + 0.4f * em;
            float hi = graph.vertical ? sub.boxMax.x : sub.boxMax.y;
            return std::clamp(other, lo + em, std::max(lo + em, hi - em));
        }

        // The ends of an edge, across: where it leaves its source, where it enters its target (a node's anchor, or a
        // point of a box)
        std::pair<float, float> EndsAcross(const Graph& graph, const Edge& e, std::pair<float, float> anchor, float em)
        {
            auto nodeAt = [&](int v, float t) {
                const Node& nd = graph.nodes[v];
                return graph.vertical ? nd.pos.x + nd.size.x * t : nd.pos.y + nd.size.y * t;
            };
            auto center = [&](int box) {
                const Subgraph& sub = graph.subgraphs[box];
                return graph.vertical ? (sub.boxMin.x + sub.boxMax.x) / 2.f : (sub.boxMin.y + sub.boxMax.y) / 2.f;
            };
            float c0 = e.srcBox >= 0 ? 0.f : nodeAt(e.src, anchor.first);
            float c1 = e.dstBox >= 0 ? 0.f : nodeAt(e.dst, anchor.second);
            if (e.srcBox >= 0 && e.dstBox >= 0)
                c0 = BoxCross(graph, e.srcBox, center(e.dstBox), em), c1 = BoxCross(graph, e.dstBox, c0, em);
            else if (e.srcBox >= 0)
                c0 = BoxCross(graph, e.srcBox, c1, em);
            else if (e.dstBox >= 0)
                c1 = BoxCross(graph, e.dstBox, c0, em);
            return {c0, c1};
        }

        // The extent of a layer along the main axis
        std::pair<float, float> LayerExtent(const Graph& graph, int rank)
        {
            float lo = FLT_MAX, hi = -FLT_MAX;
            for (const Node& nd : graph.nodes)
                if (nd.rank == rank)
                {
                    float p = graph.vertical ? nd.pos.y : nd.pos.x, size = graph.vertical ? nd.size.y : nd.size.x;
                    lo = std::min(lo, p), hi = std::max(hi, p + size);
                }
            return {lo, hi};
        }

        std::vector<ImVec2> EdgePoints(const Graph& graph, const Edge& e, float em, std::pair<float, float> anchor)
        {
            const bool vertical = graph.vertical;
            const Node& a = graph.nodes[e.src];  // for an end on a box: the member that stands for the box
            const Node& b = graph.nodes[e.dst];
            // Along the main axis: where the source ends (its node, or its box), where the target starts
            auto mainOf = [&](ImVec2 p) { return vertical ? p.y : p.x; };
            float srcEnd = e.srcBox >= 0 ? mainOf(graph.subgraphs[e.srcBox].boxMax) : mainOf(ImVec2(a.pos.x + a.size.x, a.pos.y + a.size.y));
            float dstStart = e.dstBox >= 0 ? mainOf(graph.subgraphs[e.dstBox].boxMin) : mainOf(b.pos);
            auto [c0, c1] = EndsAcross(graph, e, anchor, em);
            float m0 = srcEnd - (e.srcBox >= 0 ? 0.f : SideInset(a, em, vertical ? (graph.reversed ? Top : Bottom) : (graph.reversed ? Left : Right), anchor.first));
            float m1 = dstStart + (e.dstBox >= 0 ? 0.f : SideInset(b, em, vertical ? (graph.reversed ? Bottom : Top) : (graph.reversed ? Right : Left), anchor.second));
            ImVec2 pa = vertical ? ImVec2(c0, m0) : ImVec2(m0, c0), pb = vertical ? ImVec2(c1, m1) : ImVec2(m1, c1);
            if (e.lane > 0)
            {
                // not in the middle of the gaps, where the forward edges run; each lane on its own approach lines
                const float halfGap = 0.7f * em, step = 0.45f * em;
                float exit = std::max(LayerExtent(graph, a.rank).second, srcEnd) + halfGap + (float)e.exitTrack * step;
                float entry = std::min(LayerExtent(graph, b.rank).first, dstStart) - halfGap - (float)e.entryTrack * step;
                float lanePos = (vertical ? graph.size.x : graph.size.y) - 1.2f * em * (float)graph.lanes + 1.2f * em * (float)e.lane;
                if (vertical)
                    return {pa, ImVec2(pa.x, exit), ImVec2(lanePos, exit), ImVec2(lanePos, entry), ImVec2(pb.x, entry), pb};
                return {pa, ImVec2(exit, pa.y), ImVec2(exit, lanePos), ImVec2(entry, lanePos), ImVec2(entry, pb.y), pb};
            }
            auto channel = graph.channels.find(a.rank);
            if (vertical)
            {
                float midY = channel != graph.channels.end() ? channel->second + e.track : (srcEnd + dstStart) / 2.f;
                if (std::fabs(pa.x - pb.x) < 1.f)
                    return {pa, pb};
                return {pa, ImVec2(pa.x, midY), ImVec2(pb.x, midY), pb};
            }
            float midX = channel != graph.channels.end() ? channel->second + e.track : (srcEnd + dstStart) / 2.f;
            if (std::fabs(pa.y - pb.y) < 1.f)
                return {pa, pb};
            return {pa, ImVec2(midX, pa.y), ImVec2(midX, pb.y), pb};
        }

        // Tracks: in a channel, the segments across of two edges that overlap run on their own lines, unless the two
        // edges leave the same node or enter the same node (a fan stays a tree). Returns the room the tracks of each
        // channel take (from the first to the last): 0.45 em between two tracks, a line of text when a label sits on one.
        std::map<int, float> AssignTracks(Graph& graph, float em)
        {
            std::vector<std::pair<float, float>> anchors = Anchors(graph, em);
            struct Segment { int edge; float lo, hi, x0, x1; };  // x0: where it leaves its source, x1: where it enters its target
            std::map<int, std::vector<Segment>> segments;  // channel (the rank of the sources) -> its segments across
            for (size_t i = 0; i < graph.edges.size(); ++i)
            {
                Edge& e = graph.edges[i];
                e.track = 0.f;
                if (UsesLane(graph, e))
                    continue;
                auto [x0, x1] = EndsAcross(graph, e, anchors[i], em);
                if (std::fabs(x0 - x1) >= 1.f)
                    segments[graph.nodes[e.src].rank].push_back({(int)i, std::min(x0, x1), std::max(x0, x1), x0, x1});
            }
            std::map<int, float> trackRoom;
            for (auto& channel : segments)
            {
                const int r = channel.first;
                std::vector<Segment>& list = channel.second;
                std::stable_sort(list.begin(), list.end(), [](const Segment& a, const Segment& b) { return a.lo < b.lo; });
                // Two segments conflict when they overlap, unless their edges form a fan (they leave, or enter, the same
                // node) and no label sits where they meet
                auto onLabel = [&](const Segment& g, const Segment& other) {  // other's segment where g's label sits
                    const std::string& label = graph.edges[g.edge].label;
                    if (label.empty())
                        return false;
                    float half = ImGui::CalcTextSize(label.c_str()).x / 2.f + 0.3f * em, mid = (g.lo + g.hi) / 2.f;
                    return mid - half < other.hi && other.lo < mid + half;
                };
                auto conflict = [&](size_t a, size_t b) {
                    const Segment &sa = list[a], &sb = list[b];
                    const Edge &ea = graph.edges[sa.edge], &eb = graph.edges[sb.edge];
                    bool overlap = sa.lo < sb.hi + 0.3f * em && sb.lo < sa.hi + 0.3f * em;
                    return a != b && overlap && ((ea.src != eb.src && ea.dst != eb.dst) || onLabel(sa, sb) || onLabel(sb, sa));
                };
                // Of two conflicting segments, the one that runs above: the order where fewer of their vertical parts cross
                // the other's segment (the descent into the target of the one above, the rise from the source of the one
                // below). The segments are placed after those that must run above them.
                auto within = [&](float x, const Segment& g) { return x > g.lo - 0.2f * em && x < g.hi + 0.2f * em; };
                auto above = [&](size_t a, size_t b) {
                    if (!conflict(a, b))
                        return false;
                    int aAbove = (within(list[a].x1, list[b]) ? 1 : 0) + (within(list[b].x0, list[a]) ? 1 : 0);
                    int bAbove = (within(list[b].x1, list[a]) ? 1 : 0) + (within(list[a].x0, list[b]) ? 1 : 0);
                    return aAbove < bAbove;
                };
                std::vector<std::vector<size_t>> tracks;  // the segments on each track
                std::vector<int> trackOf(list.size(), -1);
                for (size_t placed = 0; placed < list.size(); ++placed)
                {
                    size_t k = list.size();
                    for (size_t c = 0; c < list.size() && k == list.size(); ++c)  // the first one whose predecessors are placed
                    {
                        if (trackOf[c] >= 0)
                            continue;
                        bool ready = true;
                        for (size_t o = 0; o < list.size(); ++o)
                            ready = ready && !(trackOf[o] < 0 && above(o, c));
                        if (ready)
                            k = c;
                    }
                    for (size_t c = 0; c < list.size() && k == list.size(); ++c)  // a cycle: the first one left
                        if (trackOf[c] < 0)
                            k = c;
                    auto conflicts = [&](size_t other) { return conflict(k, other); };
                    size_t t = 0;
                    for (size_t o = 0; o < list.size(); ++o)
                        if (trackOf[o] >= 0 && above(o, k))
                            t = std::max(t, (size_t)trackOf[o] + 1);
                    while (t < tracks.size() && std::any_of(tracks[t].begin(), tracks[t].end(), conflicts))
                        ++t;
                    while (t >= tracks.size())
                        tracks.emplace_back();
                    tracks[t].push_back(k);
                    trackOf[k] = (int)t;
                }
                int count = (int)tracks.size();
                bool labels = false;
                for (const Segment& seg : list)
                    labels = labels || !graph.edges[seg.edge].label.empty();
                const float spacing = labels && count > 1 ? ImGui::GetTextLineHeight() + 0.3f * em : 0.45f * em;
                for (size_t k = 0; k < list.size(); ++k)
                    graph.edges[list[k].edge].track = ((float)trackOf[k] - (float)(count - 1) / 2.f) * spacing;
                trackRoom[r] = (float)(count - 1) * spacing;
            }
            return trackRoom;
        }

        bool SegmentThroughRect(ImVec2 p, ImVec2 q, ImVec2 r0, ImVec2 r1);

        // The polylines, and the labels on the middle segment when there is one, else at the middle of the edge
        void RouteEdges(Graph& graph, float em)
        {
            std::vector<std::pair<float, float>> anchors = Anchors(graph, em);
            for (size_t i = 0; i < graph.edges.size(); ++i)
                graph.edges[i].points = EdgePoints(graph, graph.edges[i], em, anchors[i]);

            // The labels: on the middle segment (the channel's, for a forward edge), at its middle, or slid along it to
            // where no other edge crosses the label and no node is under it
            for (size_t i = 0; i < graph.edges.size(); ++i)
            {
                Edge& e = graph.edges[i];
                if (e.label.empty())
                    continue;
                size_t k = e.points.size() > 2 ? e.points.size() / 2 - 1 : 0;
                ImVec2 s0 = e.points[k], s1 = e.points[k + 1];
                auto channel = graph.channels.find(graph.nodes[e.src].rank);
                const bool onTrack = !UsesLane(graph, e) && channel != graph.channels.end();
                ImVec2 ts = ImGui::CalcTextSize(e.label.c_str());
                auto boxAt = [&](float t, ImVec2* b0, ImVec2* b1) {
                    ImVec2 c(s0.x + (s1.x - s0.x) * t, s0.y + (s1.y - s0.y) * t);
                    if (onTrack && e.points.size() == 2 && t == 0.5f)  // a straight edge: its label in the channel
                        (graph.vertical ? c.y : c.x) = channel->second + e.track;
                    *b0 = ImVec2(c.x - ts.x / 2.f - 2.f, c.y - ts.y / 2.f);
                    *b1 = ImVec2(c.x + ts.x / 2.f + 2.f, c.y + ts.y / 2.f);
                };
                auto clear = [&](ImVec2 b0, ImVec2 b1) {
                    ImVec2 i0(b0.x + 1.f, b0.y + 1.f), i1(b1.x - 1.f, b1.y - 1.f);
                    for (size_t j = 0; j < graph.edges.size(); ++j)
                        if (j != i && graph.edges[j].style != LineStyle::Invisible)
                            for (size_t m = 0; m + 1 < graph.edges[j].points.size(); ++m)
                                if (SegmentThroughRect(graph.edges[j].points[m], graph.edges[j].points[m + 1], i0, i1))
                                    return false;
                    for (const Node& nd : graph.nodes)
                        if (nd.pos.x < i1.x && i0.x < nd.pos.x + nd.size.x && nd.pos.y < i1.y && i0.y < nd.pos.y + nd.size.y)
                            return false;
                    return true;
                };
                boxAt(0.5f, &e.labelMin, &e.labelMax);
                for (float t : {0.5f, 0.7f, 0.3f, 0.85f, 0.15f})
                {
                    ImVec2 b0, b1;
                    boxAt(t, &b0, &b1);
                    if (clear(b0, b1))
                    {
                        e.labelMin = b0, e.labelMax = b1;
                        break;
                    }
                }
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
                    for (const ClassLine& line : compartment)
                        w = std::max(w, ClassLineSize(line).x);
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

        // The width a box needs for its title
        float TitleWidth(const Subgraph& sub, float em) { return ImGui::CalcTextSize(sub.title.c_str()).x + 1.5f * em; }
        // A box: its padding around the members, and the room of its title
        float BoxPadding(float em) { return 0.8f * em; }
        float TitleRoom(float em) { return ImGui::GetTextLineHeight() + 0.4f * em; }

        // Subgraph boxes: the bounding box of the members, padded, with room for the title above them, and at least
        // as wide as the title
        void ComputeSubgraphBoxes(Graph& graph, float boxPad, float titleHeight, float em)
        {
            // the children first (a subgraph comes after its parent in the source): a box encloses its nodes and the
            // boxes of its children
            for (size_t b = graph.subgraphs.size(); b-- > 0;)
            {
                Subgraph& sub = graph.subgraphs[b];
                sub.hasBox = false;
                auto enclose = [&](ImVec2 min, ImVec2 max) {
                    ImVec2 p0(min.x - boxPad, min.y - boxPad - titleHeight), p1(max.x + boxPad, max.y + boxPad);
                    if (!sub.hasBox)
                        sub.boxMin = p0, sub.boxMax = p1, sub.hasBox = true;
                    sub.boxMin = ImVec2(std::min(sub.boxMin.x, p0.x), std::min(sub.boxMin.y, p0.y));
                    sub.boxMax = ImVec2(std::max(sub.boxMax.x, p1.x), std::max(sub.boxMax.y, p1.y));
                };
                for (const Node& nd : graph.nodes)
                    if (nd.subgraph == (int)b)
                        enclose(nd.pos, ImVec2(nd.pos.x + nd.size.x, nd.pos.y + nd.size.y));
                for (size_t c = b + 1; c < graph.subgraphs.size(); ++c)
                    if (graph.subgraphs[c].parent == (int)b && graph.subgraphs[c].hasBox)
                        enclose(graph.subgraphs[c].boxMin, graph.subgraphs[c].boxMax);
                float missing = TitleWidth(sub, em) - (sub.boxMax.x - sub.boxMin.x);
                if (sub.hasBox && missing > 0.f)
                    sub.boxMin.x -= missing / 2.f, sub.boxMax.x += missing / 2.f;
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
            ComputeSubgraphBoxes(graph, boxPad, titleHeight, ImGui::GetFontSize());
        }

        // The non-decreasing sequence closest to t (least squares): isotonic regression, by pooling adjacent violators
        std::vector<float> Isotonic(const std::vector<float>& t)
        {
            std::vector<float> mean;
            std::vector<int> size;
            for (float x : t)
            {
                mean.push_back(x), size.push_back(1);
                while (mean.size() > 1 && mean[mean.size() - 2] > mean.back())
                {
                    size_t k = mean.size() - 2;
                    mean[k] = (mean[k] * (float)size[k] + mean[k + 1] * (float)size[k + 1]) / (float)(size[k] + size[k + 1]);
                    size[k] += size[k + 1];
                    mean.pop_back(), size.pop_back();
                }
            }
            std::vector<float> out;
            for (size_t k = 0; k < mean.size(); ++k)
                out.insert(out.end(), (size_t)size[k], mean[k]);
            return out;
        }

        // gapY: the gap between two layers; relations: the markers and cardinalities of a class diagram
        void LayoutGraph(Graph& graph, float em, float gapY, const std::vector<Relation>* relations)
        {
            const int n = (int)graph.nodes.size();
            auto boxEdge = [](const Edge& e) { return e.srcBox >= 0 || e.dstBox >= 0; };
            std::vector<std::vector<int>> succs(n);
            for (const Edge& e : graph.edges)
                if (!boxEdge(e))
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
                if (e.src == e.dst || boxEdge(e))
                    continue;  // a node linked to itself does not change its rank
                if (graph.backEdges.count({e.src, e.dst}))
                    forwardPreds[e.src].push_back(e.dst);
                else
                    forwardPreds[e.dst].push_back(e.src);
            }
            // Links to a subgraph's box: their other end goes before the box's entry nodes (no predecessor in the box),
            // or after its exit nodes (no successor in the box). In the order of the ranking (back edges reversed), so
            // that a cycle inside the box has its entry and its exit.
            auto inBox = [&](int v, int box) {
                for (int b = graph.nodes[v].subgraph; b >= 0; b = graph.subgraphs[b].parent)
                    if (b == box)
                        return true;
                return false;
            };
            const std::vector<std::vector<int>> edgePreds = forwardPreds;  // without the constraints of the box links
            auto boxEnds = [&](int box, bool entry) {
                std::vector<int> ends;
                for (int v = 0; v < n; ++v)
                {
                    bool inside = false;  // after (entry) or before (exit) another member
                    for (int w = 0; w < n; ++w)
                        for (int p : edgePreds[w])
                            if (entry ? w == v && inBox(p, box) : p == v && inBox(w, box))
                                inside = true;
                    if (inBox(v, box) && !inside)
                        ends.push_back(v);
                }
                return ends;
            };
            for (const Edge& e : graph.edges)
            {
                if (!boxEdge(e))
                    continue;
                std::vector<int> from = e.srcBox >= 0 ? boxEnds(e.srcBox, false) : std::vector<int>{e.src};
                std::vector<int> to = e.dstBox >= 0 ? boxEnds(e.dstBox, true) : std::vector<int>{e.dst};
                for (int u : from)
                    for (int v : to)
                        if (u != v && !(e.dstBox >= 0 && inBox(u, e.dstBox)) && !(e.srcBox >= 0 && inBox(v, e.srcBox)))
                            forwardPreds[v].push_back(u);
            }

            // Ranks: the longest path from a source
            std::vector<int> rank(n, -1);
            std::function<int(int)> rankOf = [&](int v) {
                if (rank[v] == -2)
                    return 0;  // a cycle made by links to boxes: broken here
                if (rank[v] < 0)
                {
                    rank[v] = -2;
                    int r = 0;
                    for (int p : forwardPreds[v])
                        r = std::max(r, rankOf(p) + 1);
                    rank[v] = r;
                }
                return rank[v];
            };
            for (int v = 0; v < n; ++v)
                rankOf(v);
            std::vector<std::vector<int>> forwardSuccs(n);
            for (int v = 0; v < n; ++v)
                for (int p : forwardPreds[v])
                    forwardSuccs[p].push_back(v);
            // A node without predecessor goes just above its highest successor rather than to the first layer: a
            // note stays next to its class, a node linked only to a deep one does not need a lane
            for (int v = 0; v < n; ++v)
            {
                if (!forwardPreds[v].empty() || forwardSuccs[v].empty())
                    continue;
                int highest = INT_MAX;
                for (int w : forwardSuccs[v])
                    highest = std::min(highest, rank[w]);
                rank[v] = std::max(rank[v], highest - 1);
            }
            // A link to a box: the member that stands for the box (its highest entry node, its lowest exit node)
            for (Edge& e : graph.edges)
            {
                auto pick = [&](int box, bool entry) {
                    int best = -1;
                    for (int v : boxEnds(box, entry))
                        if (best < 0 || (entry ? rank[v] < rank[best] : rank[v] > rank[best]))
                            best = v;
                    return best;
                };
                if (e.srcBox >= 0)
                    e.src = pick(e.srcBox, false);
                if (e.dstBox >= 0)
                    e.dst = pick(e.dstBox, true);
            }
            std::map<int, std::vector<int>> layers;
            for (int v = 0; v < n; ++v)
            {
                graph.nodes[v].rank = rank[v];
                layers[graph.nodes[v].rank].push_back(v);
            }
            for (auto& [r, layer] : layers)
                for (size_t i = 0; i < layer.size(); ++i)
                    graph.nodes[layer[i]].order = (int)i;

            // Ordering: barycenter sweeps, downward (a node goes to the mean position of its predecessors) and upward
            // (of its successors), keeping the ordering with the fewest crossings between neighbouring layers
            auto crossings = [&]() {
                int count = 0;
                for (int v = 0; v < n; ++v)
                    for (int w = v + 1; w < n; ++w)
                        for (int p : forwardPreds[v])
                            for (int q : forwardPreds[w])
                            {
                                const Node &nv = graph.nodes[v], &nw = graph.nodes[w], &np = graph.nodes[p], &nq = graph.nodes[q];
                                if (nv.rank != nw.rank || np.rank != nv.rank - 1 || nq.rank != nw.rank - 1 || p == q)
                                    continue;
                                count += (nv.order - nw.order) * (np.order - nq.order) < 0 ? 1 : 0;
                            }
                return count;
            };
            auto sweep = [&](bool down) {
                auto sortLayer = [&](int r, std::vector<int>& layer) {
                    std::map<int, float> key;
                    for (int v : layer)
                    {
                        float sum = 0.f;
                        int count = 0;
                        for (int p : down ? forwardPreds[v] : forwardSuccs[v])
                            if (down ? graph.nodes[p].rank < r : graph.nodes[p].rank > r)
                                sum += (float)graph.nodes[p].order, ++count;
                        key[v] = count ? sum / (float)count : (float)graph.nodes[v].order;
                    }
                    std::stable_sort(layer.begin(), layer.end(), [&](int a, int b) { return key[a] < key[b]; });
                    for (size_t i = 0; i < layer.size(); ++i)
                        graph.nodes[layer[i]].order = (int)i;
                };
                if (down)
                    for (auto& [r, layer] : layers)
                        sortLayer(r, layer);
                else
                    for (auto it = layers.rbegin(); it != layers.rend(); ++it)
                        sortLayer(it->first, it->second);
            };
            sweep(true);
            std::map<int, std::vector<int>> best = layers;
            int fewest = crossings();
            for (int i = 1; i < 8 && fewest > 0; ++i)
            {
                sweep(i % 2 == 0);
                int c = crossings();
                if (c < fewest)
                    fewest = c, best = layers;
            }
            layers = best;
            for (auto& [r, layer] : layers)
                for (size_t i = 0; i < layer.size(); ++i)
                    graph.nodes[layer[i]].order = (int)i;

            // Sizes
            const float gapX = 1.5f * em;
            for (Node& node : graph.nodes)
                if (!node.keepSize)
                    node.size = NodeSize(node, em);
            const bool vertical = graph.vertical;
            auto cross = [&](const Node& nd) { return vertical ? nd.size.x : nd.size.y; };
            auto main = [&](const Node& nd) { return vertical ? nd.size.y : nd.size.x; };

            // Bands on the cross axis, nested like the subgraphs: a subgraph's band holds the bands of its children (in
            // source order), then a column for its own nodes; the root is the same for the top-level subgraphs and the
            // free nodes. A column is as wide as its widest layer, so that the subgraph boxes never overlap
            const int freeBand = (int)graph.subgraphs.size();  // the root
            auto band = [&](int v) { return graph.nodes[v].subgraph >= 0 ? graph.nodes[v].subgraph : freeBand; };
            const float boxPad = BoxPadding(em);
            const float titleHeight = TitleRoom(em);
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
                std::stable_sort(layer.begin(), layer.end(), [&](int a, int b) {
                    return std::make_pair(band(a), graph.nodes[a].order) < std::make_pair(band(b), graph.nodes[b].order);
                });

            // Across, in the column of each band: a node goes to the median position of its neighbours in the layer above,
            // then below (alternately), keeping the order and the gaps (least squares: an isotonic regression), so that
            // the edges between neighbouring layers are as straight as the order allows. Relative to the column.
            std::vector<float> center(n, 0.f), columnMin(freeBand + 1, 0.f);
            std::vector<float> ownWidth(freeBand + 1, 0.f);  // the column of a band's own nodes
            for (int b = 0; b <= freeBand; ++b)
            {
                std::vector<std::vector<int>> rows;  // the column's nodes, layer by layer, in their order
                for (auto& [r, layer] : layers)
                {
                    std::vector<int> members = bandMembers(layer, b);
                    if (!members.empty())
                        rows.push_back(members);
                }
                if (rows.empty())
                    continue;
                for (const std::vector<int>& row : rows)  // to start: packed, centered on 0
                {
                    float x = -membersWidth(row) / 2.f;
                    for (int v : row)
                        center[v] = x + cross(graph.nodes[v]) / 2.f, x += cross(graph.nodes[v]) + gapX;
                }
                for (int pass = 0; pass < 4; ++pass)
                {
                    const bool down = pass % 2 == 0;
                    for (size_t k = 0; k < rows.size(); ++k)
                    {
                        const std::vector<int>& row = rows[down ? k : rows.size() - 1 - k];
                        std::vector<float> target(row.size()), offset(row.size(), 0.f);
                        for (size_t i = 0; i < row.size(); ++i)
                        {
                            int v = row[i];
                            if (i > 0)  // the least distance between the centers of two neighbours
                                offset[i] = offset[i - 1] + (cross(graph.nodes[row[i - 1]]) + cross(graph.nodes[v])) / 2.f + gapX;
                            std::vector<float> xs;
                            for (int w : down ? forwardPreds[v] : forwardSuccs[v])
                                if (band(w) == b && std::abs(graph.nodes[w].rank - graph.nodes[v].rank) == 1)
                                    xs.push_back(center[w]);
                            float desired = center[v];
                            if (!xs.empty())
                            {
                                std::sort(xs.begin(), xs.end());
                                desired = (xs[(xs.size() - 1) / 2] + xs[xs.size() / 2]) / 2.f;  // the median
                            }
                            target[i] = desired - offset[i];
                        }
                        std::vector<float> fitted = Isotonic(target);
                        for (size_t i = 0; i < row.size(); ++i)
                            center[row[i]] = fitted[i] + offset[i];
                    }
                }
                float lo = FLT_MAX, hi = -FLT_MAX;
                for (const std::vector<int>& row : rows)
                    for (int v : row)
                        lo = std::min(lo, center[v] - cross(graph.nodes[v]) / 2.f), hi = std::max(hi, center[v] + cross(graph.nodes[v]) / 2.f);
                columnMin[b] = lo;
                ownWidth[b] = hi - lo;
            }
            std::vector<std::vector<int>> children(freeBand + 1);
            for (int b = 0; b < freeBand; ++b)
                children[graph.subgraphs[b].parent >= 0 ? graph.subgraphs[b].parent : freeBand].push_back(b);
            // widths: the children first (they come after their parent in the source)
            auto columnsWidth = [&](int b) {
                float w = 0.f;
                int columns = 0;
                for (int c : children[b])
                    if (bandWidth[c] > 0.f)
                        w += bandWidth[c], ++columns;
                if (ownWidth[b] > 0.f)
                    w += ownWidth[b], ++columns;
                return w + gapX * (float)std::max(columns - 1, 0);
            };
            for (int k = freeBand - 1; k >= -1; --k)  // the children before their parents, the root last
            {
                const int b = k >= 0 ? k : freeBand;
                float w = columnsWidth(b);
                if (b != freeBand && w > 0.f)  // room for the box, and for its title when the cross axis is vertical (LR)
                {
                    w += 2.f * boxPad + (vertical ? 0.f : titleHeight);
                    if (vertical)
                        w = std::max(w, TitleWidth(graph.subgraphs[b], em));
                }
                bandWidth[b] = w;
            }
            // starts: the parents first; in a band, its columns centered in the room inside the box
            std::vector<float> bandStart(freeBand + 1, 0.f), ownStart(freeBand + 1, 0.f);
            std::function<void(int)> place = [&](int b) {
                float inside = b == freeBand ? 0.f : boxPad + (vertical ? 0.f : titleHeight);
                float room = bandWidth[b] - (b == freeBand ? 0.f : 2.f * boxPad + (vertical ? 0.f : titleHeight));
                float x = bandStart[b] + inside + (room - columnsWidth(b)) / 2.f;
                for (int c : children[b])
                    if (bandWidth[c] > 0.f)
                    {
                        bandStart[c] = x;
                        place(c);
                        x += bandWidth[c] + gapX;
                    }
                ownStart[b] = x;
            };
            place(freeBand);
            const float totalCross = bandWidth[freeBand];

            // The gap after each layer: wide enough for the label of each edge that crosses it (in its middle), and for
            // the markers and cardinalities at its ends (class diagrams)
            std::map<int, float> gapAfter;
            auto extent = [&](const std::string& text) {
                ImVec2 ts = ImGui::CalcTextSize(text.c_str());
                return vertical ? ts.y : ts.x + 4.f;
            };
            for (size_t i = 0; i < graph.edges.size(); ++i)
            {
                const Edge& e = graph.edges[i];
                if (UsesLane(graph, e))
                    continue;
                float endRoom = 0.f;  // the room at each end, the same at both so that the label stays in the middle
                if (relations)
                {
                    const Relation& r = (*relations)[i];
                    for (const std::string* card : {&r.cardinalitySrc, &r.cardinalityDst})
                        endRoom = std::max(endRoom, card->empty() ? 0.f : 1.3f * em + extent(*card) / 2.f + 0.3f * em);
                    for (Marker m : {r.markerSrc, r.markerDst})
                        endRoom = std::max(endRoom, m == Marker::None ? 0.f : 1.2f * em);
                }
                if (e.label.empty() && endRoom == 0.f)
                    continue;
                float needed = (e.label.empty() ? 0.f : extent(e.label)) + 2.f * endRoom + 1.5f * em;
                gapAfter[graph.nodes[e.src].rank] = std::max(gapAfter[graph.nodes[e.src].rank], needed);
            }

            // Along the main axis, a subgraph's box adds its padding before its first layer and after its last one, and
            // its title before its first layer (TD), or after its last one (BT: mirrored afterwards)
            // (nested boxes that start, or end, on the same layer: their rooms add up)
            std::vector<int> first(freeBand, INT_MAX), last(freeBand, INT_MIN);  // the layers of each subgraph, its nested ones included
            for (const Node& nd : graph.nodes)
                for (int b = nd.subgraph; b >= 0; b = graph.subgraphs[b].parent)
                    first[b] = std::min(first[b], nd.rank), last[b] = std::max(last[b], nd.rank);
            std::map<int, float> before, after;
            const float title = vertical ? titleHeight : 0.f;
            for (const Node& nd : graph.nodes)
            {
                float roomBefore = 0.f, roomAfter = 0.f;
                for (int b = nd.subgraph; b >= 0; b = graph.subgraphs[b].parent)
                {
                    if (first[b] == nd.rank)
                        roomBefore += boxPad + (graph.reversed ? 0.f : title);
                    if (last[b] == nd.rank)
                        roomAfter += boxPad + (graph.reversed ? title : 0.f);
                }
                before[nd.rank] = std::max(before[nd.rank], roomBefore);
                after[nd.rank] = std::max(after[nd.rank], roomAfter);
            }

            // Positions across: in the column of their band
            for (int v = 0; v < n; ++v)
            {
                Node& nd = graph.nodes[v];
                (vertical ? nd.pos.x : nd.pos.y) = ownStart[band(v)] + center[v] - columnMin[band(v)] - cross(nd) / 2.f;
            }
            // Lane edges: their lanes, the ones that span fewer layers inside (nested like brackets), and their approach
            // lines after the layer of their source and before the layer of their target. So that the lane routes do not
            // cross: from the layer outward, the back edges (their lanes go the other way), the inner lanes first, then
            // the edges that skip layers, the outer lanes first
            std::vector<int> laneEdges;
            for (size_t i = 0; i < graph.edges.size(); ++i)
            {
                Edge& e = graph.edges[i];
                e.lane = e.exitTrack = e.entryTrack = 0;
                if (UsesLane(graph, e))
                    laneEdges.push_back((int)i);
            }
            auto span = [&](int i) { return std::abs(graph.nodes[graph.edges[i].src].rank - graph.nodes[graph.edges[i].dst].rank); };
            std::stable_sort(laneEdges.begin(), laneEdges.end(), [&](int a, int b) { return span(a) < span(b); });
            graph.lanes = 0;
            for (int i : laneEdges)
                graph.edges[i].lane = ++graph.lanes;
            auto isBack = [&](int i) { return graph.backEdges.count({graph.edges[i].src, graph.edges[i].dst}) > 0; };
            std::stable_sort(laneEdges.begin(), laneEdges.end(), [&](int a, int b) {
                if (isBack(a) != isBack(b))
                    return isBack(a);
                return isBack(a) ? graph.edges[a].lane < graph.edges[b].lane : graph.edges[a].lane > graph.edges[b].lane;
            });
            std::map<int, int> exitCount, entryCount;
            for (int i : laneEdges)
            {
                Edge& e = graph.edges[i];
                e.exitTrack = exitCount[graph.nodes[e.src].rank]++;
                e.entryTrack = entryCount[graph.nodes[e.dst].rank]++;
            }
            ComputeSubgraphBoxes(graph, boxPad, titleHeight, em);  // across only (for the ends of the edges on boxes)
            std::map<int, float> trackRoom = AssignTracks(graph, em);
            const float step = 0.45f * em;
            auto extraLines = [&](std::map<int, int>& count, int r) { return (float)std::max(count[r] - 1, 0) * step; };

            // Positions along: the layers one after the other. Between two layers, the gap leaves the boxes' paddings
            // and titles, and a channel in the middle of the free space, wide enough for its tracks, where the edges run
            graph.channels.clear();
            float offsetMain = layers.empty() ? 0.f : before[layers.begin()->first], previousEnd = 0.f;
            int previous = -1;
            for (auto& [r, layer] : layers)
            {
                if (previous >= 0)
                {
                    float tracks = trackRoom[previous] + 1.2f * em;
                    float free = std::max({1.5f * em, gapAfter[previous], tracks});
                    float exits = extraLines(exitCount, previous), entries = extraLines(entryCount, r);  // lane approach lines
                    float gap = std::max({gapY, gapAfter[previous], tracks, after[previous] + before[r] + free}) + exits + entries;
                    offsetMain = previousEnd + gap;
                    graph.channels[previous] = (previousEnd + after[previous] + exits + offsetMain - before[r] - entries) / 2.f;
                }
                float thickness = 0.f;
                for (int v : layer)
                    thickness = std::max(thickness, main(graph.nodes[v]));
                for (int v : layer)
                {
                    Node& nd = graph.nodes[v];
                    (vertical ? nd.pos.y : nd.pos.x) = offsetMain + (thickness - main(nd)) / 2.f;
                }
                previousEnd = offsetMain + thickness;
                previous = r;
            }
            const float mainEnd = previousEnd + (previous >= 0 ? after[previous] : 0.f);
            ComputeSubgraphBoxes(graph, boxPad, titleHeight, em);

            // Everything shifted along the main axis: room for the back edges that come back in front of the first layer
            float shift = !graph.backEdges.empty() ? 1.5f * em + (layers.empty() ? 0.f : extraLines(entryCount, layers.begin()->first)) : 0.f;
            ImVec2 delta = vertical ? ImVec2(0.f, shift) : ImVec2(shift, 0.f);
            for (Node& nd : graph.nodes)
                nd.pos = ImVec2(nd.pos.x + delta.x, nd.pos.y + delta.y);
            for (auto& [r, channel] : graph.channels)
                channel += shift;
            for (Subgraph& sub : graph.subgraphs)
            {
                sub.boxMin = ImVec2(sub.boxMin.x + delta.x, sub.boxMin.y + delta.y);
                sub.boxMax = ImVec2(sub.boxMax.x + delta.x, sub.boxMax.y + delta.y);
            }
            const float lanes = 1.2f * em * (float)graph.lanes;  // room on the right (TD) or below (LR) for the lane edges
            const float mainTotal = mainEnd + shift;
            graph.size = vertical ? ImVec2(totalCross + lanes, mainTotal) : ImVec2(mainTotal, totalCross + lanes);
            RouteEdges(graph, em);
            if (graph.reversed)
                Mirror(graph, boxPad, titleHeight);
        }

        // Whether the segment [p, q] goes through the rect [r0, r1] (Liang-Barsky clipping)
        bool SegmentThroughRect(ImVec2 p, ImVec2 q, ImVec2 r0, ImVec2 r1)
        {
            float t0 = 0.f, t1 = 1.f, dx = q.x - p.x, dy = q.y - p.y;
            const float pp[4] = {-dx, dx, -dy, dy};
            const float qq[4] = {p.x - r0.x, r1.x - p.x, p.y - r0.y, r1.y - p.y};
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

        // A title where no edge crosses it: on the left of its box, else on the right, else in the middle
        void PlaceTitles(Graph& graph, float em)
        {
            for (Subgraph& sub : graph.subgraphs)
            {
                if (!sub.hasBox)
                    continue;
                ImVec2 ts = ImGui::CalcTextSize(sub.title.c_str());
                float width = sub.boxMax.x - sub.boxMin.x;
                const float candidates[3] = {0.5f * em, width - ts.x - 0.5f * em, (width - ts.x) / 2.f};
                int bestCrossings = INT_MAX;
                for (float x : candidates)
                {
                    ImVec2 t0(sub.boxMin.x + x, sub.boxMin.y + 0.2f * em), t1(t0.x + ts.x, t0.y + ts.y);
                    int crossings = 0;
                    for (const Edge& e : graph.edges)
                        for (size_t k = 0; k + 1 < e.points.size(); ++k)
                            crossings += SegmentThroughRect(e.points[k], e.points[k + 1], t0, t1) ? 1 : 0;
                    if (crossings < bestCrossings)
                        bestCrossings = crossings, sub.titleX = x;
                }
            }
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

        // Flowcharts, as Mermaid lays them out (its dagre wrapper): a subgraph that no edge links to the outside (an end
        // on the subgraph itself does not count) is laid out on its own, in its `direction`, else in the flipped
        // direction of the layout around it (TB -> LR, the others -> TB). In the layout around it, it is a node.
        void LayoutFlowchart(Graph& graph, float em, float gapY)
        {
            const int nSub = (int)graph.subgraphs.size();
            auto within = [&](int b, int s) {  // b is s, or inside it
                for (; b >= 0; b = graph.subgraphs[b].parent)
                    if (b == s)
                        return true;
                return false;
            };
            std::vector<bool> own(nSub, false);
            for (int s = 0; s < nSub; ++s)
            {
                auto inside = [&](int node, int box) { return box >= 0 ? box != s && within(box, s) : within(graph.nodes[node].subgraph, s); };
                bool members = false, linked = false;
                for (const Node& nd : graph.nodes)
                    members = members || within(nd.subgraph, s);
                for (const Edge& e : graph.edges)
                    linked = linked || inside(e.src, e.srcBox) != inside(e.dst, e.dstBox);
                own[s] = members && !linked;
            }
            if (std::find(own.begin(), own.end(), true) == own.end())
            {
                LayoutGraph(graph, em, gapY, nullptr);
                return;
            }

            // The scope of a subgraph or a node: the innermost subgraph laid out on its own around it, -1 for the root
            auto scopeOfBox = [&](int b) {
                for (b = graph.subgraphs[b].parent; b >= 0 && !own[b]; b = graph.subgraphs[b].parent) {}
                return b;
            };
            auto scopeOfNode = [&](int v) {
                int b = graph.nodes[v].subgraph;
                for (; b >= 0 && !own[b]; b = graph.subgraphs[b].parent) {}
                return b;
            };
            auto inScope = [&](int scope, int c) {  // scope is c, or a scope inside c
                for (; scope >= 0; scope = scopeOfBox(scope))
                    if (scope == c)
                        return true;
                return false;
            };
            auto firstMember = [&](int c) {
                int v = 0;
                while (!within(graph.nodes[v].subgraph, c))
                    ++v;
                return v;
            };
            // What stands for an edge's end in the layout of the scope s: the node, a box, or the node of a subgraph laid
            // out on its own (inner: the end is inside it, the edge is laid out there); or nothing (outside the scope)
            struct End { int node = -1, box = -1, compound = -1; bool inner = false, outside = false; };
            const float boxPad = BoxPadding(em), titleHeight = TitleRoom(em);
            graph.backEdges.clear();
            std::vector<int> edgeScope(graph.edges.size(), -1);

            // Returns the size of what the scope draws; its content is placed from (0, 0), the root's as LayoutGraph does
            std::function<ImVec2(int, bool, bool)> layoutScope = [&](int s, bool vertical, bool reversed) {
                // the subgraphs laid out on their own directly inside, first: their size is the size of their node here
                std::map<int, ImVec2> inner;
                for (int c = 0; c < nSub; ++c)
                    if (own[c] && scopeOfBox(c) == s)
                    {
                        const Subgraph& sub = graph.subgraphs[c];
                        const bool subVertical = sub.hasDirection ? sub.vertical : !(vertical && !reversed);
                        const bool subReversed = sub.hasDirection && sub.reversed;
                        inner[c] = layoutScope(c, subVertical, subReversed);
                    }
                auto end = [&](int node, int box) {
                    End r;
                    int c;
                    if (box >= 0)
                    {
                        if (box == s)
                            return r.outside = true, r;
                        if (scopeOfBox(box) == s)
                            return (own[box] ? r.compound : r.box) = box, r;
                        c = scopeOfBox(box);
                    }
                    else
                    {
                        c = scopeOfNode(node);
                        if (c == s)
                            return r.node = node, r;
                    }
                    while (c >= 0 && scopeOfBox(c) != s)
                        c = scopeOfBox(c);
                    if (c < 0)
                        r.outside = true;
                    else
                        r.compound = c, r.inner = true;
                    return r;
                };

                Graph reduced;
                reduced.vertical = vertical, reduced.reversed = reversed;
                std::vector<int> subIndex(nSub, -1), nodeIndex(graph.nodes.size(), -1), compoundIndex(nSub, -1);
                std::vector<int> fullNode, fullCompound;  // per node of the reduced graph: the node, or the subgraph, it stands for
                auto parentIn = [&](int p) { return p < 0 || p == s ? -1 : subIndex[p]; };
                for (int b = 0; b < nSub; ++b)
                    if (!own[b] && scopeOfBox(b) == s)
                    {
                        Subgraph sub;
                        sub.id = graph.subgraphs[b].id, sub.title = graph.subgraphs[b].title;
                        sub.parent = parentIn(graph.subgraphs[b].parent);
                        subIndex[b] = (int)reduced.subgraphs.size();
                        reduced.subgraphs.push_back(sub);
                    }
                for (int v = 0; v < (int)graph.nodes.size(); ++v)  // in source order, a subgraph where its first node is
                {
                    End e = end(v, -1);
                    if (e.node >= 0)
                    {
                        Node nd = graph.nodes[v];
                        nd.subgraph = parentIn(nd.subgraph);
                        nodeIndex[v] = (int)reduced.nodes.size();
                        reduced.nodes.push_back(nd), fullNode.push_back(v), fullCompound.push_back(-1);
                    }
                    else if (e.compound >= 0 && compoundIndex[e.compound] < 0)
                    {
                        const Subgraph& sub = graph.subgraphs[e.compound];
                        Node nd;
                        nd.id = sub.id;
                        nd.subgraph = parentIn(sub.parent);
                        nd.keepSize = true;
                        const ImVec2 content = inner[e.compound];
                        nd.size = ImVec2(std::max(content.x + 2.f * boxPad, TitleWidth(sub, em)), content.y + 2.f * boxPad + titleHeight);
                        compoundIndex[e.compound] = (int)reduced.nodes.size();
                        reduced.nodes.push_back(nd), fullNode.push_back(-1), fullCompound.push_back(e.compound);
                    }
                }
                std::vector<int> fullEdge;
                for (size_t i = 0; i < graph.edges.size(); ++i)
                {
                    const Edge& fe = graph.edges[i];
                    End a = end(fe.src, fe.srcBox), b = end(fe.dst, fe.dstBox);
                    if (a.outside || b.outside || a.inner || b.inner)
                        continue;  // the edge of another scope
                    Edge re = fe;
                    re.points.clear();
                    re.src = a.node >= 0 ? nodeIndex[a.node] : a.compound >= 0 ? compoundIndex[a.compound] : -1;
                    re.dst = b.node >= 0 ? nodeIndex[b.node] : b.compound >= 0 ? compoundIndex[b.compound] : -1;
                    re.srcBox = a.box >= 0 ? subIndex[a.box] : -1;
                    re.dstBox = b.box >= 0 ? subIndex[b.box] : -1;
                    fullEdge.push_back((int)i);
                    reduced.edges.push_back(re);
                }
                LayoutGraph(reduced, em, gapY, nullptr);

                // Back into the graph, from (0, 0) (the root: as it is)
                Extent extent;
                for (const Node& nd : reduced.nodes)
                    extent.Add(nd.pos, ImVec2(nd.pos.x + nd.size.x, nd.pos.y + nd.size.y));
                for (const Subgraph& sub : reduced.subgraphs)
                    if (sub.hasBox)
                        extent.Add(sub.boxMin, sub.boxMax);
                for (const Edge& e : reduced.edges)
                {
                    for (ImVec2 p : e.points)
                        extent.Add(p, p);
                    if (!e.label.empty())
                        extent.Add(e.labelMin, e.labelMax);
                }
                const ImVec2 o = s < 0 ? ImVec2(0.f, 0.f) : extent.min;
                auto moved = [&](ImVec2 p) { return ImVec2(p.x - o.x, p.y - o.y); };
                const int rankBase = 1000 * (s + 1);  // the ranks of different scopes do not compare
                for (size_t k = 0; k < reduced.nodes.size(); ++k)
                {
                    const Node& rn = reduced.nodes[k];
                    if (fullNode[k] >= 0)
                    {
                        Node& nd = graph.nodes[fullNode[k]];
                        nd.pos = moved(rn.pos), nd.size = rn.size, nd.rank = rankBase + rn.rank, nd.order = rn.order;
                        continue;
                    }
                    // a subgraph laid out on its own: its box is the node, its content goes inside, under the title
                    const int c = fullCompound[k];
                    Subgraph& sub = graph.subgraphs[c];
                    sub.hasBox = true;
                    sub.boxMin = moved(rn.pos);
                    sub.boxMax = ImVec2(sub.boxMin.x + rn.size.x, sub.boxMin.y + rn.size.y);
                    const ImVec2 d(sub.boxMin.x + (rn.size.x - inner[c].x) / 2.f, sub.boxMin.y + boxPad + titleHeight);
                    auto move = [&](ImVec2& p) { p = ImVec2(p.x + d.x, p.y + d.y); };
                    for (Node& nd : graph.nodes)
                        if (within(nd.subgraph, c))
                            move(nd.pos);
                    for (int b = 0; b < nSub; ++b)
                        if (b != c && within(b, c))
                            move(graph.subgraphs[b].boxMin), move(graph.subgraphs[b].boxMax);
                    for (size_t i = 0; i < graph.edges.size(); ++i)
                        if (edgeScope[i] >= 0 && inScope(edgeScope[i], c))
                        {
                            for (ImVec2& p : graph.edges[i].points)
                                move(p);
                            move(graph.edges[i].labelMin), move(graph.edges[i].labelMax);
                        }
                }
                for (int b = 0; b < nSub; ++b)
                    if (subIndex[b] >= 0)
                    {
                        const Subgraph& rs = reduced.subgraphs[subIndex[b]];
                        graph.subgraphs[b].hasBox = rs.hasBox;
                        graph.subgraphs[b].boxMin = moved(rs.boxMin), graph.subgraphs[b].boxMax = moved(rs.boxMax);
                    }
                auto nodeOf = [&](int k) { return fullNode[k] >= 0 ? fullNode[k] : firstMember(fullCompound[k]); };
                for (size_t k = 0; k < reduced.edges.size(); ++k)
                {
                    const Edge& re = reduced.edges[k];
                    Edge& fe = graph.edges[fullEdge[k]];
                    fe.points.clear();
                    for (ImVec2 p : re.points)
                        fe.points.push_back(moved(p));
                    fe.labelMin = moved(re.labelMin), fe.labelMax = moved(re.labelMax);
                    fe.track = re.track, fe.lane = re.lane, fe.exitTrack = re.exitTrack, fe.entryTrack = re.entryTrack;
                    fe.src = nodeOf(re.src), fe.dst = nodeOf(re.dst);
                    if (reduced.backEdges.count({re.src, re.dst}))
                        graph.backEdges.insert({fe.src, fe.dst});
                    edgeScope[fullEdge[k]] = s;
                }
                if (s < 0)
                    return graph.size = reduced.size;
                return ImVec2(extent.max.x - extent.min.x, extent.max.y - extent.min.y);
            };
            layoutScope(-1, graph.vertical, graph.reversed);
        }

        // The rows go down one after the other, each as tall as its text; a frame adds a header band where it starts
        // (and where each of its sections starts), and a margin where it ends
        void LayoutSequence(Sequence& seq, float em)
        {
            const int n = (int)seq.participantIds.size();
            const float lineHeight = ImGui::GetTextLineHeight();
            const float half = 0.35f * em;                    // half the width of an activation box
            const float headerBand = lineHeight + 0.6f * em;  // the header of a frame, or of a section
            bool anyActor = false;
            for (bool actor : seq.participantIsActor)
                anyActor = anyActor || actor;
            seq.boxHeight = lineHeight + (anyActor ? 2.6f : 1.f) * em;
            seq.boxWidth.assign(n, 0.f);
            for (int i = 0; i < n; ++i)
                seq.boxWidth[i] = std::max(ImGui::CalcTextSize(seq.participantLabels[i].c_str()).x + 2.f * em, 3.f * em);

            // The distance between two neighbours: room for their boxes, for the messages between them, for the notes
            // beside them
            std::vector<float> distance(n > 0 ? n - 1 : 0);
            for (int i = 0; i + 1 < n; ++i)
                distance[i] = seq.boxWidth[i] / 2.f + 3.f * em + seq.boxWidth[i + 1] / 2.f;
            for (const SequenceRow& r : seq.rows)
            {
                float textWidth = ImGui::CalcTextSize(r.text.c_str()).x;
                if (!r.isNote && std::abs(r.src - r.dst) == 1)
                {
                    int i = std::min(r.src, r.dst);
                    distance[i] = std::max(distance[i], textWidth + 2.f * em + (r.number > 0 ? 1.4f * em : 0.f));
                }
                if (r.isNote && r.notePlacement == 1 && r.over[0] + 1 < n)
                    distance[r.over[0]] = std::max(distance[r.over[0]], textWidth + 2.2f * em);
                if (r.isNote && r.notePlacement == -1 && r.over[0] > 0)
                    distance[r.over[0] - 1] = std::max(distance[r.over[0] - 1], textWidth + 2.2f * em);
            }
            seq.xCenter.assign(n, 0.f);
            if (n > 0)
                seq.xCenter[0] = seq.boxWidth[0] / 2.f;
            for (int i = 1; i < n; ++i)
                seq.xCenter[i] = seq.xCenter[i - 1] + distance[i - 1];
            seq.totalWidth = n > 0 ? seq.xCenter[n - 1] + seq.boxWidth[n - 1] / 2.f : 0.f;

            // Rows and frames, from top to bottom
            const int rowCount = (int)seq.rows.size();
            auto isEmpty = [](const SequenceFrame& f) { return f.last < f.first; };
            float cursor = seq.boxHeight + 0.5f * em;
            for (SequenceFrame& f : seq.frames)
                f.sectionY.clear();
            auto boundary = [&](int k) {  // between the rows k - 1 and k
                for (int fi = (int)seq.frames.size() - 1; fi >= 0; --fi)  // the frames that end, the inner ones first
                {
                    SequenceFrame& f = seq.frames[fi];
                    if (!isEmpty(f) && f.last == k - 1)
                        cursor += 0.4f * em, f.frameMax.y = cursor;
                }
                for (SequenceFrame& f : seq.frames)  // the sections that start (before the frames opened in them)
                    for (const auto& [row, label] : f.sections)
                        if (row == k)
                            f.sectionY.push_back(cursor), cursor += headerBand;
                for (SequenceFrame& f : seq.frames)  // the frames that start, the outer ones first
                {
                    if (f.first != k)
                        continue;
                    f.frameMin.y = cursor;
                    cursor += f.kind == "rect" ? 0.3f * em : headerBand;
                    if (isEmpty(f))
                        cursor += 0.4f * em, f.frameMax.y = cursor;
                }
            };
            for (int k = 0; k < rowCount; ++k)
            {
                boundary(k);
                SequenceRow& r = seq.rows[k];
                ImVec2 ts = ImGui::CalcTextSize(r.text.c_str());
                if (r.isNote)
                {
                    float top = cursor + 0.2f * em, h = ts.y + 0.6f * em;
                    r.y = top + h / 2.f;
                    r.boxMin.y = top, r.boxMax.y = top + h;
                    cursor = top + h + 0.5f * em;
                }
                else if (r.src == r.dst)  // a hook, with the text beside it
                {
                    float extent = std::max(0.5f * em, ts.y / 2.f);
                    r.y = cursor + 0.3f * em + extent;
                    cursor = r.y + extent + 0.7f * em;
                }
                else  // the text above the line
                {
                    r.y = cursor + 0.3f * em + ts.y + 0.2f * em;
                    cursor = r.y + 0.7f * em;
                }
            }
            boundary(rowCount);
            seq.height = cursor + 0.5f * em + seq.boxHeight;

            // Activations: a box on the lifeline, from the message that starts it to the one that ends it; the nested
            // ones shifted to the right
            auto rowY = [&](int row) { return row >= 0 && row < rowCount ? seq.rows[row].y : seq.boxHeight + 0.5f * em; };
            for (size_t ai = 0; ai < seq.activations.size(); ++ai)
            {
                SequenceActivation& a = seq.activations[ai];
                float y0 = rowY(a.startRow), y1 = std::max(rowY(a.endRow), y0 + 0.6f * em);
                int depth = 0;
                for (size_t bi = 0; bi < ai; ++bi)
                {
                    const SequenceActivation& b = seq.activations[bi];
                    if (b.participant == a.participant && b.boxMin.y <= y0 && y0 < b.boxMax.y)
                        ++depth;
                }
                float x0 = seq.xCenter[a.participant] - half + (float)depth * half;
                a.boxMin = ImVec2(x0, y0);
                a.boxMax = ImVec2(x0 + 2.f * half, y1);
            }
            auto activeDepth = [&](int participant, float y) {
                int depth = 0;
                for (const SequenceActivation& a : seq.activations)
                    if (a.participant == participant && a.boxMin.y - 0.01f <= y && y <= a.boxMax.y + 0.01f)
                        ++depth;
                return depth;
            };

            // Messages and notes, across
            for (SequenceRow& r : seq.rows)
            {
                ImVec2 ts = ImGui::CalcTextSize(r.text.c_str());
                if (r.isNote)
                {
                    float w = ts.x + 1.f * em;
                    float x0, x1;
                    if (r.notePlacement == 1)
                        x0 = seq.xCenter[r.over[0]] + 0.6f * em, x1 = x0 + w;
                    else if (r.notePlacement == -1)
                        x1 = seq.xCenter[r.over[0]] - 0.6f * em, x0 = x1 - w;
                    else
                    {
                        float xMin = FLT_MAX, xMax = -FLT_MAX;
                        for (int v : r.over)
                            xMin = std::min(xMin, seq.xCenter[v]), xMax = std::max(xMax, seq.xCenter[v]);
                        float cx = (xMin + xMax) / 2.f, halfWidth = std::max((xMax - xMin) / 2.f + em, w / 2.f);
                        x0 = cx - halfWidth, x1 = cx + halfWidth;
                    }
                    r.boxMin.x = x0, r.boxMax.x = x1;
                    r.textMin = ImVec2((x0 + x1) / 2.f - ts.x / 2.f, r.y - ts.y / 2.f);
                }
                else
                {
                    int ds = activeDepth(r.src, r.y), dd = activeDepth(r.dst, r.y);
                    float xs = seq.xCenter[r.src], xd = seq.xCenter[r.dst];
                    if (r.src == r.dst)
                    {
                        r.xStart = r.xEnd = xs + (float)ds * half;
                        r.textMin = ImVec2(r.xStart + 1.9f * em, r.y - ts.y / 2.f);
                    }
                    else
                    {
                        bool right = xd > xs;
                        r.xStart = right ? xs + (float)ds * half : xs - (ds > 0 ? half : 0.f);
                        r.xEnd = right ? xd - (dd > 0 ? half : 0.f) : xd + (float)dd * half;
                        r.textMin = ImVec2((r.xStart + r.xEnd) / 2.f - ts.x / 2.f, r.y - 0.2f * em - ts.y);
                    }
                }
                r.textMax = ImVec2(r.textMin.x + ts.x, r.textMin.y + ts.y);
            }

            // Frames, across: the participants and the texts of their rows, wider for each level of frame inside
            std::vector<int> innerLevels(seq.frames.size(), 0);
            auto inside = [&](const SequenceFrame& inner, const SequenceFrame& outer, size_t innerIndex, size_t outerIndex) {
                return innerIndex > outerIndex && inner.first >= outer.first && inner.last <= outer.last;
            };
            for (size_t fi = seq.frames.size(); fi-- > 0;)
                for (size_t gi = fi + 1; gi < seq.frames.size(); ++gi)
                    if (inside(seq.frames[gi], seq.frames[fi], gi, fi))
                        innerLevels[fi] = std::max(innerLevels[fi], innerLevels[gi] + 1);
            for (size_t fi = 0; fi < seq.frames.size(); ++fi)
            {
                SequenceFrame& f = seq.frames[fi];
                f.depth = 0;
                for (size_t gi = 0; gi < fi; ++gi)
                    if (inside(f, seq.frames[gi], fi, gi))
                        ++f.depth;
                float x0 = FLT_MAX, x1 = -FLT_MAX;
                for (int k = f.first; k <= f.last && k < rowCount; ++k)
                {
                    const SequenceRow& r = seq.rows[k];
                    std::vector<int> involved = r.isNote ? r.over : std::vector<int>{r.src, r.dst};
                    for (int v : involved)
                        x0 = std::min(x0, seq.xCenter[v] - seq.boxWidth[v] / 2.f), x1 = std::max(x1, seq.xCenter[v] + seq.boxWidth[v] / 2.f);
                    x0 = std::min(x0, std::min(r.textMin.x, r.isNote ? r.boxMin.x : r.textMin.x) - 0.3f * em);
                    x1 = std::max(x1, std::max(r.textMax.x, r.isNote ? r.boxMax.x : r.textMax.x) + 0.3f * em);
                }
                if (x0 > x1)  // no row: across the diagram
                    x0 = 0.f, x1 = seq.totalWidth;
                float grow = 0.5f * em + 0.4f * em * (float)innerLevels[fi];
                f.frameMin.x = x0 - grow;
                f.frameMax.x = x1 + grow;
                // the header's width: the kind's tab and the label
                if (f.kind != "rect")
                {
                    float header = ImGui::CalcTextSize(f.kind.c_str()).x + 2.f * em + ImGui::CalcTextSize(("[" + f.label + "]").c_str()).x;
                    f.frameMax.x = std::max(f.frameMax.x, f.frameMin.x + header);
                }
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
                if (r.number > 0)
                    extent.Add(ImVec2(r.xStart - 0.7f * em, r.y - 0.7f * em), ImVec2(r.xStart + 0.7f * em, r.y + 0.7f * em));
            }
            for (const SequenceFrame& f : seq.frames)
                extent.Add(f.frameMin, f.frameMax);
            ImVec2 size(seq.totalWidth, seq.height);
            ImVec2 d = FitDelta(extent, &size, em);
            seq.totalWidth = size.x;
            if (d.x == 0.f)
                return;
            for (float& xc : seq.xCenter)
                xc += d.x;
            for (SequenceRow& r : seq.rows)
                r.textMin.x += d.x, r.textMax.x += d.x, r.boxMin.x += d.x, r.boxMax.x += d.x, r.xStart += d.x, r.xEnd += d.x;
            for (SequenceFrame& f : seq.frames)
                f.frameMin.x += d.x, f.frameMax.x += d.x;
            for (SequenceActivation& a : seq.activations)
                a.boxMin.x += d.x, a.boxMax.x += d.x;
        }
    }

    void Layout(Diagram& diagram)
    {
        float em = ImGui::GetFontSize();
        if (diagram.kind == DiagramKind::Sequence)
            LayoutSequence(diagram.sequence, em);
        else
        {
            // class diagrams: room for the UML markers at the ends of the lines
            bool isClass = diagram.kind == DiagramKind::Class;
            if (isClass)
                LayoutGraph(diagram.graph, em, 3.3f * em, &diagram.relations);
            else
                LayoutFlowchart(diagram.graph, em, 2.5f * em);
            if (diagram.kind == DiagramKind::Class)
                PlaceCardinalities(diagram.graph, diagram.relations, em);
            PlaceTitles(diagram.graph, em);
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
