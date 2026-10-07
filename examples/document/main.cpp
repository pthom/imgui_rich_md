// imgui_rich_md documents: markdown in a scroll area of its own, with a table of contents beside it. The document is
// made of two renders and a section of widgets between them; a link [text](#slug) reaches a heading of any of them.
// Its headings fold (DocumentOptions::foldableHeadings).
#include "common/app_glfw_gl3.h"
#include "imgui_rich_md/rich_md.h"

#include <cmath>
#include <string>

static const char* kIntro = R"md(
# A document

`BeginDocument()` and `EndDocument()` frame the renders and the widgets of a document. They share a scroll area, and a
table of contents on the left: its edge can be dragged, and the arrow at its top hides it.

Between them, call `Render()` as many times as you like, and any other widget, as the slider and the curve below: they
all belong to the document, with one table of contents and one search (see the code in "The search").

Links to the sections, wherever they are: [the widgets](#a-section-of-widgets), [the collapsed
section](#inside-a-collapsed-section), [the end](#the-end).

The headings fold: the arrow at their left, shown under the mouse, hides their section. The menu of a right click,
and the buttons at the top of the table of contents, fold or unfold them all.

## Headings and anchors

Each heading has a *slug*, made from its text as GitHub does: `## Headings and anchors` is `#headings-and-anchors`. A
link to it scrolls the document there, also when the heading is in another render of the document.

A repeated title gets a number: the second "Notes" below is `#notes-1`.

## Notes

The first section named "Notes".

## Collapsed sections

<details>
<summary>A collapsed section</summary>

### Inside a collapsed section

A heading hidden in a collapsed section is listed in the table of contents, dimmed. A click on it, or a link to it,
opens the section.

</details>
)md";

static const char* kSecondRender = R"md(
## Notes

The second section named "Notes", in the second render of the document: its slug is `#notes-1`.

## The search

Ctrl+F (Cmd+F on macOS) opens the find bar. The search also finds the text of the collapsed sections (a jump to one of
their matches opens them), and of the code blocks:

```cpp
RichMd::BeginDocument("document");
RichMd::Render(intro);  // markdown: as many renders as you like

// Widgets, under a heading of the document (false: its section is folded)
if (RichMd::DocumentHeading(2, "A section of widgets")) {
    ImGui::SliderFloat("Frequency", &frequency, 0.5f, 5.f);
    DrawCurve(frequency);  // any ImGui drawing
}

RichMd::Render(moreMarkdown);  // the same document: one table of contents, one search
RichMd::EndDocument();
```

## Long text

The table of contents marks the section at the top of the view, and follows it as the document scrolls.

)md";

static std::string LongText()
{
    std::string md = kSecondRender;
    for (int i = 1; i <= 12; ++i)
        md += "Paragraph " + std::to_string(i) + ": some text to scroll through, long enough to wrap on a narrow window, "
              "so that the document has a length worth a table of contents.\n\n";
    md += "## The end\n\nBack to [the top](#a-document).\n";
    return md;
}

// A curve drawn with ImGui's draw list, the width of the window
static void DrawCurve(float frequency)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 6.f);
    ImGui::Dummy(size);
    for (int i = 1; i < 200; ++i) {
        float x0 = (float)(i - 1) / 199.f, x1 = (float)i / 199.f;
        auto y = [&](float x) {
            return origin.y + size.y * (0.5f - 0.4f * std::sin(frequency * 6.2832f * x));
        };
        drawList->AddLine(ImVec2(origin.x + x0 * size.x, y(x0)), ImVec2(origin.x + x1 * size.x, y(x1)),
                          ImGui::GetColorU32(ImGuiCol_PlotLines), 2.f);
    }
}

static void Gui()
{
    static const std::string kLongText = LongText();
    static float frequency = 2.f;

    RichMd::DocumentOptions options;
    options.foldableHeadings = true;  // an arrow at the left of each heading folds its section
    RichMd::BeginDocument("document", ImVec2(0.f, 0.f), options);
    RichMd::Render(kIntro);

    // A section of widgets: DocumentHeading() draws its title as a markdown heading, and gives it a slug. It returns
    // false when the section is folded: its widgets are skipped.
    if (RichMd::DocumentHeading(2, "A section of widgets")) {
        ImGui::SliderFloat("Frequency", &frequency, 0.5f, 5.f);
        DrawCurve(frequency);
    }

    RichMd::Render(kLongText);
    RichMd::EndDocument();
}

int main(int, char**)
{
    return RunApp("imgui_rich_md: a document", ImVec2(900, 650), RichMd::MarkdownOptions(), Gui);
}
