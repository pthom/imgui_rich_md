// Part of ImGui Bundle - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_bundle
//
// Mermaid diagrams, a native subset: flowcharts, sequence diagrams and class diagrams, parsed, laid out and drawn
// with ImDrawList. Internal header (rich_md.cpp and the tests); applications call RichMd::RenderMermaid.
#pragma once
#include "imgui.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace RichMd::Mermaid
{
    // Flowcharts and class diagrams: a graph laid out in layers
    enum class NodeShape { Rect, Rounded, Diamond, Cylinder };
    enum class LineStyle { Solid, Dotted, Thick };

    struct Node
    {
        std::string id, label;
        NodeShape shape = NodeShape::Rect;
        int subgraph = -1;                                   // index in Graph::subgraphs, -1: none
        std::vector<std::vector<std::string>> compartments;  // class diagrams: the name (after its annotations), the attributes, the methods
        // filled by the layout
        int rank = 0, order = 0;
        ImVec2 pos, size;                                    // relative to the diagram's origin
    };

    struct Edge
    {
        int src = 0, dst = 0;
        std::string label;
        bool arrow = true;
        LineStyle style = LineStyle::Solid;
    };

    struct Subgraph
    {
        std::string id, title;
        bool hasBox = false;       // filled by the layout (false when the subgraph has no node)
        ImVec2 boxMin, boxMax;
    };

    struct Graph
    {
        bool vertical = true;      // TD / TB; false: LR
        std::vector<Node> nodes;   // in the order of the source
        std::vector<Edge> edges;
        std::vector<Subgraph> subgraphs;
        // filled by the layout
        std::set<std::pair<int, int>> backEdges;  // (src, dst) of the edges that close a cycle
        int lanes = 0;             // edges routed past the graph: back edges and edges that skip a layer
        ImVec2 size;
    };

    // Class diagrams: a relation per edge of the graph, with its UML markers
    enum class Marker { None, Triangle, Diamond, DiamondFilled, Arrow };

    struct Relation
    {
        Marker markerSrc = Marker::None, markerDst = Marker::None;
        bool dashed = false;
        std::string label, cardinalitySrc, cardinalityDst;
    };

    // Sequence diagrams
    struct SequenceRow
    {
        bool isNote = false;
        int src = 0, dst = 0;          // message
        bool dashed = false, arrowhead = true;
        std::vector<int> over;         // note: the participants it covers
        std::string text;
    };

    struct SequenceLoop
    {
        std::string label;
        int first = 0, last = -1;      // the rows inside the loop
    };

    struct Sequence
    {
        std::vector<std::string> participantIds, participantLabels;
        std::vector<SequenceRow> rows;
        std::vector<SequenceLoop> loops;
        // filled by the layout
        std::vector<float> boxWidth, xCenter;
        float totalWidth = 0.f, boxHeight = 0.f, rowHeight = 0.f, height = 0.f;
    };

    enum class DiagramKind { Flowchart, Sequence, Class };

    struct Diagram
    {
        DiagramKind kind = DiagramKind::Flowchart;
        std::string error;                 // "line N: ...": the source could not be parsed
        Graph graph;                       // flowchart, class
        std::vector<Relation> relations;   // class: one per edge of the graph
        Sequence sequence;
        // the font of the last layout (laid out again when it changes)
        ImFont* layoutFont = nullptr;
        float layoutFontSize = 0.f;
    };

    Diagram Parse(const std::string& source);
    void Layout(Diagram& diagram);                          // with the current font
    ImVec2 DiagramSize(const Diagram& diagram);             // once laid out, the size Draw reserves
    void Draw(const Diagram& diagram);                      // at the cursor, then a Dummy of DiagramSize
    // Back edges and edges that skip a layer travel on a lane past the graph
    bool UsesLane(const Graph& graph, const Edge& e);

    // Parses (once per source), lays out (when the font changes) and draws
    struct RenderResult
    {
        bool drawn = false;
        std::string error;  // when not drawn: the parse error, "line N: ..."
    };
    RenderResult Render(const std::string& source);
    void ClearCache();
}
