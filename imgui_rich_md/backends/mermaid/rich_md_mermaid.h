// Part of ImGui Bundle - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_bundle
//
// Mermaid diagrams, a native subset: flowcharts, sequence diagrams and class diagrams, parsed, laid out and drawn
// with ImDrawList. Internal header (rich_md.cpp and the tests); applications call RichMd::RenderMermaid.
#pragma once
#include "imgui.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace RichMd::Mermaid
{
    // Flowcharts and class diagrams: a graph laid out in layers
    enum class NodeShape
    {
        Rect, Rounded, Stadium, Subroutine, Cylinder, Circle, DoubleCircle, Diamond, Hexagon,
        Parallelogram, ParallelogramAlt, Trapezoid, TrapezoidAlt, Asymmetric,
        Note  // class diagrams: a note (a sheet with a folded corner)
    };
    enum class LineStyle { Solid, Dotted, Thick, Invisible };
    enum class EdgeEnd { None, Arrow, Circle, Cross };

    // A line of a class: its name, an annotation, an attribute or a method
    struct ClassLine
    {
        std::string text;
        bool isStatic = false, isAbstract = false;  // underlined, italic
    };

    struct Node
    {
        std::string id, label;
        NodeShape shape = NodeShape::Rect;
        int subgraph = -1;                                   // index in Graph::subgraphs, -1: none
        std::vector<std::vector<ClassLine>> compartments;  // class diagrams: the name (after its annotations), the attributes, the methods
        // filled by the layout
        int rank = 0, order = 0;
        ImVec2 pos, size;                                    // relative to the diagram's origin
    };

    struct Edge
    {
        int src = 0, dst = 0;
        std::string label;
        EdgeEnd start = EdgeEnd::None, end = EdgeEnd::Arrow;
        LineStyle style = LineStyle::Solid;
        int srcBox = -1, dstBox = -1;  // an end on a subgraph's box (A --> SubgraphId): the subgraph; the layout then sets
                                       // src or dst to a member of it
        // filled by the layout, relative to the diagram's origin
        std::vector<ImVec2> points;   // the polyline, from the source to the target
        float track = 0.f;            // where its segment across the channel runs, from the channel's middle
        int lane = 0;                 // a lane edge: its lane (1, 2...), 0 otherwise
        int exitTrack = 0, entryTrack = 0;  // a lane edge: its approach lines after its source's layer, before its target's
        ImVec2 labelMin, labelMax;    // the box behind the label (when there is one)
    };

    struct Subgraph
    {
        std::string id, title;
        int parent = -1;           // the subgraph around it (index in Graph::subgraphs), -1: none
        bool hasDirection = false; // flowcharts: `direction` inside it, then vertical and reversed as in Graph
        bool vertical = true, reversed = false;
        bool hasBox = false;       // filled by the layout (false when the subgraph has no node)
        ImVec2 boxMin, boxMax;
        float titleX = 0.f;        // where the title starts, from the left of the box (where no edge crosses it)
    };

    struct Graph
    {
        bool vertical = true;      // TD / TB / BT; false: LR / RL
        bool reversed = false;     // BT, RL
        std::vector<Node> nodes;   // in the order of the source
        std::vector<Edge> edges;
        std::vector<Subgraph> subgraphs;
        // filled by the layout
        std::set<std::pair<int, int>> backEdges;  // (src, dst) of the edges that close a cycle
        int lanes = 0;             // edges routed past the graph: back edges and edges that skip a layer
        std::map<int, float> channels;  // along the main axis, where the edges from the layer r to the layer r + 1 run
        ImVec2 size;
    };

    // Class diagrams: a relation per edge of the graph, with its UML markers
    enum class Marker { None, Triangle, Diamond, DiamondFilled, Arrow, Lollipop };

    struct Relation
    {
        Marker markerSrc = Marker::None, markerDst = Marker::None;
        bool dashed = false;
        std::string cardinalitySrc, cardinalityDst;  // the label is the edge's
        ImVec2 cardinalitySrcPos, cardinalityDstPos;  // filled by the layout: where their text starts
    };

    // Sequence diagrams
    enum class SequenceArrow { None, Filled, Open, Cross };

    struct SequenceRow
    {
        bool isNote = false;
        int src = 0, dst = 0;          // message
        bool dashed = false;
        SequenceArrow arrowStart = SequenceArrow::None, arrowEnd = SequenceArrow::Filled;
        int number = 0;                // autonumber (0: none)
        std::vector<int> over;         // note: the participants it covers
        int notePlacement = 0;         // note: 0 over, -1 left of, 1 right of
        std::string text;
        // filled by the layout, relative to the diagram's origin
        float y = 0.f;                 // the line of a message, the middle of a note
        float xStart = 0.f, xEnd = 0.f;  // message: the ends of its line (activations move them)
        ImVec2 textMin, textMax;       // where the text is drawn
        ImVec2 boxMin, boxMax;         // note: its box
    };

    // loop, alt, opt, par, critical, break: a frame with a header; rect: a colored background
    struct SequenceFrame
    {
        std::string kind, label;
        int first = 0, last = -1;      // the rows inside the frame
        std::vector<std::pair<int, std::string>> sections;  // else / and / option: the row where each starts, its label
        ImU32 color = 0;               // rect
        // filled by the layout
        int depth = 0;                 // nesting
        ImVec2 frameMin, frameMax;
        std::vector<float> sectionY;   // the separators
    };

    struct SequenceActivation
    {
        int participant = 0;
        int startRow = -1, endRow = -1;  // the rows of the messages that start and end it (-1: before the first row)
        ImVec2 boxMin, boxMax;           // filled by the layout
    };

    struct Sequence
    {
        std::vector<std::string> participantIds, participantLabels;
        std::vector<bool> participantIsActor;
        std::vector<SequenceRow> rows;
        std::vector<SequenceFrame> frames;
        std::vector<SequenceActivation> activations;
        // filled by the layout
        std::vector<float> boxWidth, xCenter;
        float totalWidth = 0.f, boxHeight = 0.f, height = 0.f;
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
    // The outline of a node's shape, in its local coordinates (from (0, 0) to its size); curves are approximated
    std::vector<ImVec2> ShapeOutline(const Node& node, float em);
    // The italic font of the markdown (abstract members), or nullptr outside of a markdown context
    ImFont* ItalicFont();
    // The size of a class line's text: abstract members are in italic
    ImVec2 ClassLineSize(const ClassLine& line);

    // Parses (once per source), lays out (when the font changes) and draws
    struct RenderResult
    {
        bool drawn = false;
        std::string error;  // when not drawn: the parse error, "line N: ..."
    };
    RenderResult Render(const std::string& source);
    void ClearCache();
}
