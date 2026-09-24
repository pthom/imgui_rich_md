// imgui_rich_md Mermaid tour: the diagrams the library draws natively, each with its source to edit (the diagram
// follows as you type), and a link that opens it in the Mermaid Live Editor, to compare with Mermaid's own render.
#include "common/app_glfw_gl3.h"
#include "imgui_rich_md/rich_md.h"
#include "imgui_stdlib.h"  // InputTextMultiline with a std::string

#include <string>
#include <vector>

static const char* kIntro = R"md(
# Mermaid diagrams, drawn natively

imgui_rich_md draws [Mermaid](https://mermaid.js.org) diagrams itself, with Dear ImGui's `ImDrawList` and the colors
of the ImGui style: no web view, no JavaScript. In a markdown document, a code block of the language `mermaid`
becomes a diagram.

**Edit a source on the left**: the diagram follows as you type. The link under each source opens the same diagram in
the Mermaid Live Editor, to compare with Mermaid's own render.

> [!NOTE]
> A subset of Mermaid:
> - **Supported**: flowcharts (shapes, links, labels, nested subgraphs and links to them, the four directions, also
>   inside a subgraph), sequence diagrams (participants, actors, activations, notes, frames, autonumber), class
>   diagrams (members, relations, cardinalities, namespaces, notes, generics).
> - **Not supported**: the other diagram types (state, ER, gantt, pie, git graphs, mind maps...). Styles (`classDef`,
>   `style`) and `click` are ignored.
>
> A diagram that cannot be read is shown as code, with the line of the error.
)md";

struct Diagram
{
    const char* heading;  // markdown: a section title, then the diagram's title and what it shows
    std::string source;
};

static std::vector<Diagram> gDiagrams = {
    {R"md(## Flowcharts
### Shapes and decisions
Rectangles, stadiums, diamonds, parallelograms, databases; labels on the links.)md",
     R"(flowchart TD
    Start([Start]) --> Check{Logged in?}
    Check -->|yes| Home[Home page]
    Check -->|no| Login[/Login form/]
    Login --> Users[(Users)]
    Users --> Home)"},
    {R"md(### Subgraphs
Nested subgraphs, left to right, and a dotted link going back.)md",
     R"(flowchart LR
    subgraph Browser
        UI[Web app]
    end
    subgraph Cloud
        subgraph Tier[API tier]
            Gateway --> Service
        end
        Service --> DB[(Database)]
    end
    UI --> Gateway
    Service -.->|events| UI)"},
    {R"md(### Links
Chains, several nodes at once (`&`), text inside a link, thick and dotted lines, circle and cross ends.)md",
     R"(flowchart TD
    A[Source] --> B[Parse] & C[Check]
    B -- tokens --> D[Build]
    C -. warnings .-> D
    D ==> E((Done))
    A --o F[Log]
    A --x G[Abort])"},
    {R"md(## Sequence diagrams
### Messages and activations
An actor, activation boxes (the `+` and `-` after an arrow), numbered messages.)md",
     R"(sequenceDiagram
    autonumber
    actor User
    participant App
    participant API
    User->>+App: click "Save"
    App->>+API: POST /document
    API-->>-App: 201 Created
    App-->>-User: saved)"},
    {R"md(### Frames and notes
`alt` / `else`, `loop`, `par` / `and`, a note over two participants, asynchronous messages.)md",
     R"(sequenceDiagram
    participant C as Client
    participant S as Server
    C->>S: request
    alt cached
        S-->>C: cached answer
    else not cached
        loop each page
            S->>S: fetch a page
        end
        S-->>C: fresh answer
    end
    Note over C,S: the connection stays open
    par notify
        S-)C: event
    and log
        S-)S: write the log
    end)"},
    {R"md(## Class diagrams
### Classes and relations
Compartments, an annotation, abstract (italic) and static (underlined) members, inheritance, aggregation with
cardinalities.)md",
     R"(classDiagram
    class Animal {
        <<abstract>>
        +String name
        +makeSound()* void
        +count()$ int
    }
    class Dog {
        +fetch() void
    }
    class Owner {
        +List~Animal~ pets
    }
    Animal <|-- Dog
    Owner "1" o-- "*" Animal : owns)"},
    {R"md(### Namespaces, generics and notes
A namespace (drawn as a UML package), a generic class, a dependency, a note.)md",
     R"(classDiagram
    namespace Storage {
        class Repository~T~ {
            +find(id) T
            +save(item: T)
        }
        class Cache
    }
    class Service
    Service --> Repository : uses
    Repository ..> Cache
    note for Cache "evicts what was not used")"},
};

// A link that opens a diagram in the Mermaid Live Editor: the editor's state (JSON) in the URL, in base64url
static std::string JsonString(const std::string& s)
{
    std::string out = "\"";
    for (unsigned char c : s)
    {
        if (c == '"' || c == '\\')
            out += '\\', out += (char)c;
        else if (c == '\n')
            out += "\\n";
        else if (c == '\t')
            out += "\\t";
        else if (c >= 0x20)
            out += (char)c;
    }
    return out + "\"";
}

static std::string Base64Url(const std::string& data)
{
    static const char* digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    unsigned bits = 0;
    int count = 0;
    for (unsigned char c : data)
    {
        bits = (bits << 8) | c, count += 8;
        while (count >= 6)
            out += digits[(bits >> (count - 6)) & 63], count -= 6;
    }
    if (count > 0)
        out += digits[(bits << (6 - count)) & 63];
    return out;
}

static std::string MermaidLiveUrl(const std::string& source)
{
    std::string state = "{\"code\":" + JsonString(source) + ",\"mermaid\":" + JsonString("{\"theme\": \"default\"}")
                      + ",\"updateDiagram\":true,\"rough\":false}";
    return "https://mermaid.live/edit#base64:" + Base64Url(state);
}

// A diagram: its source to edit and the link on the left, the diagram on the right
static void ShowDiagram(Diagram& d)
{
    RichMd::Render(d.heading);
    float em = ImGui::GetFontSize();
    ImGui::BeginGroup();
    RichMd::SizedFont code = RichMd::GetCodeFont();
    ImGui::PushFont(code.font, code.size);
    int lines = 1;
    for (char c : d.source)
        lines += c == '\n' ? 1 : 0;
    ImVec2 size(24.f * em, (float)(lines + 1) * ImGui::GetTextLineHeight() + 2.f * ImGui::GetStyle().FramePadding.y);
    ImGui::InputTextMultiline("##source", &d.source, size, ImGuiInputTextFlags_AllowTabInput);
    ImGui::PopFont();
    RichMd::SizedFont text = RichMd::GetFont(RichMd::MarkdownFontSpec());
    ImGui::PushFont(text.font, text.size);
    RichMd::RenderTextAsLink("Open in the Mermaid Live Editor", MermaidLiveUrl(d.source).c_str());
    ImGui::PopFont();
    ImGui::EndGroup();
    ImGui::SameLine(0.f, 1.5f * em);
    ImGui::BeginChild("diagram", ImVec2(0.f, 0.f), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_HorizontalScrollbar);
    RichMd::RenderRaw("```mermaid\n" + d.source + "\n```\n");
    ImGui::EndChild();
}

static void Gui()
{
    RichMd::Render(kIntro);
    for (size_t i = 0; i < gDiagrams.size(); ++i)
    {
        ImGui::PushID((int)i);
        ShowDiagram(gDiagrams[i]);
        ImGui::PopID();
    }
}

int main(int, char**)
{
    RichMd::MarkdownOptions options;
    return RunApp("imgui_rich_md Mermaid tour", ImVec2(1200.f, 900.f), options, Gui);
}
