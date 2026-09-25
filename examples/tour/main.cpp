// imgui_rich_md tour: every feature of the library, each with its markdown source.
// Stock Dear ImGui + GLFW + OpenGL3, see common/app_glfw_gl3.h.
#include "common/app_glfw_gl3.h"
#include "imgui_rich_md/rich_md.h"

#include <cstdio>
#include <string>
#include <vector>

// Filled by the OnHeading callback
static std::vector<std::string> gHeadings;

// The tour, in two literals (MSVC limits a single string literal to about 16 KB)
static const char* kTour = R"md(

# imgui_rich_md tour

`imgui_rich_md` renders markdown directly inside a Dear ImGui window: no browser,
no HTML, no external renderer.

> [!TIP]
> Use this tour as a reference when writing markdown in your own application.
> Expand the *"Show source"* sections to get copyable snippets.

---

# Basics

<details open>
<summary>Text and typography</summary>

All the usual inline styling works:

- *emphasis*, **bold**, ***both***, ~~strikethrough~~, <u>underlined</u>
- <mark>highlighted passages</mark> for things that need to stand out
- Inline `code`, handy for tokens, flags, and short snippets

HTML-like spans render natively too, no callbacks needed:

- Keyboard shortcuts read naturally: press <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd>, or <kbd>Cmd</kbd>+<kbd>K</kbd> on a Mac
- Chemistry: H<sub>2</sub>O, CO<sub>2</sub>, C<sub>8</sub>H<sub>10</sub>N<sub>4</sub>O<sub>2</sub>
- Exponents: x<sup>2</sup> + y<sup>2</sup> = r<sup>2</sup>

For any HTML span not in the default set, wire `MarkdownCallbacks::OnHtmlSpan`.

<details>
<summary>Show source</summary>

```
*emphasis*, **bold**, ***both***, ~~strikethrough~~, <u>underlined</u>
<mark>highlighted</mark>, inline `code`
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd>
H<sub>2</sub>O, x<sup>2</sup> + y<sup>2</sup> = r<sup>2</sup>
```

</details>
</details>
<details>
<summary>Structure: headers, lists, quotes, rules</summary>

Markdown handles the bones of a document out of the box.

**Headers** go from `#` (H1) to `###` (H3); all render as ImGui-styled headings.

<details>
<summary>Test Headers</summary>

# Title 1

A quick intro in normal text.

## Title 2

### Title 3

#### Title 4

##### Title 5

</details>

**Ordered and unordered lists**, including nesting:

1. First
2. Second
    - Nested bullet
    - Another one
        1. Deeper still
3. Third

**Blockquotes** for callouts that aren't admonitions:

> Markdown inside an ImGui window: the best of both worlds.

**Horizontal rules** to separate sections (three dashes on a line):

---

<details>
<summary>Show source</summary>

```
# H1
## H2
### H3

1. First
2. Second
    - Nested bullet
        1. Deeper still

> A blockquote.

---
```

</details>
</details>
<details>
<summary>Links and autolinks</summary>

Explicit markdown link syntax: [imgui_rich_md](https://github.com/pthom/imgui_rich_md).

Autolinks turn bare URLs, `www.` hosts, and email addresses into clickable
links automatically:

- https://github.com/pthom/imgui_rich_md
- www.dearimgui.org
- Contact: pthomet@gmail.com

Disable with `MarkdownOptions::autolinks = false` for strict CommonMark
behavior.

<details>
<summary>Show source</summary>

```
[imgui_rich_md](https://github.com/pthom/imgui_rich_md)

https://github.com/pthom/imgui_rich_md
www.dearimgui.org
Contact: pthomet@gmail.com
```

</details>
</details>
<details>
<summary>Images</summary>

Images load from local assets with a relative path:

![World](images/world.png)

Remote URLs load asynchronously (a spinner is shown while downloading):

![Photo](https://picsum.photos/id/1018/300/200)

Use `<img>` when you need to control the size:

<img src="https://picsum.photos/id/237/300/200" width="100">

> *Remote images need the library built with `IMGUI_RICHMD_WITH_DOWNLOAD_IMAGES` (libcurl on desktop,
> the browser's fetch with Emscripten), or a download function in `HostServices`.*

<details>
<summary>Show source</summary>

```
![World](images/world.png)
![Photo](https://picsum.photos/id/1018/300/200)
<img src="https://picsum.photos/id/237/300/200" width="100">
```

</details>
</details>

# Tables and code blocks


<details>
<summary>Code blocks</summary>

**Inline snippets**

Inline snippets are rendered like code inside a regular paragraph, like this: `result = 37`.

They are written like this:
<pre>
`result = 37`
</pre>

**Code blocks**

Code blocks preserve layout and use a monospaced font.

A small Python example:

```python
from imgui_bundle import imgui, rich_md

def gui():
    imgui.text("Hello, World!")
    rich_md.render("Hello, *markdown*!")
```

And its C++ equivalent:

```cpp
#include "imgui.h"
#include "imgui_rich_md/rich_md.h"

void Gui()
{
    ImGui::Text("Hello, World!");
    RichMd::Render("Hello, *markdown*!");
}
```

Code blocks get a copy button, and syntax highlighting when the library is built with its code
editor (`IMGUI_RICHMD_WITH_CODE_EDITOR`, see `RichMd::HasCodeEditor()`); otherwise they are plain
monospaced blocks.

Code blocks are delimited by three backticks, plus an optional language. See example below:

<pre>
```python
int main()
{
    return 0;
}
```
</pre>

</details>
<details>
<summary>Tables</summary>

Columns are resizable at runtime: grab a column border and drag. First-row
widths drive column layout, so use `&nbsp;` in that row to enforce a
minimum width where needed.

Column alignment is controlled by colons in the separator row:

```
:---    left
---:    right
:---:   centre
```

| Continent      |   Population  | Countries |
|----------------|--------------:|:---------:|
| Africa         | 1300 million  |    54     |
| Asia           | 4500 million  |    48     |
| Europe         |  743 million  |    44     |
| North America  |  579 million  |    23     |
| Oceania        |   41 million  |    14     |
| South America  |  422 million  |    12     |
| Antarctica     |           0   |     0     |

<details>
<summary>Show source</summary>

```
| Continent      |   Population  | Countries |
|----------------|--------------:|:---------:|
| Africa         | 1300 million  |    54     |
| Asia           | 4500 million  |    48     |
```

</details>
</details>

)md"
R"md(# Useful extensions

<details>
<summary>GitHub-style admonitions</summary>

Blockquotes that start with `[!NOTE]`, `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]`
or `[!CAUTION]` render as coloured callouts, great for in-app help and
onboarding:

> [!NOTE]
> A note provides useful context that a reader should know.

> [!TIP]
> A small hint that saves the reader time.

> [!IMPORTANT]
> Required information to complete a task.

> [!WARNING]
> Heads up: something can subtly go wrong here.

> [!CAUTION]
> A negative outcome is likely without care.

<details>
<summary>Show source</summary>

```
> [!NOTE]
> A note provides useful context that a reader should know.

> [!WARNING]
> Heads up: something can subtly go wrong here.
```

</details>
</details>
<details>
<summary>Task lists</summary>

GitHub-style task lists render as checkbox glyphs. Handy for changelogs
and roadmap-style content embedded inside your app:

- [x] Design the UI
- [x] Wire up the data layer
- [x] Write the onboarding flow
- [ ] Polish documentation
- [ ] Record a demo video
- [ ] Ship it

<details>
<summary>Show source</summary>

```
- [x] Done item
- [ ] Pending item
```

</details>
</details>
<details>
<summary>Math with LaTeX</summary>

Requires the library built with `IMGUI_RICHMD_WITH_LATEX`, and `MarkdownOptions::withLatex = true`.
Rendering is powered by [MicroTeX](https://github.com/NanoMichael/MicroTeX).

**Inline math** uses single dollars: Euler's identity $e^{i\pi} + 1 = 0$
is a consequence of the more general $e^{i\theta} = \cos\theta + i\sin\theta$.

**Display math** uses double dollars on their own line. The quadratic
formula:

$$
x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}
$$

Sums, integrals and matrices all work:

$$
\int_{-\infty}^{\infty} e^{-x^2}\, dx = \sqrt{\pi}
\qquad
A = \begin{pmatrix} a & b \\ c & d \end{pmatrix}
\qquad
\sum_{i=0}^{n} i = \frac{n(n+1)}{2}
$$

<details>
<summary>Show source</summary>

```
Inline: $e^{i\pi} + 1 = 0$

Display:
$$
x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}
$$
```

</details>
</details>
<details>
<summary>Centered blocks</summary>

`<center>` centers a block. As with `<div>` and `<details>`, put the tags on their own
lines, with blank lines around them, so that the content inside is parsed as markdown:

<center>

**Centered**, with *markdown* inside: $E = mc^2$

</center>

<details>
<summary>Show source</summary>

```
<center>

**Centered**, with *markdown* inside: $E = mc^2$

</center>
```

</details>
</details>
<details>
<summary>Custom fenced blocks</summary>

A fenced block whose language you registered is rendered by your own function instead of
the code renderer: tables from `csv`, diagrams, live widgets... This demo registers `csv`:

```csv
name,score,rank
Alice,10,1
Bob,7,2
Carol,4,3
```

<details>
<summary>Show source</summary>

```cpp
void RenderCsv(const std::string& code)
{
    std::vector<std::vector<std::string>> rows = SplitCsv(code);  // one vector per line
    if (ImGui::BeginTable("csv", (int)rows[0].size(), ImGuiTableFlags_Borders))
    {
        for (const auto& row : rows)
        {
            ImGui::TableNextRow();
            for (const auto& cell : row)
            {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(cell.c_str());
            }
        }
        ImGui::EndTable();
    }
}

RichMd::RegisterFencedBlockRenderer("csv", RenderCsv);
```

Then in the markdown:
<pre>
```csv
name,score
Alice,10
```
</pre>

</details>
</details>
<details>
<summary>Mermaid diagrams</summary>

` ```mermaid ` blocks are drawn natively, with `ImDrawList` and the colors of the ImGui style (no web
view, no JavaScript): flowcharts, sequence diagrams and class diagrams. A diagram that cannot be parsed
shows as code, with the line of the error. `RichMd::RenderMermaid(source)` draws one outside of markdown.
More diagrams, to edit, in the [Mermaid tour](https://pthom.github.io/imgui_rich_md/mermaid.html).

```mermaid
flowchart LR
    A[Markdown] --> B{Mermaid block?}
    B -->|yes| C([Parse]) --> D[Layout] --> E[(ImDrawList)]
    B -->|no| F[Code block]
```

```mermaid
sequenceDiagram
    participant App
    participant MD as imgui_rich_md
    App->>+MD: Render(markdown)
    MD->>MD: parse, lay out
    MD-->>-App: drawn
```

```mermaid
classDiagram
    class Diagram {
        +kind
        +error
    }
    class Graph {
        +nodes
        +edges
    }
    class Sequence {
        +participants
        +rows
    }
    Diagram *-- Graph
    Diagram *-- Sequence
```

<details>
<summary>Show source</summary>

<pre>
```mermaid
flowchart LR
    A[Markdown] --> B{Mermaid block?}
    B -->|yes| C([Parse]) --> D[Layout] --> E[(ImDrawList)]
    B -->|no| F[Code block]
```
</pre>

</details>
</details>
<details>
<summary>Wikilinks and hard line breaks (options)</summary>

Two features are enabled through `MarkdownOptions`, i.e. before the first render:

- **Wikilinks**: `[[target]]` and `[[target|label]]` become links, and clicking one calls
  `callbacks.OnWikiLink(target)`: navigation between notes, in-app pages, etc.
  *(Enabled in this tour: the wikilink below is clickable, see the console.)*
- **Hard line breaks**: with `hardSoftBreaks = true`, a newline in the source is a line
  break (as in GitHub comments and chat messages) instead of a space. It applies to the whole
  document, so it is not enabled here.

A wikilink to [[Home]] and one with a label: [[Notes/todo|my todo list]].

<details>
<summary>Show source</summary>

```cpp
RichMd::MarkdownOptions options;
options.callbacks.OnWikiLink = [](const std::string& target) { printf("go to %s\n", target.c_str()); };
options.hardSoftBreaks = true;   // for chat-like text
RichMd::CreateContext(options);
```

```
A wikilink to [[Home]] and one with a label: [[Notes/todo|my todo list]].
```

</details>
</details>
<details>
<summary>Headings callback</summary>

`callbacks.OnHeading(level, text)` is called after each heading is rendered: build a
table of contents, scroll to an anchor, track the section under the mouse...

@@HEADINGS_STATUS@@

<details>
<summary>Show source</summary>

```cpp
std::vector<std::string> toc;
options.callbacks.OnHeading = [&](int level, const std::string& text) {
    toc.push_back(std::string(2 * (level - 1), ' ') + text);
};
```

</details>
</details>
<details>
<summary>Icons, emoji and other fonts</summary>

Any font can be merged into all the markdown fonts with `fontOptions.mergeFonts`: an icon
font such as FontAwesome (its glyphs then work in every style: regular, bold, italic, code), an
emoji font, a CJK font (Dear ImGui loads glyphs on demand, so a large font costs nothing until
it is used)... The library ships none of them.

<details>
<summary>Show source</summary>

```cpp
RichMd::MarkdownOptions options;
options.fontOptions.mergeFonts = {"fonts/fontawesome-webfont.ttf", "fonts/NotoEmoji-Regular.ttf"};
RichMd::CreateContext(options);

RichMd::Render("Launch " ICON_FA_ROCKET);   // ICON_FA_ROCKET: from the icon font's header
```

</details>
</details>
<details>
<summary>Preformatted text with the pre tag</summary>

`<pre>` renders a block of monospaced text **without** the styling of a
fenced code block (no background frame, no syntax coloring). Use it
for ASCII layouts, aligned data, and anything where "monospace" is
what you want but "this is source code" is not the right message:

<pre>
Metric        Aligned        Value
--------      ---------      -----
Ping          right           12ms
Throughput    right          42MB/s
Latency       right           87us
</pre>

Same content in a fenced code block, for comparison:

```
Metric        Aligned        Value
--------      ---------      -----
Ping          right           12ms
```

<details>
<summary>Show source</summary>

```
<pre>
First line
    Indented line
Last line
</pre>
```

</details>
</details>

---

# Under the hood: how this page is built

<details>
<summary>What this build supports</summary>

@@SUPPORT_STATUS@@

`RichMd::HasLatex()`, `HasUrlImages()` and `HasCodeEditor()` tell what the library was
built with and what the host provides.

</details>
<details>
<summary>Rendering and fonts</summary>

- `RichMd::Render(text)` removes the common indentation first, so that a markdown string
  written inside an indented function renders as expected (`RenderRaw` renders as is).
- The markdown fonts are loaded at the first render: `CreateContext()` can be called
  any time after the ImGui context exists.
- Each `Render()` call is a fragment with its own id scope: render prose between widgets,
  the same fragment twice, and nothing collides.

</details>

<details>
<summary>It's collapsibles all the way down</summary>

Every section above, and every "Show source" inside them, is a
`<details>` block. The tags render as `CollapsingHeader` widgets,
and blank lines around the opening / closing tags let the inner
content be parsed as regular markdown:

<details>
<summary>A nested collapsible</summary>

Hidden until you click. The content is regular markdown.

- One
- Two

<details>
<summary>Going deeper</summary>

Another level. Indentation doesn't matter; what matters is the blank
lines around the tags.

</details>
</details>

<details>
<summary>Show source</summary>

```
<details>
<summary>Click me</summary>

Hidden content (regular markdown here).

</details>
```

</details>
</details>

---

# Using it from Python

`imgui_rich_md` is included in [Dear ImGui Bundle](https://github.com/pthom/imgui_bundle)
(`pip install imgui-bundle`), as `imgui_bundle.rich_md`, with the same features:

```python
from imgui_bundle import immapp, rich_md

def gui():
    rich_md.render("# Hello, *markdown*")

immapp.run(gui, with_markdown=True)   # with_latex=True for the formulas
```

Try it in your browser, without installing anything: [the Dear ImGui Bundle playground](https://imgui-bundle.pages.dev/playground/).
)md";

static std::vector<std::vector<std::string>> SplitCsv(const std::string& code)
{
    std::vector<std::vector<std::string>> rows;
    size_t lineStart = 0;
    while (lineStart < code.size())
    {
        size_t lineEnd = code.find('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = code.size();
        std::string line = code.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;
        if (line.empty())
            continue;
        std::vector<std::string> cells;
        size_t cellStart = 0;
        while (true)
        {
            size_t comma = line.find(',', cellStart);
            cells.push_back(line.substr(cellStart, comma == std::string::npos ? std::string::npos : comma - cellStart));
            if (comma == std::string::npos)
                break;
            cellStart = comma + 1;
        }
        rows.push_back(cells);
    }
    return rows;
}

// Renders a ```csv fenced block as a table (see RegisterFencedBlockRenderer below)
static void RenderCsv(const std::string& code)
{
    auto rows = SplitCsv(code);
    if (rows.empty())
        return;
    if (ImGui::BeginTable("csv", (int)rows[0].size(), ImGuiTableFlags_Borders))
    {
        for (const auto& row : rows)
        {
            ImGui::TableNextRow();
            for (const auto& cell : row)
            {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(cell.c_str());
            }
        }
        ImGui::EndTable();
    }
}

static std::string ReplaceAll(std::string text, const std::string& from, const std::string& to)
{
    for (size_t pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size()))
        text.replace(pos, from.size(), to);
    return text;
}

static std::string FillDynamicParts(const std::string& markdown, const std::vector<std::string>& headings)
{
    std::string headingsStatus = "This run collects the headings of this page: ";
    for (size_t i = 0; i < headings.size() && i < 6; ++i)
        headingsStatus += (i > 0 ? ", `" : "`") + headings[i].substr(headings[i].find_first_not_of(' ')) + "`";
    if (headings.size() > 6)
        headingsStatus += "...";
    std::string supportStatus = std::string("This build: LaTeX **") + (RichMd::HasLatex() ? "yes" : "no")
        + "**, URL images **" + (RichMd::HasUrlImages() ? "yes" : "no")
        + "**, code editor **" + (RichMd::HasCodeEditor() ? "yes" : "no") + "**.";
    std::string r = ReplaceAll(markdown, "@@HEADINGS_STATUS@@", headingsStatus);
    return ReplaceAll(r, "@@SUPPORT_STATUS@@", supportStatus);
}

static void Gui()
{
    static bool csvRendererRegistered = false;
    if (!csvRendererRegistered)  // the markdown context exists once the app runs
    {
        RichMd::RegisterFencedBlockRenderer("csv", RenderCsv);
        csvRendererRegistered = true;
    }
    // The headings rendered during the previous frame are listed in this one
    std::vector<std::string> headingsLastFrame;
    std::swap(headingsLastFrame, gHeadings);
    RichMd::Render(FillDynamicParts(kTour, headingsLastFrame));
}

int main(int, char**)
{
    RichMd::MarkdownOptions options;
#ifdef IMGUI_RICHMD_WITH_LATEX
    options.withLatex = true;
#endif
    options.callbacks.OnWikiLink = [](const std::string& target) { printf("wikilink clicked: %s\n", target.c_str()); };
    options.callbacks.OnHeading = [](int level, const std::string& text) {
        gHeadings.push_back(std::string(2 * (level - 1), ' ') + text);
    };
    return RunApp("imgui_rich_md tour", ImVec2(900, 900), options, Gui);
}
