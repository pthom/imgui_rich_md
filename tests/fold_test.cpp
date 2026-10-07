// Fold test: the foldable headings (MarkdownOptions::foldableHeadings), through Dear ImGui's null backend. A click on
// the arrow in the margin folds a heading's section: the headings it hides are listed as hidden, at the folded
// heading's place, until a heading of its level or above; a heading inside a list does not end the fold, and does not
// fold; the "#" title does not fold (foldableHeadingsMinLevel). A link to a heading of a folded section unfolds it.
#include "imgui.h"
#include "imgui_internal.h"  // ImAbs
#include "imgui_impl_null.h"
#include "imgui_rich_md/rich_md.h"

#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

static const char* kMarkdown = R"([to alpha one](#alpha-one)

# Title

Intro.

## Alpha

Alpha text.

### Alpha one

Alpha one text.

- An item

  ## In a list

## Beta

Beta text.

## Gamma

Gamma text.
)";

struct Frame
{
    std::vector<RichMd::Heading> headings;
    float originY = 0.f;  // the top of the markdown, in the window's content coordinates
    ImVec2 textOrigin;    // the same, on the screen
    float height = 0.f;   // the height of the render
};

static Frame DrawFrame()
{
    Frame frame;
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(600.f, 800.f), ImGuiCond_Always);
    ImGui::Begin("Fold", nullptr, ImGuiWindowFlags_NoTitleBar);
    frame.originY = ImGui::GetCursorPosY();
    frame.textOrigin = ImGui::GetCursorScreenPos();
    RichMd::Render(kMarkdown);
    frame.headings = RichMd::LastRenderHeadings();
    frame.height = ImGui::GetCursorPosY() - frame.originY;
    ImGui::End();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    return frame;
}

static bool Expect(const char* what, bool condition)
{
    if (!condition)
        printf("fold test failed: %s\n", what);
    return condition;
}

static const RichMd::Heading* FindHeading(const Frame& f, const char* slug)
{
    for (const RichMd::Heading& h : f.headings)
        if (h.slug == slug)
            return &h;
    return nullptr;
}

// A click at a place of the screen (the release acts), then a few frames
static Frame Click(ImVec2 pos)
{
    ImGui::GetIO().AddMousePosEvent(pos.x, pos.y);
    DrawFrame();
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    DrawFrame();
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    Frame f;
    for (int i = 0; i < 4; ++i)
        f = DrawFrame();
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    return DrawFrame();
}

// A click on the margin at the left of a heading's first line, where its arrow is
static Frame ClickArrow(const Frame& f, const char* slug)
{
    const RichMd::Heading* h = FindHeading(f, slug);
    if (!h)
        return f;
    return Click(ImVec2(f.textOrigin.x + 4.f, f.textOrigin.y + (h->y - f.originY) + 4.f));
}

static bool ExpectHidden(const Frame& f, const char* slug, bool hidden)
{
    const RichMd::Heading* h = FindHeading(f, slug);
    bool ok = h != nullptr && h->hidden == hidden;
    if (!ok)
        printf("fold test failed: %s should be %s\n", slug, hidden ? "hidden" : "shown");
    return ok;
}

int main(int, char**)
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplNull_Init();
    RichMd::MarkdownOptions options;
    options.foldableHeadings = true;
    RichMd::CreateContext(options);
    DrawFrame();  // the first frame loads the fonts
    Frame open = DrawFrame();

    bool ok = Expect("six headings", open.headings.size() == 6);
    for (const char* slug : {"title", "alpha", "alpha-one", "in-a-list", "beta", "gamma"})
        ok = ExpectHidden(open, slug, false) && ok;

    // Alpha folds: its section hides "Alpha one" and the heading in the list, until "Beta" (the same level)
    Frame folded = ClickArrow(open, "alpha");
    ok = ExpectHidden(folded, "alpha", false) && ok;
    ok = ExpectHidden(folded, "alpha-one", true) && ok;
    ok = ExpectHidden(folded, "in-a-list", true) && ok;
    ok = ExpectHidden(folded, "beta", false) && ok;
    ok = ExpectHidden(folded, "gamma", false) && ok;
    const RichMd::Heading* alpha = FindHeading(folded, "alpha");
    const RichMd::Heading* alphaOne = FindHeading(folded, "alpha-one");
    ok = Expect("a hidden heading is at the folded heading's place", alpha && alphaOne && alphaOne->y == alpha->y) && ok;
    ok = Expect("the folded render is shorter", folded.height < open.height) && ok;
    ok = Expect("the fold holds at the next frame", DrawFrame().height == folded.height) && ok;

    // A second click unfolds it
    Frame unfolded = ClickArrow(folded, "alpha");
    ok = ExpectHidden(unfolded, "alpha-one", false) && ok;
    ok = Expect("the unfolded render has its height back", ImAbs(unfolded.height - open.height) <= 1.f) && ok;

    // A level 3 heading folds until the next heading of level 3 or above: "Beta"
    Frame folded3 = ClickArrow(unfolded, "alpha-one");
    ok = ExpectHidden(folded3, "alpha-one", false) && ok;
    ok = ExpectHidden(folded3, "in-a-list", true) && ok;
    ok = ExpectHidden(folded3, "beta", false) && ok;
    unfolded = ClickArrow(folded3, "alpha-one");
    ok = ExpectHidden(unfolded, "in-a-list", false) && ok;

    // No arrow: the "#" title (below foldableHeadingsMinLevel), and a heading inside a list
    Frame f = ClickArrow(unfolded, "title");
    ok = Expect("the title does not fold", ImAbs(f.height - open.height) <= 1.f) && ok;
    f = ClickArrow(f, "in-a-list");
    ok = Expect("a heading in a list does not fold", ImAbs(f.height - open.height) <= 1.f) && ok;

    // A link to a heading of a folded section unfolds it
    folded = ClickArrow(f, "alpha");
    ok = ExpectHidden(folded, "alpha-one", true) && ok;
    float lineHeight = RichMd::GetFont(RichMd::MarkdownFontSpec()).size;
    f = Click(ImVec2(folded.textOrigin.x + 30.f, folded.textOrigin.y + lineHeight * 0.5f));  // past the margin
    ok = ExpectHidden(f, "alpha-one", false) && ok;

    RichMd::DestroyContext();
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    printf(ok ? "fold test: ok\n" : "fold test: FAILED\n");
    return ok ? 0 : 1;
}
