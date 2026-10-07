// Headings test: the headings of a render (LastRenderHeadings), through Dear ImGui's null backend: their text, their
// slugs as GitHub makes them, their places in the window's content (the scroll does not change them, a narrower window
// moves those below a wrapped paragraph), and a heading inside a collapsed <details>.
#include "imgui.h"
#include "imgui_internal.h"  // ImAbs
#include "imgui_impl_null.h"
#include "imgui_rich_md/rich_md.h"

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

struct Frame
{
    std::vector<RichMd::Heading> headings;
    float originY = 0.f;  // the top of the markdown, in the window's content coordinates
};

// One frame: the markdown in a window of the given width; scrollY >= 0 scrolls the window there
static Frame DrawFrame(float width, float scrollY = -1.f)
{
    Frame frame;
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, 150.f), ImGuiCond_Always);
    if (scrollY >= 0.f)
        ImGui::SetNextWindowScroll(ImVec2(-1.f, scrollY));
    ImGui::Begin("Headings", nullptr, ImGuiWindowFlags_NoTitleBar);
    frame.originY = ImGui::GetCursorPosY();
    RichMd::Render(kMarkdown);
    frame.headings = RichMd::LastRenderHeadings();
    ImGui::End();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    return frame;
}

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

int main(int, char**)
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplNull_Init();
    RichMd::CreateContext();
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
