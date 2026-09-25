// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
// ::md Drawing
// The drawing, with `ImDrawList`. The colors come from the ImGui style.
// ::endmd
#include "mermaid_model.h"

#include <algorithm>
#include <cmath>
#include <string>

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

        // The marker at the end `tip` of a line that comes from `from`; returns the point where the line stops
        ImVec2 DrawEdgeEnd(ImDrawList* dl, EdgeEnd kind, ImVec2 tip, ImVec2 from, ImU32 col, float em)
        {
            float dx = tip.x - from.x, dy = tip.y - from.y;
            float n = Length(dx, dy);
            float ux = dx / n, uy = dy / n;
            if (kind == EdgeEnd::Arrow)
            {
                float s = 0.55f * em;
                ImVec2 base(tip.x - s * ux, tip.y - s * uy);
                dl->AddTriangleFilled(tip, ImVec2(base.x + s * 0.45f * uy, base.y - s * 0.45f * ux),
                                      ImVec2(base.x - s * 0.45f * uy, base.y + s * 0.45f * ux), col);
                return base;
            }
            if (kind == EdgeEnd::Circle)
            {
                float r = 0.3f * em;
                ImVec2 c(tip.x - r * ux, tip.y - r * uy);
                dl->AddCircleFilled(c, r, col);
                return ImVec2(c.x - r * ux, c.y - r * uy);
            }
            if (kind == EdgeEnd::Cross)
            {
                float r = 0.3f * em;
                ImVec2 c(tip.x - 1.3f * r * ux, tip.y - 1.3f * r * uy);
                dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 1.5f);
                dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), col, 1.5f);
                return c;
            }
            return tip;
        }

        // The segments, and the markers at the ends
        void Polyline(ImDrawList* dl, std::vector<ImVec2> pts, ImU32 col, EdgeEnd start, EdgeEnd end, float em,
                      LineStyle style)
        {
            if (style == LineStyle::Invisible)
                return;
            pts.back() = DrawEdgeEnd(dl, end, pts.back(), pts[pts.size() - 2], col, em);
            pts.front() = DrawEdgeEnd(dl, start, pts.front(), pts[1], col, em);
            for (size_t i = 0; i + 1 < pts.size(); ++i)
                Segment(dl, pts[i], pts[i + 1], col, style, em);
        }

        // A text centered on c, line by line
        void CenteredText(ImDrawList* dl, ImVec2 c, ImU32 col, const std::string& text)
        {
            float y = c.y - ImGui::CalcTextSize(text.c_str()).y / 2.f;
            size_t start = 0;
            while (true)
            {
                size_t end = text.find('\n', start);
                const char* b = text.c_str() + start;
                const char* e = end == std::string::npos ? text.c_str() + text.size() : text.c_str() + end;
                dl->AddText(ImVec2(c.x - ImGui::CalcTextSize(b, e).x / 2.f, y), col, b, e);
                if (end == std::string::npos)
                    break;
                y += ImGui::GetTextLineHeight();
                start = end + 1;
            }
        }

        // The label's box, then its text
        void EdgeLabel(ImDrawList* dl, const Edge& e, ImVec2 origin, ImU32 textCol, ImU32 bg)
        {
            ImVec2 p0 = Add(origin, e.labelMin), p1 = Add(origin, e.labelMax);
            dl->AddRectFilled(p0, p1, bg);
            CenteredText(dl, ImVec2((p0.x + p1.x) / 2.f, (p0.y + p1.y) / 2.f), textCol, e.label);
        }

        void DrawNode(ImDrawList* dl, const Node& node, ImVec2 origin, ImU32 fill, ImU32 border, ImU32 textCol,
                      float em)
        {
            ImVec2 p0 = Add(origin, node.pos), p1 = Add(p0, node.size);
            ImVec2 c((p0.x + p1.x) / 2.f, (p0.y + p1.y) / 2.f);
            switch (node.shape)
            {
            case NodeShape::Rect:
            case NodeShape::Rounded:
            case NodeShape::Stadium:
            case NodeShape::Subroutine:
            {
                float rounding = node.shape == NodeShape::Rounded   ? 0.6f * em
                                 : node.shape == NodeShape::Stadium ? node.size.y / 2.f
                                                                    : 0.15f * em;
                dl->AddRectFilled(p0, p1, fill, rounding);
                StrokeRect(dl, p0, p1, border, rounding, 1.5f);
                if (node.shape == NodeShape::Subroutine)
                    for (float x : {p0.x + 0.5f * em, p1.x - 0.5f * em})
                        dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), border, 1.5f);
                break;
            }
            case NodeShape::Circle:
            case NodeShape::DoubleCircle:
            {
                float r = node.size.x / 2.f;
                dl->AddCircleFilled(c, r, fill);
                dl->AddCircle(c, r, border, 0, 1.5f);
                if (node.shape == NodeShape::DoubleCircle)
                    dl->AddCircle(c, r - 0.35f * em, border, 0, 1.5f);
                break;
            }
            case NodeShape::Cylinder:
            {
                float ry = GetMetrics(em).lidHeight;
                ImVec2 radius(node.size.x / 2.f, ry);
                dl->AddRectFilled(ImVec2(p0.x, p0.y + ry), ImVec2(p1.x, p1.y - ry), fill);
                dl->AddEllipseFilled(ImVec2(c.x, p1.y - ry), radius, fill);
                dl->AddEllipseFilled(ImVec2(c.x, p0.y + ry), radius, fill);
                dl->AddLine(ImVec2(p0.x, p0.y + ry), ImVec2(p0.x, p1.y - ry), border, 1.5f);
                dl->AddLine(ImVec2(p1.x, p0.y + ry), ImVec2(p1.x, p1.y - ry), border, 1.5f);
                dl->AddEllipse(ImVec2(c.x, p0.y + ry), radius, border, 0.f, 0, 1.5f);
                dl->AddEllipse(ImVec2(c.x, p1.y - ry), radius, border, 0.f, 0, 1.5f);
                break;
            }
            default:  // the polygons
            {
                std::vector<ImVec2> pts = ShapeOutline(node, em);
                for (ImVec2& p : pts)
                    p = Add(p0, p);
                if (node.shape == NodeShape::Asymmetric)
                    dl->AddConcavePolyFilled(pts.data(), (int)pts.size(), fill);
                else
                    dl->AddConvexPolyFilled(pts.data(), (int)pts.size(), fill);
                StrokeClosedPolyline(dl, pts.data(), (int)pts.size(), border, 1.5f);
                break;
            }
            }
            CenteredText(dl, c, textCol, node.label);
        }

        std::vector<ImVec2> Translated(const std::vector<ImVec2>& points, ImVec2 origin)
        {
            std::vector<ImVec2> result;
            for (ImVec2 p : points)
                result.push_back(Add(origin, p));
            return result;
        }

        // The boxes of the subgraphs, before the edges; their titles, after
        void SubgraphBoxes(ImDrawList* dl, const Graph& graph, ImVec2 origin, float em)
        {
            for (const Subgraph& sub : graph.subgraphs)
            {
                if (!sub.hasBox)
                    continue;
                ImVec2 q0 = Add(origin, sub.boxMin), q1 = Add(origin, sub.boxMax);
                dl->AddRectFilled(q0, q1, ImGui::GetColorU32(ImGuiCol_Text, 0.04f), 0.3f * em);
                StrokeRect(dl, q0, q1, ImGui::GetColorU32(ImGuiCol_Text, 0.35f), 0.3f * em, 1.f);
            }
        }

        void SubgraphTitles(ImDrawList* dl, const Graph& graph, ImVec2 origin, float em)
        {
            for (const Subgraph& sub : graph.subgraphs)
            {
                if (!sub.hasBox)
                    continue;
                ImVec2 q0 = Add(origin, sub.boxMin);
                // the title on the box's own background: an edge that crosses it passes under it
                ImVec2 t0(q0.x + sub.titleX, q0.y + GetMetrics(em).titleInset);
                ImVec2 ts = ImGui::CalcTextSize(sub.title.c_str());
                ImVec2 b0(t0.x - 2.f, t0.y), b1(t0.x + ts.x + 2.f, t0.y + ts.y);
                dl->AddRectFilled(b0, b1, ImGui::GetColorU32(ImGuiCol_WindowBg));
                dl->AddRectFilled(b0, b1, ImGui::GetColorU32(ImGuiCol_Text, 0.04f));
                dl->AddText(t0, ImGui::GetColorU32(ImGuiCol_Text, 0.8f), sub.title.c_str());
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
                if (e.points.size() < 2)  // not laid out: a bug, but not a reason to crash the application
                    continue;
                Polyline(dl, Translated(e.points, origin), border, e.start, e.end, em, e.style);
                if (!e.label.empty() && e.style != LineStyle::Invisible)
                    EdgeLabel(dl, e, origin, textCol, windowBg);
            }
            SubgraphTitles(dl, graph, origin, em);
            for (const Node& node : graph.nodes)
                DrawNode(dl, node, origin, fill, border, textCol, em);
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
                ImVec2 q1(base.x + s * 0.5f * px, base.y + s * 0.5f * py),
                    q2(base.x - s * 0.5f * px, base.y - s * 0.5f * py);
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
            if (kind == Marker::Lollipop)  // an interface: a circle at the end of the line
            {
                float r = 0.4f * em;
                ImVec2 c(tip.x + r * ux, tip.y + r * uy);
                dl->AddCircleFilled(c, r, bg);
                dl->AddCircle(c, r, col, 0, 1.5f);
                return ImVec2(c.x + r * ux, c.y + r * uy);
            }
            return tip;
        }

        // Namespaces as UML packages: the box, before the edges; the name in a tab above it, after (an edge that
        // crosses the tab passes under it)
        void PackageBoxes(ImDrawList* dl, const Graph& graph, ImVec2 origin, float em, bool tabs)
        {
            ImU32 fill = ImGui::GetColorU32(ImGuiCol_Text, 0.04f), border = ImGui::GetColorU32(ImGuiCol_Text, 0.35f);
            for (const Subgraph& sub : graph.subgraphs)
            {
                if (!sub.hasBox)
                    continue;
                ImVec2 q0 = Add(origin, sub.boxMin), q1 = Add(origin, sub.boxMax);
                float tabHeight = GetMetrics(em).titleHeight;
                if (!tabs)
                {
                    dl->AddRectFilled(ImVec2(q0.x, q0.y + tabHeight), q1, fill);
                    StrokeRect(dl, ImVec2(q0.x, q0.y + tabHeight), q1, border, 0.f, 1.f);
                    continue;
                }
                ImVec2 tab0(q0.x + sub.titleX - 0.5f * em, q0.y);
                ImVec2 tab1(tab0.x + ImGui::CalcTextSize(sub.title.c_str()).x + em, q0.y + tabHeight);
                dl->AddRectFilled(tab0, tab1, ImGui::GetColorU32(ImGuiCol_WindowBg));
                dl->AddRectFilled(tab0, tab1, fill);
                StrokeRect(dl, tab0, tab1, border, 0.f, 1.f);
                dl->AddText(ImVec2(q0.x + sub.titleX, q0.y + GetMetrics(em).titleInset),
                            ImGui::GetColorU32(ImGuiCol_Text, 0.8f), sub.title.c_str());
            }
        }

        // A class line: abstract members in italic, static ones underlined
        void DrawClassLine(ImDrawList* dl, ImVec2 pos, ImU32 col, const ClassLine& line)
        {
            ImFont* italic = line.isAbstract ? ItalicFont() : nullptr;
            if (italic)
                dl->AddText(italic, ImGui::GetFontSize(), pos, col, line.text.c_str());
            else
                dl->AddText(pos, col, line.text.c_str());
            if (line.isStatic)
            {
                ImVec2 ts = ClassLineSize(line);
                dl->AddLine(ImVec2(pos.x, pos.y + ts.y), ImVec2(pos.x + ts.x, pos.y + ts.y), col, 1.f);
            }
        }

        void DrawClass(const Graph& graph, const std::vector<Relation>& relations, ImVec2 origin, float em)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
            ImU32 border = ImGui::GetColorU32(ImGuiCol_Text, 0.6f);
            ImU32 fill = ImGui::GetColorU32(ImGuiCol_FrameBg);
            ImU32 windowBg = ImGui::GetColorU32(ImGuiCol_WindowBg);
            const float lineHeight = ImGui::GetTextLineHeight();
            PackageBoxes(dl, graph, origin, em, false);
            for (size_t i = 0; i < graph.edges.size(); ++i)
            {
                const Edge& e = graph.edges[i];
                const Relation& r = relations[i];
                if (e.points.size() < 2)  // not laid out: a bug, but not a reason to crash the application
                    continue;
                std::vector<ImVec2> pts = Translated(e.points, origin);
                ImVec2 start = DrawMarker(dl, r.markerSrc, pts[0], pts[1], border, windowBg, em);
                ImVec2 end = DrawMarker(dl, r.markerDst, pts.back(), pts[pts.size() - 2], border, windowBg, em);
                pts.front() = start;
                pts.back() = end;
                Polyline(dl, pts, border, EdgeEnd::None, EdgeEnd::None, em,
                         r.dashed ? LineStyle::Dotted : LineStyle::Solid);
                if (!e.label.empty())
                    EdgeLabel(dl, e, origin, textCol, windowBg);
                if (!r.cardinalitySrc.empty())
                    dl->AddText(Add(origin, r.cardinalitySrcPos), textCol, r.cardinalitySrc.c_str());
                if (!r.cardinalityDst.empty())
                    dl->AddText(Add(origin, r.cardinalityDstPos), textCol, r.cardinalityDst.c_str());
            }
            PackageBoxes(dl, graph, origin, em, true);
            for (const Node& node : graph.nodes)
            {
                ImVec2 p0 = Add(origin, node.pos), p1 = Add(p0, node.size);
                if (node.shape == NodeShape::Note)  // a sheet with its top right corner folded
                {
                    float f = 0.7f * em;
                    ImVec2 pts[5] = {p0, ImVec2(p1.x - f, p0.y), ImVec2(p1.x, p0.y + f), p1, ImVec2(p0.x, p1.y)};
                    dl->AddConvexPolyFilled(pts, 5, ImGui::GetColorU32(ImGuiCol_FrameBgHovered));
                    StrokeClosedPolyline(dl, pts, 5, border, 1.f);
                    dl->AddLine(ImVec2(p1.x - f, p0.y), ImVec2(p1.x - f, p0.y + f), border, 1.f);
                    dl->AddLine(ImVec2(p1.x - f, p0.y + f), ImVec2(p1.x, p0.y + f), border, 1.f);
                    CenteredText(dl, ImVec2((p0.x + p1.x) / 2.f, (p0.y + p1.y) / 2.f), textCol, node.label);
                    continue;
                }
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
                        const ClassLine& line = compartment[j];
                        ImVec2 ts = ClassLineSize(line);
                        float x = i == 0 ? (p0.x + p1.x) / 2.f - ts.x / 2.f : p0.x + 0.6f * em;  // the name is centered
                        DrawClassLine(dl, ImVec2(x, y + 0.3f * em + (float)j * lineHeight), textCol, line);
                    }
                    y += (float)compartment.size() * lineHeight + 0.6f * em;
                }
            }
        }

        // A stick figure, its head at the top of [top, top + 2.2 em]
        void DrawActor(ImDrawList* dl, float cx, float top, ImU32 col, float em)
        {
            dl->AddCircle(ImVec2(cx, top + 0.45f * em), 0.35f * em, col, 0, 1.5f);
            dl->AddLine(ImVec2(cx, top + 0.8f * em), ImVec2(cx, top + 1.5f * em), col, 1.5f);
            dl->AddLine(ImVec2(cx - 0.55f * em, top + 1.05f * em), ImVec2(cx + 0.55f * em, top + 1.05f * em), col,
                        1.5f);
            dl->AddLine(ImVec2(cx, top + 1.5f * em), ImVec2(cx - 0.45f * em, top + 2.1f * em), col, 1.5f);
            dl->AddLine(ImVec2(cx, top + 1.5f * em), ImVec2(cx + 0.45f * em, top + 2.1f * em), col, 1.5f);
        }

        // An arrow head at `tip`, for a line coming from the side `direction` (+1: from the left); returns where the
        // line stops
        float DrawSequenceArrow(ImDrawList* dl, SequenceArrow kind, ImVec2 tip, float direction, ImU32 col, float em)
        {
            float s = 0.55f * em;
            float backX = tip.x - direction * s;
            if (kind == SequenceArrow::Filled)
            {
                dl->AddTriangleFilled(tip, ImVec2(backX, tip.y - s * 0.45f), ImVec2(backX, tip.y + s * 0.45f), col);
                return backX;
            }
            if (kind == SequenceArrow::Open)
            {
                dl->AddLine(tip, ImVec2(backX, tip.y - s * 0.45f), col, 1.5f);
                dl->AddLine(tip, ImVec2(backX, tip.y + s * 0.45f), col, 1.5f);
            }
            else if (kind == SequenceArrow::Cross)
            {
                float r = 0.3f * em;
                ImVec2 c(tip.x - direction * r, tip.y);
                dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 1.5f);
                dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), col, 1.5f);
            }
            return tip.x;
        }

        void DashedLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float dash, float gap, float thickness)
        {
            float dx = b.x - a.x, dy = b.y - a.y;
            float length = std::sqrt(dx * dx + dy * dy);
            if (length < 1e-3f)
                return;
            float ux = dx / length, uy = dy / length;
            for (float d = 0.f; d < length; d += dash + gap)
            {
                float e = std::min(d + dash, length);
                dl->AddLine(ImVec2(a.x + ux * d, a.y + uy * d), ImVec2(a.x + ux * e, a.y + uy * e), col, thickness);
            }
        }

        // The style's fills are translucent: over a lifeline, a fill is laid on the window's background
        void OpaqueRect(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col, float rounding)
        {
            dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_WindowBg), rounding);
            dl->AddRectFilled(p0, p1, col, rounding);
        }

        void DrawSequence(const Sequence& seq, ImVec2 origin, float em)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
            ImU32 lineCol = ImGui::GetColorU32(ImGuiCol_Text, 0.6f);
            ImU32 fill = ImGui::GetColorU32(ImGuiCol_FrameBg);
            ImU32 noteFill = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
            ImU32 windowBg = ImGui::GetColorU32(ImGuiCol_WindowBg);
            const float lineHeight = ImGui::GetTextLineHeight();
            const float boxH = lineHeight + em;  // a participant's box (the header may be taller, for the actors)

            // Highlighted regions (rect), behind everything
            for (const SequenceFrame& f : seq.frames)
                if (f.kind == "rect")
                {
                    ImVec4 c = f.color ? ImGui::ColorConvertU32ToFloat4(f.color)
                                       : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                    c.w *= 0.25f;  // a tint: the text stays readable with a light or a dark style
                    dl->AddRectFilled(Add(origin, f.frameMin), Add(origin, f.frameMax),
                                      ImGui::ColorConvertFloat4ToU32(c));
                }

            // Lifelines, and the participants above and below
            for (size_t i = 0; i < seq.participantIds.size(); ++i)
            {
                float cx = origin.x + seq.xCenter[i];
                const std::string& label = seq.participantLabels[i];
                ImVec2 ts = ImGui::CalcTextSize(label.c_str());
                for (float top : {origin.y, origin.y + seq.height - seq.boxHeight})
                {
                    if (seq.participantIsActor[i])
                    {
                        DrawActor(dl, cx, top, lineCol, em);
                        dl->AddText(ImVec2(cx - ts.x / 2.f, top + 2.3f * em), textCol, label.c_str());
                        continue;
                    }
                    // the box touches the lifeline: at the bottom of the header above, at the top below
                    bool above = top == origin.y;
                    float y = above ? top + seq.boxHeight - boxH : top;
                    ImVec2 p0(cx - seq.boxWidth[i] / 2.f, y), p1(cx + seq.boxWidth[i] / 2.f, y + boxH);
                    OpaqueRect(dl, p0, p1, fill, 0.2f * em);
                    StrokeRect(dl, p0, p1, lineCol, 0.2f * em, 1.5f);
                    CenteredText(dl, ImVec2(cx, y + boxH / 2.f), textCol, label);
                }
                DashedLine(dl, ImVec2(cx, origin.y + seq.boxHeight), ImVec2(cx, origin.y + seq.height - seq.boxHeight),
                           lineCol, 0.5f * em, 0.5f * em, 1.f);
            }

            // Activations
            for (const SequenceActivation& a : seq.activations)
            {
                ImVec2 p0 = Add(origin, a.boxMin), p1 = Add(origin, a.boxMax);
                OpaqueRect(dl, p0, p1, fill, 0.f);
                StrokeRect(dl, p0, p1, lineCol, 0.f, 1.f);
            }

            // Frames: a border, the kind in a tab, the label; dashed separators between the sections
            for (const SequenceFrame& f : seq.frames)
            {
                if (f.kind == "rect")
                    continue;
                ImVec2 p0 = Add(origin, f.frameMin), p1 = Add(origin, f.frameMax);
                StrokeRect(dl, p0, p1, lineCol, 0.f, 1.f);
                ImVec2 kindSize = ImGui::CalcTextSize(f.kind.c_str());
                ImVec2 tab(p0.x + kindSize.x + 0.8f * em, p0.y + lineHeight + 0.5f * em);
                ImVec2 tabPts[5] = {p0, ImVec2(tab.x + 0.3f * em, p0.y), ImVec2(tab.x + 0.3f * em, tab.y - 0.3f * em),
                                    ImVec2(tab.x, tab.y), ImVec2(p0.x, tab.y)};
                dl->AddConvexPolyFilled(tabPts, 5, windowBg);
                dl->AddConvexPolyFilled(tabPts, 5, fill);
                StrokeClosedPolyline(dl, tabPts, 5, lineCol, 1.f);
                dl->AddText(ImVec2(p0.x + 0.4f * em, p0.y + 0.25f * em), textCol, f.kind.c_str());
                auto label = [&](const std::string& text, float y) {
                    if (text.empty())
                        return;
                    std::string shown = "[" + text + "]";
                    float w = ImGui::CalcTextSize(shown.c_str()).x;
                    float x = std::max((p0.x + p1.x) / 2.f - w / 2.f, tab.x + 0.8f * em);
                    dl->AddText(ImVec2(x, y), textCol, shown.c_str());
                };
                label(f.label, p0.y + 0.25f * em);
                for (size_t k = 0; k < f.sections.size() && k < f.sectionY.size(); ++k)
                {
                    float y = origin.y + f.sectionY[k];
                    DashedLine(dl, ImVec2(p0.x, y), ImVec2(p1.x, y), lineCol, 0.4f * em, 0.3f * em, 1.f);
                    label(f.sections[k].second, y + 0.25f * em);
                }
            }

            // Rows: messages and notes
            for (const SequenceRow& r : seq.rows)
            {
                float y = origin.y + r.y;
                ImVec2 textPos = Add(origin, r.textMin);
                if (r.isNote)
                {
                    ImVec2 p0 = Add(origin, r.boxMin), p1 = Add(origin, r.boxMax);
                    OpaqueRect(dl, p0, p1, noteFill, 0.1f * em);
                    StrokeRect(dl, p0, p1, lineCol, 0.1f * em, 1.f);
                    CenteredText(dl, ImVec2((p0.x + p1.x) / 2.f, (p0.y + p1.y) / 2.f), textCol, r.text);
                    continue;
                }
                float xa = origin.x + r.xStart, xb = origin.x + r.xEnd;
                const float thickness = 1.5f;
                if (r.src == r.dst)  // a message to itself: a small hook on the right of the lifeline
                {
                    float w = 1.5f * em;
                    ImVec2 pts[4] = {ImVec2(xa, y - 0.5f * em), ImVec2(xa + w, y - 0.5f * em),
                                     ImVec2(xa + w, y + 0.5f * em), ImVec2(xa, y + 0.5f * em)};
                    float end = DrawSequenceArrow(dl, r.arrowEnd, pts[3], -1.f, lineCol, em);
                    pts[3].x = end;
                    for (int i = 0; i < 3; ++i)
                    {
                        if (r.dashed)
                            DashedLine(dl, pts[i], pts[i + 1], lineCol, 0.5f * em, 0.3f * em, thickness);
                        else
                            dl->AddLine(pts[i], pts[i + 1], lineCol, thickness);
                    }
                }
                else
                {
                    float direction = xb > xa ? 1.f : -1.f;
                    float end = DrawSequenceArrow(dl, r.arrowEnd, ImVec2(xb, y), direction, lineCol, em);
                    float start = DrawSequenceArrow(dl, r.arrowStart, ImVec2(xa, y), -direction, lineCol, em);
                    if (r.dashed)
                        DashedLine(dl, ImVec2(start, y), ImVec2(end, y), lineCol, 0.5f * em, 0.3f * em, thickness);
                    else
                        dl->AddLine(ImVec2(start, y), ImVec2(end, y), lineCol, thickness);
                }
                std::string lines = r.text;
                CenteredText(
                    dl, ImVec2((textPos.x + origin.x + r.textMax.x) / 2.f, (textPos.y + origin.y + r.textMax.y) / 2.f),
                    textCol, lines);
                if (r.number > 0)  // autonumber: in a disc at the start of the line
                {
                    std::string number = std::to_string(r.number);
                    float radius = std::max(0.6f * em, ImGui::CalcTextSize(number.c_str()).x / 2.f + 0.25f * em);
                    dl->AddCircleFilled(ImVec2(xa, y), radius, textCol);
                    CenteredText(dl, ImVec2(xa, y), windowBg, number);
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
