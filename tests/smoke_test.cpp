// Smoke test: a document using every feature (and the error paths), rendered for a few frames with Dear ImGui's
// null backend (no window, no GPU: its renderer only marks the textures created and destroyed). The whole cycle,
// from CreateContext to DestroyContext, runs twice. It fails on a crash, on an assertion (build it in
// Debug), or when nothing was drawn.
#include "imgui.h"
#include "imgui_impl_null.h"
#include "imgui_rich_md/rich_md.h"
#include "imgui_rich_md/internal/rich_md_internal.h"  // Context::resolvedTransclusions

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static const char* kDocument = R"md(
# Smoke test
*emphasis*, **bold**, ***both***, ~~strikethrough~~, <u>underlined</u>, <mark>marked</mark>, <kbd>Ctrl</kbd>,
H<sub>2</sub>O, x<sup>2</sup>, `inline code`, a [link](https://github.com/pthom/imgui_rich_md) and an autolink:
https://github.com/pthom/imgui_rich_md

## Lists and quotes
1. first
2. second
    - nested
        1. deeper

> A quote.

> [!NOTE]
> An admonition.

- [x] done
- [ ] to do

## Table
| Left | Right | Center |
|:-----|------:|:------:|
| a    | 1     | x      |
| b    | 2     | y      |

## Code
```cpp
int main()
{
    return 0;
}
```

```csv
name,score
Alice,10
```

## Images
An embedded image: ![broken](images/markdown_broken_image.png) and a missing one: ![missing](images/missing.png)
<img src="images/markdown_broken_image.png" width="40">

## Formulas
Inline $e^{i\pi} + 1 = 0$, display:

$$
x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}
$$

An invalid one: $\frac{1}{$

## Structure
<details open>
<summary>An open section</summary>

Its content, with a nested one:

<details>
<summary>A closed section</summary>

Hidden.

</details>
</details>

<center>

Centered

</center>

<pre>
preformatted   text
</pre>

## Mermaid
```mermaid
flowchart TD
    subgraph Client
        UI[Web app] --> Cache[(Local cache)]
    end
    UI -->|login| Auth{Token?}
    Auth -. yes .-> UI
    Auth ==> Loop(Retry)
    Loop --> Loop
```

```mermaid
sequenceDiagram
    participant App
    participant MD as rich_md
    App->>MD: Render(text)
    MD->>MD: resolve transclusions
    loop each formula
        MD-->>App: bitmap
    end
    Note over App,MD: textures
```

```mermaid
classDiagram
    namespace Geometry {
        class Shape {
            <<interface>>
            +area() float
        }
    }
    Shape <|-- Circle : implements
    Canvas "1" *-- "many" Shape : owns
    Canvas o-- Renderer
    Renderer ..> Style
    Circle : -radius float
```

An invalid diagram:
```mermaid
flowchart LR
    A --> B
    this is not mermaid
```

A wikilink: [[Home]] and one with a label: [[Notes/todo|my notes]].

A transclusion that fails:
![[#NoSuchSection]]

---
)md";

// Contexts: the first one becomes current, DestroyContext() destroys the current one, and the former names
// (InitializeMarkdown, DeInitializeMarkdown) leave a context made with CreateContext alone
static bool CheckContexts()
{
    RichMd::Context* mine = RichMd::CreateContext();
    bool ok = RichMd::GetCurrentContext() == mine;
    RichMd::InitializeMarkdown();  // the default context, made current
    ok = ok && RichMd::GetCurrentContext() != mine;
    RichMd::DeInitializeMarkdown();  // destroys the default context
    RichMd::SetCurrentContext(mine);
    RichMd::DeInitializeMarkdown();  // no default context left: nothing to do
    ok = ok && RichMd::GetCurrentContext() == mine;
    RichMd::DestroyContext();  // the current one
    ok = ok && RichMd::GetCurrentContext() == nullptr;
    if (!ok)
        printf("smoke test failed: contexts\n");
    return ok;
}

// A running program shows the new version of its narrative: Render() resolves a text again when a file it
// transcludes changes
static bool CheckLiveStory()
{
    namespace fs = std::filesystem;
    fs::path file = fs::temp_directory_path() / "rich_md_smoke_test_live_story.md";
    std::ofstream(file, std::ios::binary) << "first version\n";
    const std::string embed = "![[" + file.generic_string() + "]]";
    RichMd::CreateContext();
    auto renderFrames = [&](int count) {
        for (int i = 0; i < count; ++i)
        {
            ImGui_ImplNull_NewFrame();
            ImGui::NewFrame();
            ImGui::Begin("Live story");
            RichMd::Render(embed);
            ImGui::End();
            ImGui::Render();
            ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
        }
    };
    auto resolved = [&]() { return RichMd::GetCurrentContext()->resolvedTransclusions.begin()->second.markdown; };

    renderFrames(1);
    bool ok = resolved().find("first version") != std::string::npos;
    std::ofstream(file, std::ios::binary) << "second version\n";
    fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(2));  // file times can be coarse
    renderFrames(40);  // the files are checked twice a second (40 frames of 1/60 s)
    ok = ok && resolved().find("second version") != std::string::npos;

    RichMd::DestroyContext();
    fs::remove(file);
    if (!ok)
        printf("smoke test failed: the narrative did not follow its file\n");
    return ok;
}

// An HTML comment is not shown: a document with comments draws as many vertices as the same document without them
static bool CheckHtmlComments()
{
    RichMd::CreateContext();
    auto vertexCount = [](const char* markdown) {
        int count = 0;
        for (int i = 0; i < 2; ++i)  // the first frame loads the fonts
        {
            ImGui_ImplNull_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowSize(ImVec2(600.f, 400.f));
            ImGui::Begin("Comments");
            RichMd::Render(markdown);
            ImGui::End();
            ImGui::Render();
            ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
            count = ImGui::GetDrawData()->TotalVtxCount;
        }
        return count;
    };
    int withComments = vertexCount("Before\n\n<!-- a comment,\non two lines -->\n\nAfter <!-- inline --> the end.\n");
    int withoutComments = vertexCount("Before\n\nAfter  the end.\n");
    RichMd::DestroyContext();
    bool ok = withComments == withoutComments;
    if (!ok)
        printf("smoke test failed: HTML comments are shown (%d vertices, %d without them)\n", withComments, withoutComments);
    return ok;
}

static int RunOneCycle()
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplNull_Init();
    if (!CheckContexts() || !CheckLiveStory() || !CheckHtmlComments())
        return 0;

    RichMd::MarkdownOptions options;
#ifdef IMGUI_RICHMD_WITH_LATEX
    options.withLatex = true;
#endif
    options.callbacks.OnWikiLink = [](const std::string&) {};
    RichMd::CreateContext(options);
    RichMd::RegisterFencedBlockRenderer("csv", [](const std::string& code) { ImGui::TextUnformatted(code.c_str()); });

    int vertexCount = 0;
    for (int frame = 0; frame < 10; ++frame)
    {
        ImGui_ImplNull_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(900.f, 700.f));
        ImGui::Begin("Document");
        RichMd::Render(kDocument);
        ImGui::End();
        ImGui::Render();
        ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
        vertexCount = ImGui::GetDrawData()->TotalVtxCount;
    }

    RichMd::DestroyContext();
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    return vertexCount;
}

int main(int, char**)
{
    for (int cycle = 1; cycle <= 2; ++cycle)
    {
        int vertexCount = RunOneCycle();
        printf("cycle %d: %d vertices in the last frame\n", cycle, vertexCount);
        if (vertexCount == 0)
        {
            printf("smoke test failed: nothing was drawn\n");
            return 1;
        }
    }
    printf("smoke test: ok\n");
    return 0;
}
