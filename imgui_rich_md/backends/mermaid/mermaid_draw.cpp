// Part of ImGui Bundle - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_bundle
// Mermaid: the drawing, with ImDrawList. Colors come from the ImGui style.
#include "rich_md_mermaid.h"

#include <algorithm>
#include <cmath>

namespace RichMd::Mermaid
{
    namespace
    {
        // Dear ImGui 1.92.8 swapped the thickness and flags arguments of AddRect and AddPolyline
        void StrokeRect(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col, float rounding, float thickness)
        {
#if IMGUI_VERSION_NUM < 19276
            dl->AddRect(p0, p1, col, rounding, 0, thickness);
#else
            dl->AddRect(p0, p1, col, rounding, thickness);
#endif
        }

        void StrokeClosedPolyline(ImDrawList* dl, const ImVec2* points, int count, ImU32 col, float thickness)
        {
#if IMGUI_VERSION_NUM < 19276
            dl->AddPolyline(points, count, col, ImDrawFlags_Closed, thickness);
#else
            dl->AddPolyline(points, count, col, thickness, ImDrawFlags_Closed);
#endif
        }

        ImVec2 Add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
        float Length(float dx, float dy) { return std::max(std::sqrt(dx * dx + dy * dy), 1e-3f); }

        void Segment(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, LineStyle style, float em)
        {
            if (style != LineStyle::Dotted)
            {
                dl->AddLine(a, b, col, style == LineStyle::Thick ? 2.5f : 1.5f);
                return;
            }
            float dx = b.x - a.x, dy = b.y - a.y;
            float length = Length(dx, dy);
            float ux = dx / length, uy = dy / length;
            for (float d = 0.f; d < length; d += 0.7f * em)
            {
                float e = std::min(d + 0.35f * em, length);
                dl->AddLine(ImVec2(a.x + ux * d, a.y + uy * d), ImVec2(a.x + ux * e, a.y + uy * e), col, 1.5f);
            }
        }

        // The segments; with an arrow, the last segment stops at the base of the triangle
        void Polyline(ImDrawList* dl, std::vector<ImVec2> pts, ImU32 col, bool arrow, float em, LineStyle style)
        {
            if (arrow)
            {
                ImVec2 end = pts.back(), prev = pts[pts.size() - 2];
                float dx = end.x - prev.x, dy = end.y - prev.y;
                float n = Length(dx, dy);
                float ux = dx / n, uy = dy / n;
                float s = 0.55f * em;
                ImVec2 base(end.x - s * ux, end.y - s * uy);
                pts.back() = base;
                dl->AddTriangleFilled(end, ImVec2(base.x + s * 0.45f * uy, base.y - s * 0.45f * ux),
                                      ImVec2(base.x - s * 0.45f * uy, base.y + s * 0.45f * ux), col);
            }
            for (size_t i = 0; i + 1 < pts.size(); ++i)
                Segment(dl, pts[i], pts[i + 1], col, style, em);
        }

        // The label's box, then its text
        void EdgeLabel(ImDrawList* dl, const Edge& e, ImVec2 origin, ImU32 textCol, ImU32 bg)
        {
            dl->AddRectFilled(Add(origin, e.labelMin), Add(origin, e.labelMax), bg);
            dl->AddText(ImVec2(origin.x + e.labelMin.x + 2.f, origin.y + e.labelMin.y), textCol, e.label.c_str());
        }

        std::vector<ImVec2> Translated(const std::vector<ImVec2>& points, ImVec2 origin)
        {
            std::vector<ImVec2> result;
            for (ImVec2 p : points)
                result.push_back(Add(origin, p));
            return result;
        }

        void SubgraphBoxes(ImDrawList* dl, const Graph& graph, ImVec2 origin, float em)
        {
            for (const Subgraph& sub : graph.subgraphs)
            {
                if (!sub.hasBox)
                    continue;
                ImVec2 q0 = Add(origin, sub.boxMin), q1 = Add(origin, sub.boxMax);
                dl->AddRectFilled(q0, q1, ImGui::GetColorU32(ImGuiCol_Text, 0.04f), 0.3f * em);
                StrokeRect(dl, q0, q1, ImGui::GetColorU32(ImGuiCol_Text, 0.35f), 0.3f * em, 1.f);
                dl->AddText(ImVec2(q0.x + 0.5f * em, q0.y + 0.2f * em), ImGui::GetColorU32(ImGuiCol_Text, 0.8f), sub.title.c_str());
            }
        }

        void DrawFlowchart(const Graph& graph, ImVec2 origin, float em)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
            ImU32 border = ImGui::GetColorU32(ImGuiCol_Text, 0.6f);
            ImU32 fill = ImGui::GetColorU32(ImGuiCol_FrameBg);
            ImU32 windowBg = ImGui::GetColorU32(ImGuiCol_WindowBg);
            SubgraphBoxes(dl, graph, origin, em);
            for (const Edge& e : graph.edges)
            {
                Polyline(dl, Translated(e.points, origin), border, e.arrow, em, e.style);
                if (!e.label.empty())
                    EdgeLabel(dl, e, origin, textCol, windowBg);
            }
            for (const Node& node : graph.nodes)
            {
                ImVec2 p0 = Add(origin, node.pos), p1 = Add(p0, node.size);
                ImVec2 c((p0.x + p1.x) / 2.f, (p0.y + p1.y) / 2.f);
                if (node.shape == NodeShape::Diamond)
                {
                    ImVec2 pts[4] = {ImVec2(c.x, p0.y), ImVec2(p1.x, c.y), ImVec2(c.x, p1.y), ImVec2(p0.x, c.y)};
                    dl->AddConvexPolyFilled(pts, 4, fill);
                    StrokeClosedPolyline(dl, pts, 4, border, 1.5f);
                }
                else if (node.shape == NodeShape::Cylinder)
                {
                    float ry = 0.35f * em;
                    ImVec2 radius(node.size.x / 2.f, ry);
                    dl->AddRectFilled(ImVec2(p0.x, p0.y + ry), ImVec2(p1.x, p1.y - ry), fill);
                    dl->AddEllipseFilled(ImVec2(c.x, p1.y - ry), radius, fill);
                    dl->AddEllipseFilled(ImVec2(c.x, p0.y + ry), radius, fill);
                    dl->AddLine(ImVec2(p0.x, p0.y + ry), ImVec2(p0.x, p1.y - ry), border, 1.5f);
                    dl->AddLine(ImVec2(p1.x, p0.y + ry), ImVec2(p1.x, p1.y - ry), border, 1.5f);
                    dl->AddEllipse(ImVec2(c.x, p0.y + ry), radius, border, 0.f, 0, 1.5f);
                    dl->AddEllipse(ImVec2(c.x, p1.y - ry), radius, border, 0.f, 0, 1.5f);
                }
                else
                {
                    float rounding = node.shape == NodeShape::Rounded ? 0.6f * em : 0.15f * em;
                    dl->AddRectFilled(p0, p1, fill, rounding);
                    StrokeRect(dl, p0, p1, border, rounding, 1.5f);
                }
                ImVec2 ts = ImGui::CalcTextSize(node.label.c_str());
                dl->AddText(ImVec2(c.x - ts.x / 2.f, c.y - ts.y / 2.f), textCol, node.label.c_str());
            }
        }

        // A UML marker at `tip`, pointing away from `towards` (the next point of the line); returns the point where
        // the line stops
        ImVec2 DrawMarker(ImDrawList* dl, Marker kind, ImVec2 tip, ImVec2 towards, ImU32 col, ImU32 bg, float em)
        {
            float dx = towards.x - tip.x, dy = towards.y - tip.y;
            float n = Length(dx, dy);
            float ux = dx / n, uy = dy / n;  // from the tip along the line
            float px = -uy, py = ux;
            float s = 0.8f * em;
            if (kind == Marker::Triangle)
            {
                ImVec2 base(tip.x + s * ux, tip.y + s * uy);
                ImVec2 q1(base.x + s * 0.5f * px, base.y + s * 0.5f * py), q2(base.x - s * 0.5f * px, base.y - s * 0.5f * py);
                dl->AddTriangleFilled(tip, q1, q2, bg);
                dl->AddTriangle(tip, q1, q2, col, 1.5f);
                return base;
            }
            if (kind == Marker::Diamond || kind == Marker::DiamondFilled)
            {
                ImVec2 mid(tip.x + s * 0.6f * ux, tip.y + s * 0.6f * uy);
                ImVec2 end(tip.x + s * 1.2f * ux, tip.y + s * 1.2f * uy);
                ImVec2 pts[4] = {tip, ImVec2(mid.x + s * 0.35f * px, mid.y + s * 0.35f * py), end,
                                 ImVec2(mid.x - s * 0.35f * px, mid.y - s * 0.35f * py)};
                dl->AddConvexPolyFilled(pts, 4, kind == Marker::DiamondFilled ? col : bg);
                StrokeClosedPolyline(dl, pts, 4, col, 1.5f);
                return end;
            }
            if (kind == Marker::Arrow)
            {
                ImVec2 base(tip.x + s * 0.7f * ux, tip.y + s * 0.7f * uy);
                dl->AddLine(tip, ImVec2(base.x + s * 0.35f * px, base.y + s * 0.35f * py), col, 1.5f);
                dl->AddLine(tip, ImVec2(base.x - s * 0.35f * px, base.y - s * 0.35f * py), col, 1.5f);
            }
            return tip;
        }

        void DrawClass(const Graph& graph, const std::vector<Relation>& relations, ImVec2 origin, float em)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
            ImU32 border = ImGui::GetColorU32(ImGuiCol_Text, 0.6f);
            ImU32 fill = ImGui::GetColorU32(ImGuiCol_FrameBg);
            ImU32 windowBg = ImGui::GetColorU32(ImGuiCol_WindowBg);
            const float lineHeight = ImGui::GetTextLineHeight();
            SubgraphBoxes(dl, graph, origin, em);
            for (size_t i = 0; i < graph.edges.size(); ++i)
            {
                const Edge& e = graph.edges[i];
                const Relation& r = relations[i];
                std::vector<ImVec2> pts = Translated(e.points, origin);
                ImVec2 start = DrawMarker(dl, r.markerSrc, pts[0], pts[1], border, windowBg, em);
                ImVec2 end = DrawMarker(dl, r.markerDst, pts.back(), pts[pts.size() - 2], border, windowBg, em);
                pts.front() = start;
                pts.back() = end;
                Polyline(dl, pts, border, false, em, r.dashed ? LineStyle::Dotted : LineStyle::Solid);
                if (!e.label.empty())
                    EdgeLabel(dl, e, origin, textCol, windowBg);
                if (!r.cardinalitySrc.empty())
                    dl->AddText(Add(origin, r.cardinalitySrcPos), textCol, r.cardinalitySrc.c_str());
                if (!r.cardinalityDst.empty())
                    dl->AddText(Add(origin, r.cardinalityDstPos), textCol, r.cardinalityDst.c_str());
            }
            for (const Node& node : graph.nodes)
            {
                ImVec2 p0 = Add(origin, node.pos), p1 = Add(p0, node.size);
                dl->AddRectFilled(p0, p1, fill, 0.15f * em);
                StrokeRect(dl, p0, p1, border, 0.15f * em, 1.5f);
                float y = p0.y;
                for (size_t i = 0; i < node.compartments.size(); ++i)
                {
                    const auto& compartment = node.compartments[i];
                    if (i > 0)
                        dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), border, 1.f);
                    for (size_t j = 0; j < compartment.size(); ++j)
                    {
                        const std::string& text = compartment[j];
                        ImVec2 ts = ImGui::CalcTextSize(text.c_str());
                        float x = i == 0 ? (p0.x + p1.x) / 2.f - ts.x / 2.f : p0.x + 0.6f * em;  // the name is centered
                        dl->AddText(ImVec2(x, y + 0.3f * em + (float)j * lineHeight), textCol, text.c_str());
                    }
                    y += (float)compartment.size() * lineHeight + 0.6f * em;
                }
            }
        }

        void DrawSequence(const Sequence& seq, ImVec2 origin, float em)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
            ImU32 lineCol = ImGui::GetColorU32(ImGuiCol_Text, 0.6f);
            ImU32 fill = ImGui::GetColorU32(ImGuiCol_FrameBg);
            ImU32 noteFill = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
            const float boxH = seq.boxHeight;

            // Lifelines, and the participant boxes above and below
            for (size_t i = 0; i < seq.participantIds.size(); ++i)
            {
                float cx = origin.x + seq.xCenter[i];
                const std::string& label = seq.participantLabels[i];
                for (float y : {origin.y, origin.y + seq.height - boxH})
                {
                    ImVec2 p0(cx - seq.boxWidth[i] / 2.f, y), p1(cx + seq.boxWidth[i] / 2.f, y + boxH);
                    dl->AddRectFilled(p0, p1, fill, 0.2f * em);
                    StrokeRect(dl, p0, p1, lineCol, 0.2f * em, 1.5f);
                    ImVec2 ts = ImGui::CalcTextSize(label.c_str());
                    dl->AddText(ImVec2(cx - ts.x / 2.f, y + (boxH - ts.y) / 2.f), textCol, label.c_str());
                }
                float y0 = origin.y + boxH, y1 = origin.y + seq.height - boxH;
                for (float d = 0.f; d < y1 - y0; d += em)  // dashed
                    dl->AddLine(ImVec2(cx, y0 + d), ImVec2(cx, std::min(y0 + d + 0.5f * em, y1)), lineCol, 1.f);
            }
            // Loop frames, behind the rows
            for (const SequenceLoop& loop : seq.loops)
            {
                ImVec2 p0 = Add(origin, loop.frameMin), p1 = Add(origin, loop.frameMax);
                StrokeRect(dl, p0, p1, lineCol, 0.f, 1.f);
                std::string label = "loop [" + loop.label + "]";
                ImVec2 ts = ImGui::CalcTextSize(label.c_str());
                dl->AddRectFilled(p0, ImVec2(p0.x + ts.x + em, p0.y + ts.y + 0.3f * em), fill);
                dl->AddText(ImVec2(p0.x + 0.5f * em, p0.y + 0.15f * em), textCol, label.c_str());
            }
            // Rows: messages and notes
            for (size_t k = 0; k < seq.rows.size(); ++k)
            {
                const SequenceRow& r = seq.rows[k];
                float y = origin.y + r.y;
                ImVec2 textPos = Add(origin, r.textMin);
                if (r.isNote)
                {
                    ImVec2 p0 = Add(origin, r.boxMin), p1 = Add(origin, r.boxMax);
                    dl->AddRectFilled(p0, p1, noteFill, 0.1f * em);
                    StrokeRect(dl, p0, p1, lineCol, 0.1f * em, 1.f);
                    dl->AddText(textPos, textCol, r.text.c_str());
                    continue;
                }
                float xa = origin.x + seq.xCenter[r.src], xb = origin.x + seq.xCenter[r.dst];
                ImVec2 tip;
                float direction;
                if (r.src == r.dst)  // a message to itself: a small hook on the right of the lifeline
                {
                    float w = 1.5f * em;
                    ImVec2 pts[4] = {ImVec2(xa, y - 0.5f * em), ImVec2(xa + w, y - 0.5f * em), ImVec2(xa + w, y + 0.5f * em), ImVec2(xa, y + 0.5f * em)};
                    for (int i = 0; i < 3; ++i)
                        dl->AddLine(pts[i], pts[i + 1], lineCol, 1.5f);
                    dl->AddText(textPos, textCol, r.text.c_str());
                    tip = pts[3];
                    direction = -1.f;
                }
                else
                {
                    if (r.dashed)
                    {
                        float length = std::fabs(xb - xa), sign = xb > xa ? 1.f : -1.f;
                        for (float d = 0.f; d < length; d += 0.8f * em)
                            dl->AddLine(ImVec2(xa + sign * d, y), ImVec2(xa + sign * std::min(d + 0.5f * em, length), y), lineCol, 1.5f);
                    }
                    else
                        dl->AddLine(ImVec2(xa, y), ImVec2(xb, y), lineCol, 1.5f);
                    dl->AddText(textPos, textCol, r.text.c_str());
                    tip = ImVec2(xb, y);
                    direction = xb > xa ? 1.f : -1.f;
                }
                float s = 0.55f * em;
                ImVec2 back(tip.x - direction * s, tip.y);
                if (r.arrowhead)
                {
                    // the line stops at the base
                    dl->AddRectFilled(ImVec2(std::min(tip.x, back.x), tip.y - 1.f), ImVec2(std::max(tip.x, back.x), tip.y + 1.f), ImGui::GetColorU32(ImGuiCol_WindowBg));
                    dl->AddTriangleFilled(tip, ImVec2(back.x, tip.y - s * 0.45f), ImVec2(back.x, tip.y + s * 0.45f), lineCol);
                }
                else
                {
                    dl->AddLine(tip, ImVec2(back.x, tip.y - s * 0.45f), lineCol, 1.5f);
                    dl->AddLine(tip, ImVec2(back.x, tip.y + s * 0.45f), lineCol, 1.5f);
                }
            }
        }
    }

    void Draw(const Diagram& diagram)
    {
        float em = diagram.layoutFontSize;
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImVec2 origin(cursor.x + em, cursor.y + em / 2.f);
        if (diagram.kind == DiagramKind::Sequence)
            DrawSequence(diagram.sequence, origin, em);
        else if (diagram.kind == DiagramKind::Class)
            DrawClass(diagram.graph, diagram.relations, origin, em);
        else
            DrawFlowchart(diagram.graph, origin, em);
        ImGui::Dummy(DiagramSize(diagram));
    }
}
