// Headings test: the headings of a render (LastRenderHeadings), through Dear ImGui's null backend: their text, their
// slugs as GitHub makes them, their places in the window's content (the scroll does not change them, a narrower window
// moves those below a wrapped paragraph), and a heading inside a collapsed <details>. Then the anchor links: a click on
// [text](#slug) scrolls the window to the heading, opening the collapsed section that hides it; an unknown anchor, or
// one to another render, goes to OnOpenLink.
#include "imgui.h"
#include "imgui_internal.h"  // ImAbs
#include "imgui_impl_null.h"
#include "imgui_rich_md/rich_md.h"

#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

static const char* kMarkdown = R"(# Getting started

A paragraph long enough to wrap in a narrow window: one two three four five six seven eight nine ten eleven twelve.

## Install & run (v2.0)

### The `Render()` call

## Install & run (v2.0)

<details>
<summary>More</summary>

## Hidden *section*

</details>

## Après la fin

)";

static std::string gOpenedLink;

struct Frame
{
    std::vector<RichMd::Heading> headings;
    float originY = 0.f;  // the top of the markdown, in the window's content coordinates
    ImVec2 textOrigin;    // the same, on the screen
    float scrollY = 0.f;
};

// One frame: the markdown in a window of the given width; scrollY >= 0 scrolls the window there. A second markdown,
// when given, is a second render below the first.
static Frame DrawFrame(const char* window, const std::string& markdown, float width, float scrollY = -1.f,
                       const std::string& secondMarkdown = "")
{
    Frame frame;
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, 150.f), ImGuiCond_Always);
    if (scrollY >= 0.f)
        ImGui::SetNextWindowScroll(ImVec2(-1.f, scrollY));
    ImGui::Begin(window, nullptr, ImGuiWindowFlags_NoTitleBar);
    frame.scrollY = ImGui::GetScrollY();
    frame.originY = ImGui::GetCursorPosY();
    frame.textOrigin = ImGui::GetCursorScreenPos();
    RichMd::Render(markdown);
    frame.headings = RichMd::LastRenderHeadings();
    if (!secondMarkdown.empty()) {
        RichMd::Render(secondMarkdown);
        frame.headings = RichMd::LastRenderHeadings();
    }
    ImGui::End();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    return frame;
}

static Frame DrawFrame(float width, float scrollY = -1.f) { return DrawFrame("Headings", kMarkdown, width, scrollY); }

static bool Expect(const char* what, bool condition)
{
    if (!condition)
        printf("headings test failed: %s\n", what);
    return condition;
}

static bool ExpectHeading(const RichMd::Heading& h, int level, const char* text, const char* slug, bool hidden)
{
    bool ok = h.level == level && h.text == text && h.slug == slug && h.hidden == hidden;
    if (!ok)
        printf("headings test failed: expected (%d, \"%s\", \"%s\", %s), got (%d, \"%s\", \"%s\", %s)\n", level, text,
               slug, hidden ? "hidden" : "shown", h.level, h.text.c_str(), h.slug.c_str(), h.hidden ? "hidden" : "shown");
    return ok;
}

// The text without markup (inline code included), GitHub's slugs: punctuation dropped, a repeated title numbered,
// the non-ASCII letters kept
static bool CheckTextsAndSlugs(const Frame& f)
{
    if (!Expect("six headings", f.headings.size() == 6))
        return false;
    bool ok = ExpectHeading(f.headings[0], 1, "Getting started", "getting-started", false);
    ok = ExpectHeading(f.headings[1], 2, "Install & run (v2.0)", "install--run-v20", false) && ok;
    ok = ExpectHeading(f.headings[2], 3, "The Render() call", "the-render-call", false) && ok;
    ok = ExpectHeading(f.headings[3], 2, "Install & run (v2.0)", "install--run-v20-1", false) && ok;
    ok = ExpectHeading(f.headings[4], 2, "Hidden section", "hidden-section", true) && ok;
    ok = ExpectHeading(f.headings[5], 2, "Après la fin", "après-la-fin", false) && ok;
    return ok;
}

// The first heading is at the top of the render; the others follow in order (the hidden one at its section's header,
// below the heading before it and above the one after it)
static bool CheckPlaces(const Frame& f)
{
    bool ok = Expect("the first heading at the top of the render", f.headings[0].y == f.originY);
    for (size_t i = 1; i < f.headings.size(); ++i)
        ok = Expect("the headings in order, down the page", f.headings[i].y > f.headings[i - 1].y) && ok;
    return ok;
}

static const RichMd::Heading* FindHeading(const Frame& f, const char* slug)
{
    for (const RichMd::Heading& h : f.headings)
        if (h.slug == slug)
            return &h;
    return nullptr;
}

// A document whose first line is a link, with a collapsed section and a heading far below, and lines after it (so that
// the window can bring it to the top)
static std::string AnchorDocument(const char* firstLine)
{
    std::string md = std::string(firstLine) + "\n\n";
    for (int i = 0; i < 20; ++i)
        md += "Line " + std::to_string(i) + "\n\n";
    md += "<details>\n<summary>More</summary>\n\n## Hidden section\n\nInside.\n\n</details>\n\n";
    for (int i = 20; i < 30; ++i)
        md += "Line " + std::to_string(i) + "\n\n";
    md += "## The target\n\n";
    for (int i = 30; i < 60; ++i)
        md += "Line " + std::to_string(i) + "\n\n";
    return md;
}

// A click on the link of the first line (it opens on the release), then a few frames; returns the last one
static Frame ClickFirstLine(const char* window, const std::string& md, const std::string& secondMd = "")
{
    Frame f = DrawFrame(window, md, 600.f, -1.f, secondMd);
    float lineHeight = RichMd::GetFont(RichMd::MarkdownFontSpec()).size;
    ImVec2 pos(f.textOrigin.x + 5.f, f.textOrigin.y + lineHeight * 0.5f);
    ImGui::GetIO().AddMousePosEvent(pos.x, pos.y);
    DrawFrame(window, md, 600.f, -1.f, secondMd);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    DrawFrame(window, md, 600.f, -1.f, secondMd);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    for (int i = 0; i < 8; ++i)
        f = DrawFrame(window, md, 600.f, -1.f, secondMd);
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    return f;
}

static bool CheckAnchorLinks()
{
    // A heading far below: the window scrolls it to the top
    gOpenedLink.clear();
    std::string md = AnchorDocument("[to the target](#the-target)");
    Frame f = ClickFirstLine("Anchor", md);
    const RichMd::Heading* target = FindHeading(f, "the-target");
    bool ok = Expect("the target exists", target != nullptr);
    ok = ok && Expect("the window scrolls the target to the top", ImAbs(f.scrollY - target->y) <= 1.f);
    ok = Expect("an anchor of the render does not reach OnOpenLink", gOpenedLink.empty()) && ok;

    // A heading inside a collapsed section: the section opens, then the window scrolls to the heading
    md = AnchorDocument("[to the hidden section](#hidden-section)");
    f = ClickFirstLine("Anchor into a collapsed section", md);
    target = FindHeading(f, "hidden-section");
    ok = Expect("the hidden section exists", target != nullptr) && ok;
    ok = ok && Expect("the collapsed section opened", !target->hidden);
    ok = ok && Expect("the window scrolls the heading of the section to the top", ImAbs(f.scrollY - target->y) <= 1.f);

    // An anchor that no heading of the render has, and a link to a site: OnOpenLink
    gOpenedLink.clear();
    f = ClickFirstLine("Unknown anchor", AnchorDocument("[nowhere](#nowhere)"));
    ok = Expect("an unknown anchor goes to OnOpenLink", gOpenedLink == "#nowhere") && ok;
    ok = Expect("an unknown anchor does not scroll", f.scrollY == 0.f) && ok;
    gOpenedLink.clear();
    ClickFirstLine("Site", AnchorDocument("[a site](https://example.com)"));
    ok = Expect("a link to a site goes to OnOpenLink", gOpenedLink == "https://example.com") && ok;

    // An anchor to a heading of another render (the documents will reach it): OnOpenLink, no scroll
    gOpenedLink.clear();
    f = ClickFirstLine("Two renders", "[to the target](#the-target)", AnchorDocument("The second render"));
    ok = Expect("an anchor to another render goes to OnOpenLink", gOpenedLink == "#the-target") && ok;
    ok = Expect("an anchor to another render does not scroll", f.scrollY == 0.f) && ok;
    return ok;
}

int main(int, char**)
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplNull_Init();
    RichMd::MarkdownOptions options;
    options.callbacks.OnOpenLink = [](const std::string& url) { gOpenedLink = url; };
    RichMd::CreateContext(options);
    DrawFrame(600.f);  // the first frame loads the fonts
    DrawFrame(600.f);

    Frame wide = DrawFrame(600.f);
    bool ok = CheckTextsAndSlugs(wide);
    ok = ok && CheckPlaces(wide);

    // The scroll does not change the places (content coordinates), to a pixel: ImGui truncates the cursor in screen
    // coordinates, so a fractional gap (above a heading) lands a pixel lower or higher when the window is scrolled
    Frame scrolled = DrawFrame(600.f, 60.f);
    DrawFrame(600.f, 60.f);
    scrolled = DrawFrame(600.f, 60.f);
    for (size_t i = 0; ok && i < wide.headings.size(); ++i)
        ok = Expect("the same places when the window is scrolled",
                    ImAbs(scrolled.headings[i].y - wide.headings[i].y) <= 1.f) && ok;

    // A narrower window wraps the paragraph on more lines: the first heading stays, the next ones move down
    DrawFrame(250.f);
    Frame narrow = DrawFrame(250.f);
    ok = ok && Expect("the same headings in a narrow window", narrow.headings.size() == wide.headings.size());
    ok = ok && Expect("the first heading stays at the top", narrow.headings[0].y == wide.headings[0].y);
    ok = ok && Expect("the headings below the paragraph move down", narrow.headings[1].y > wide.headings[1].y);

    ok = CheckAnchorLinks() && ok;

    // An empty render has no headings (none left from the render before)
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    ImGui::Begin("Empty");
    RichMd::Render("");
    ok = Expect("an empty render has no headings", RichMd::LastRenderHeadings().empty()) && ok;
    ImGui::End();
    ImGui::Render();

    RichMd::DestroyContext();
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    printf(ok ? "headings test: ok\n" : "headings test: FAILED\n");
    return ok ? 0 : 1;
}
