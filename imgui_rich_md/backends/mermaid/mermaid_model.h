// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
#pragma once
/*::md Mermaid model and pipeline
The Mermaid backend's model and pipeline, for its own files and for the tests (`rich_md.cpp` only needs
`rich_md_mermaid.h`):

    Parse: source -> Diagram;  Layout: positions, sizes, polylines;  Draw: with ImDrawList

In the model, the fields after "filled by the layout" are results of `Layout`; the others come from `Parse`.
*/
#include "imgui.h"

#include <string>
#include <utility>
#include <vector>

namespace RichMd::Mermaid
{
    // -----------------------------------------------------------------------------------------------------------------
    // The model: flowcharts and class diagrams (a graph laid out in layers)
    // -----------------------------------------------------------------------------------------------------------------
    // ::code Graph model
    enum class NodeShape
    {
        Rect,
        Rounded,
        Stadium,
        Subroutine,
        Cylinder,
        Circle,
        DoubleCircle,
        Diamond,
        Hexagon,
        Parallelogram,
        ParallelogramAlt,
        Trapezoid,
        TrapezoidAlt,
        Asymmetric,
        Note  // class diagrams: a note (a sheet with a folded corner)
    };
    enum class LineStyle
    {
        Solid,
        Dotted,
        Thick,
        Invisible
    };
    enum class EdgeEnd
    {
        None,
        Arrow,
        Circle,
        Cross
    };

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
        int subgraph = -1;  // index in Graph::subgraphs, -1: none
        // class diagrams: the name (after its annotations), the attributes, the methods
        std::vector<std::vector<ClassLine>> compartments;
        // the layout keeps its size (a subgraph laid out on its own)
        bool keepSize = false;
        // filled by the layout
        int rank = 0;      // its layer (a subgraph laid out on its own has its ranks apart)
        ImVec2 pos, size;  // relative to the diagram's origin
    };

    struct Edge
    {
        int src = 0, dst = 0;
        std::string label;
        EdgeEnd start = EdgeEnd::None, end = EdgeEnd::Arrow;
        LineStyle style = LineStyle::Solid;
        // an end on a subgraph's box (A --> SubgraphId): the subgraph (the layout sets src or dst to a member)
        int srcBox = -1, dstBox = -1;
        // filled by the layout, relative to the diagram's origin
        std::vector<ImVec2> points;  // the polyline, from the source to the target
        // routed past the graph, on the lane 1, 2...: a back edge, or an edge that skips a layer; 0 otherwise
        int lane = 0;
        ImVec2 labelMin, labelMax;  // the box behind the label (when there is one)
    };

    struct Subgraph
    {
        std::string id, title;
        int parent = -1;            // the subgraph around it (index in Graph::subgraphs), -1: none
        bool hasDirection = false;  // flowcharts: `direction` inside it, then vertical and reversed as in Graph
        bool vertical = true, reversed = false;
        bool hasBox = false;  // filled by the layout (false when the subgraph has no node)
        ImVec2 boxMin, boxMax;
        float titleX = 0.f;  // where the title starts, from the left of the box (where no edge crosses it)
    };

    struct Graph
    {
        bool vertical = true;     // TD / TB / BT; false: LR / RL
        bool reversed = false;    // BT, RL
        std::vector<Node> nodes;  // in the order of the source
        std::vector<Edge> edges;
        std::vector<Subgraph> subgraphs;
        // filled by the layout
        ImVec2 size;
    };

    // Class diagrams: a relation per edge of the graph, with its UML markers
    enum class Marker
    {
        None,
        Triangle,
        Diamond,
        DiamondFilled,
        Arrow,
        Lollipop
    };

    struct Relation
    {
        Marker markerSrc = Marker::None, markerDst = Marker::None;
        bool dashed = false;
        std::string cardinalitySrc, cardinalityDst;   // the label is the edge's
        ImVec2 cardinalitySrcPos, cardinalityDstPos;  // filled by the layout: where their text starts
    };
    // ::endcode

    // -----------------------------------------------------------------------------------------------------------------
    // The model: sequence diagrams
    // -----------------------------------------------------------------------------------------------------------------
    // ::code Sequence model
    enum class SequenceArrow
    {
        None,
        Filled,
        Open,
        Cross
    };

    struct SequenceRow
    {
        bool isNote = false;
        int src = 0, dst = 0;  // message
        bool dashed = false;
        SequenceArrow arrowStart = SequenceArrow::None, arrowEnd = SequenceArrow::Filled;
        int number = 0;         // autonumber (0: none)
        std::vector<int> over;  // note: the participants it covers
        int notePlacement = 0;  // note: 0 over, -1 left of, 1 right of
        std::string text;
        // filled by the layout, relative to the diagram's origin
        float y = 0.f;                   // the line of a message, the middle of a note
        float xStart = 0.f, xEnd = 0.f;  // message: the ends of its line (activations move them)
        ImVec2 textMin, textMax;         // where the text is drawn
        ImVec2 boxMin, boxMax;           // note: its box
    };

    // loop, alt, opt, par, critical, break: a frame with a header; rect: a colored background
    struct SequenceFrame
    {
        std::string kind, label;
        int first = 0, last = -1;                           // the rows inside the frame
        std::vector<std::pair<int, std::string>> sections;  // else / and / option: the row where each starts, its label
        ImU32 color = 0;                                    // rect
        // filled by the layout
        int depth = 0;  // nesting
        ImVec2 frameMin, frameMax;
        std::vector<float> sectionY;  // the separators
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
    // ::endcode

    // -----------------------------------------------------------------------------------------------------------------
    // The model: a diagram
    // -----------------------------------------------------------------------------------------------------------------
    // ::code Diagram
    enum class DiagramKind
    {
        Flowchart,
        Sequence,
        Class
    };

    struct Diagram
    {
        DiagramKind kind = DiagramKind::Flowchart;
        std::string error;                // "line N: ...": the source could not be parsed
        Graph graph;                      // flowchart, class
        std::vector<Relation> relations;  // class: one per edge of the graph
        Sequence sequence;
        // the font of its layout
        ImFont* layoutFont = nullptr;
        float layoutFontSize = 0.f;
    };
    // ::endcode

    // -----------------------------------------------------------------------------------------------------------------
    // The pipeline
    // -----------------------------------------------------------------------------------------------------------------
    // ::code Pipeline
    Diagram Parse(const std::string& source);
    void Layout(Diagram& diagram);               // with the current font
    ImVec2 DiagramSize(const Diagram& diagram);  // once laid out, the size Draw reserves
    void Draw(const Diagram& diagram);           // at the cursor, then a Dummy of DiagramSize
    // ::endcode

    // -----------------------------------------------------------------------------------------------------------------
    // Shared by the layout, the drawing and the tests
    // -----------------------------------------------------------------------------------------------------------------
    // ::code Shared
    // The distances that the layout, the drawing and the tests must agree on
    struct Metrics
    {
        float boxPad;       // a subgraph's box, around its members
        float titleHeight;  // the room of a box's title, above its members
        float titleInset;   // from the top of a box to the top of its title
        float lidHeight;    // a cylinder's lids: the half height of their ellipses
        float lineStep;     // between two parallel lines: the tracks of a channel, the approach lines of the lanes
        float laneStep;     // between two lanes
    };
    Metrics GetMetrics(float em);  // with the current font (its line height)
    // The outline of a node's shape, in its local coordinates (from (0, 0) to its size); curves are approximated
    std::vector<ImVec2> ShapeOutline(const Node& node, float em);
    // The italic font of the markdown (abstract members), or nullptr outside of a markdown context
    ImFont* ItalicFont();
    // The size of a class line's text: abstract members are in italic
    ImVec2 ClassLineSize(const ClassLine& line);
    // ::endcode
}
