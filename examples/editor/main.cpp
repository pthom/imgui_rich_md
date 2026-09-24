// imgui_rich_md editor: the markdown source on the left, the rendered document on the right, updated as you type.
// With IMGUI_RICHMD_WITH_CODE_EDITOR the source is edited with ImGuiColorTextEdit (markdown highlighting),
// otherwise with ImGui::InputTextMultiline.
#include "common/app_glfw_gl3.h"
#include "imgui_rich_md/rich_md.h"
#include "imgui_stdlib.h"  // InputTextMultiline with a std::string
#ifdef IMGUI_RICHMD_WITH_CODE_EDITOR
#include "ImGuiColorTextEdit/TextEditor.h"
#endif

#include <string>

struct Sample
{
    const char* name;
    const char* markdown;
};

static const Sample kSamples[] = {
    {"Welcome", R"md(# Welcome
Type on the left: the document on the right follows.

Markdown gives you *emphasis*, **bold**, `inline code`, [links](https://github.com/pthom/imgui_rich_md),
and lists:
- one
- two
    - nested

> A quote, for good measure.
)md"},
    {"Table and code", R"md(## A table
| Language | Typing  |
|----------|:-------:|
| C++      | static  |
| Python   | dynamic |

## A code block
```cpp
int main()
{
    return 0;
}
```
)md"},
    {"Formulas", R"md(## Formulas
Inline, $e^{i\pi} + 1 = 0$, and on its own line:

$$
\int_0^1 x^2 \, dx = \frac{1}{3}
$$

Formulas need the library built with `IMGUI_RICHMD_WITH_LATEX`; otherwise they show as source.
)md"},
    {"Mermaid diagrams", R"md(## Mermaid diagrams
Flowcharts, sequence diagrams and class diagrams, drawn natively: edit them on the left.

```mermaid
flowchart LR
    A[Idea] --> B{Worth it?}
    B -->|yes| C([Build it]) --> D[Ship]
    B -->|no| E[Forget it]
```

```mermaid
sequenceDiagram
    participant U as User
    participant E as Editor
    U->>+E: types
    E-->>-U: renders
```
)md"},
    {"Callouts and sections", R"md(> [!TIP]
> Callouts start with `[!NOTE]`, `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]` or `[!CAUTION]`.

<details>
<summary>A collapsible section</summary>

Hidden until you click. The blank lines around the tags let the content be parsed as markdown.

</details>
)md"},
};

static int gSample = 0;
static std::string gText = kSamples[0].markdown;

#ifdef IMGUI_RICHMD_WITH_CODE_EDITOR
// Created at the first frame, once Dear ImGui exists
static TextEditor& Editor()
{
    static TextEditor editor;
    static bool initialized = false;
    if (!initialized)
    {
        editor.SetLanguage(TextEditor::Language::Markdown());
        editor.SetPalette(TextEditor::GetDarkPalette());
        editor.SetShowWhitespacesEnabled(false);
        editor.SetText(gText);
        editor.SetChangeCallback([] { gText = Editor().GetText(); });
        initialized = true;
    }
    return editor;
}
#endif

static void SetText(const std::string& text)
{
    gText = text;
#ifdef IMGUI_RICHMD_WITH_CODE_EDITOR
    Editor().SetText(text);
#endif
}

static void SourcePane()
{
    RichMd::SizedFont codeFont = RichMd::GetCodeFont();
    ImGui::PushFont(codeFont.font, codeFont.size);
#ifdef IMGUI_RICHMD_WITH_CODE_EDITOR
    Editor().Render("##source", ImGui::GetContentRegionAvail());
#else
    ImGui::InputTextMultiline("##source", &gText, ImGui::GetContentRegionAvail());
#endif
    ImGui::PopFont();
}

static void Gui()
{
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
    if (ImGui::BeginCombo("Sample", kSamples[gSample].name))
    {
        for (int i = 0; i < IM_ARRAYSIZE(kSamples); ++i)
            if (ImGui::Selectable(kSamples[i].name, i == gSample))
            {
                gSample = i;
                SetText(kSamples[i].markdown);
            }
        ImGui::EndCombo();
    }

    // The source, resizable by dragging its right border, then the rendered document
    ImGui::BeginChild("source", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0.f),
                      ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
    SourcePane();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("render", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders);
    RichMd::Render(gText);
    ImGui::EndChild();
}

int main(int, char**)
{
    RichMd::MarkdownOptions options;
#ifdef IMGUI_RICHMD_WITH_LATEX
    options.withLatex = true;
#endif
    return RunApp("imgui_rich_md editor", ImVec2(1200.f, 800.f), options, Gui);
}
