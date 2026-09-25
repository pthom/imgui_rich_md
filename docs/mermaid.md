# Mermaid diagrams: what is supported

imgui_rich_md draws [Mermaid](https://mermaid.js.org) diagrams itself, with Dear ImGui's `ImDrawList` and the colors
of the ImGui style: no web view, no JavaScript, no dependency. In markdown, a code block of the language `mermaid`
becomes a diagram; outside of markdown, `RichMd::RenderMermaid(source)` (C++) or `rich_md.render_mermaid(source)`
(Python) draws one. The CMake option `IMGUI_RICHMD_WITH_MERMAID` (ON by default) builds it.

**The goal is a readable diagram, not a copy of mermaid.js.** imgui_rich_md supports a subset of Mermaid, meant for
the small and medium diagrams of documentation and applications. Its own layout places the nodes and routes the
edges differently from mermaid.js, and it draws in the colors of your ImGui style. For large diagrams, or when you
need Mermaid's exact look, render them with mermaid.js.

A diagram that cannot be read is shown as code, with the number of the line that stopped the parser.

Try it: the [Mermaid tour](https://pthom.github.io/imgui_rich_md/mermaid.html) lets you edit diagrams and open each
one in the Mermaid Live Editor, to compare with Mermaid's own render.

## Diagram types

| Type | Header | Support |
|---|---|---|
| Flowchart | `flowchart` or `graph`, with `TD`, `TB`, `BT`, `LR`, `RL` | yes |
| Sequence diagram | `sequenceDiagram` | yes |
| Class diagram | `classDiagram` | yes |
| All the others (state, ER, gantt, pie, git graph, mind map, timeline, ...) | | no: shown as code |

Also shown as code: the variant headers (`classDiagram-v2`, `flowchart-elk`), and a YAML front matter before the
header (`---` / `title: ...` / `---`).

## Everywhere

- `%%` comments are skipped, and so are `%%{init: ...}%%` directives: Mermaid's themes and configuration do not
  apply. The colors come from the ImGui style.
- Styling and interaction lines are read and ignored: `classDef`, `class`, `style`, `linkStyle`, `cssClass`, `:::name`,
  `click`, `callback`, `link`.
- Labels are plain text: `<br>` breaks a line, and entity codes (`#quot;`, `#35;`) are decoded. Markdown in labels
  (Mermaid's `` "`**bold**`" `` strings), other HTML tags and icons (`fa:fa-car`) are shown as written.
- `accTitle`, `accDescr` and `title` lines are not supported.

## Flowcharts

Supported:
- Node shapes: `[rect]`, `(rounded)`, `([stadium])`, `[[subroutine]]`, `[(database)]`, `((circle))`,
  `(((double circle)))`, `{diamond}`, `{{hexagon}}`, `[/parallelogram/]`, `[\parallelogram\]`, `[/trapezoid\]`,
  `[\trapezoid/]`, `>asymmetric]`. Quoted labels may contain brackets.
- Links: `-->`, `---`, `-.->`, `-.-`, `==>`, `===`, `~~~` (invisible), with `o` and `x` ends, and on both sides
  (`<-->`, `o--o`, `x--x`). Text inside (`-- text -->`, `-. text .->`, `== text ==>`) or after (`-->|text|`).
- Chains (`A --> B --> C`) and groups (`A & B --> C & D`).
- Subgraphs: `subgraph id`, `subgraph id [title]`, `subgraph some title`; nested; links to and from a subgraph
  (`A --> SubgraphId`).
- `direction` inside a subgraph, with Mermaid's rule: it applies when no link crosses the subgraph's border (a link to
  the subgraph itself is fine), and is ignored otherwise.

Not supported:
- The new shape syntax `A@{ shape: ... }`, edge ids (`e1@-->`), images and icons in nodes (shown as code, or as text).
- Link lengths: `--->` is read as `-->`, and does not make the link span more layers.
- The direction shorthands `>`, `<`, `^`, `v`.
- Edges are drawn as straight segments with right angles (no curves).

Layout differences with mermaid.js:
- A subgraph with no link to the outside and no `direction` is laid out on its own, in the flipped direction, as
  Mermaid's source does (LR inside a TD diagram, TB inside an LR one). In an LR diagram, mermaid.js 12 was seen
  drawing such a subgraph in LR: yours is then TB inside where mermaid.js is LR.
- Dense diagrams (many edges between the same layers, narrow nodes) may have more crossings than mermaid.js, and two
  edges may run very close to each other along a short stretch.

## Sequence diagrams

Supported:
- `participant A`, `participant A as Alice`, `actor B`; participants also appear at their first message.
- Messages: `->`, `-->` (no head), `->>`, `-->>` (filled head), `-x`, `--x` (cross), `-)`, `--)` (open head),
  `<<->>`, `<<-->>` (both ends); a message to itself.
- Activations: `activate` / `deactivate`, and the `+` / `-` after an arrow; nested ones.
- Notes: `Note left of A`, `Note right of A`, `Note over A`, `Note over A,B`.
- Frames: `loop`, `alt` / `else`, `opt`, `par` / `and`, `critical` / `option`, `break`, nested; `rect rgb(...)` and
  `rect rgba(...)` highlighted regions (other color forms get a neutral tint).
- `autonumber`, with an optional start and step.

Not supported (the diagram is shown as code): `box` groups, `create` and `destroy`, participant types
(`participant A@{ "type": "database" }`), `title`. `links` lines are ignored.

## Class diagrams

Supported:
- `class Name`, `class Name { members }`, `Name : member`, `class Name["Label"]`, generics (`List~int~`, nested).
- Members: a member with parentheses is a method, any other an attribute; `*` makes it abstract (italic), `$`
  static (underlined); visibility signs are shown as written.
- Annotations: `<<interface>> Name`, or `<<abstract>>` inside the class.
- Relations: inheritance (`<|--`), composition (`*--`), aggregation (`o--`), association (`-->`), link (`--`),
  dependency (`..>`), realization (`..|>`), dashed link (`..`), lollipop interfaces (`()--`), two-way relations;
  a label (`: text`) and cardinalities (`"1" --> "*"`).
- Namespaces, nested, and dotted names (`namespace A.B` makes B inside A); `note for Name "text"` and floating notes;
  `direction`.

Not supported: `click`, `callback`, `link` and the styling lines (read and ignored).

## Reporting a problem

Open an issue on [GitHub](https://github.com/pthom/imgui_rich_md/issues) with the diagram's source. The tour's
"Open in the Mermaid Live Editor" link shows how Mermaid draws the same source.
