// Fold test: the foldable headings (MarkdownOptions::foldableHeadings), through Dear ImGui's null backend. A click on
// the arrow in the margin folds a heading's section: the headings it hides are listed as hidden, at the folded
// heading's place, until a heading of its level or above; a heading inside a list does not end the fold, and does not
// fold; the "#" title does not fold (foldableHeadingsMinLevel). A link to a heading of a folded section unfolds it.
// FoldAllHeadings() folds them all, and unfolds them all, also those inside a folded section. In a document, a fold
// goes on across its renders and its sections of widgets: DocumentHeading() returns false for a hidden section, and
// a jump of the search to a match in a folded section unfolds what hides it. A document folds also when it asks for it
// (DocumentOptions::foldableHeadings), in a context whose option is off.
#include "imgui.h"
#include "imgui_internal.h"  // ImAbs
#include "imgui_impl_null.h"
#include "imgui_rich_md/rich_md.h"
#include "imgui_rich_md/internal/rich_md_internal.h"  // the state of a document (its search)

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

static int gFoldAll = 0;  // FoldAllHeadings() after the render of the next frame: 1 folds, -1 unfolds
static bool gDocumentFoldable = false;  // DocumentOptions::foldableHeadings of the document

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
    if (gFoldAll != 0) {
        RichMd::FoldAllHeadings(gFoldAll > 0);
        gFoldAll = 0;
    }
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

// A document: two renders, a section of widgets inside the second one's last section, a third render
struct DocFrame
{
    std::vector<RichMd::Heading> first;  // the headings of the first render
    ImVec2 origin;                       // the top left of the content (past the margin), on the screen
    float originY = 0.f;                 // the same, in the content's coordinates
    float secondHeight = 0.f;            // the height of the second render
    bool widgetsShown = false;           // what DocumentHeading() returned
};

static DocFrame DrawDocument(int foldAll = 0)
{
    DocFrame frame;
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(900.f, 800.f), ImGuiCond_Always);
    ImGui::Begin("Document window", nullptr, ImGuiWindowFlags_NoTitleBar);
    RichMd::DocumentOptions options;
    options.toc = false;
    options.foldableHeadings = gDocumentFoldable;
    RichMd::BeginDocument("doc", ImVec2(0.f, 0.f), options);
    frame.origin = ImGui::GetCursorScreenPos();
    frame.originY = ImGui::GetCursorPosY();
    RichMd::Render("## One\n\nOne text.\n\n### One a\n\nOne a text.");
    frame.first = RichMd::LastRenderHeadings();
    float y = ImGui::GetCursorPosY();
    RichMd::Render("Still in One a.\n\n## Two\n\nTwo text.");
    frame.secondHeight = ImGui::GetCursorPosY() - y;
    frame.widgetsShown = RichMd::DocumentHeading(3, "Widgets", false);
    if (frame.widgetsShown)
        ImGui::Button("A widget");
    RichMd::Render("## Three\n\nThree text.");
    if (foldAll != 0)
        RichMd::FoldAllHeadings(foldAll > 0);
    RichMd::EndDocument();
    ImGui::End();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    return frame;
}

static DocFrame ClickDocumentArrow(const DocFrame& f, const char* slug)
{
    for (const RichMd::Heading& h : f.first)
        if (h.slug == slug) {
            ImVec2 pos(f.origin.x - 6.f, f.origin.y + (h.y - f.originY) + 4.f);  // in the margin
            ImGui::GetIO().AddMousePosEvent(pos.x, pos.y);
            DrawDocument();
            ImGui::GetIO().AddMouseButtonEvent(0, true);
            DrawDocument();
            ImGui::GetIO().AddMouseButtonEvent(0, false);
            DrawDocument();
            ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        }
    DrawDocument();
    return DrawDocument();
}

static bool CheckDocument()
{
    DrawDocument();
    DocFrame open = DrawDocument();
    bool ok = Expect("a document: its widgets shown", open.widgetsShown);
    ok = Expect("a document: two headings in its first render", open.first.size() == 2) && ok;

    // "One a" folds: the fold goes on into the second render, until "Two"
    DocFrame folded = ClickDocumentArrow(open, "one-a");
    ok = Expect("a fold goes on into the next render", folded.secondHeight < open.secondHeight) && ok;
    ok = Expect("a fold of the render before does not hide the widgets after Two", folded.widgetsShown) && ok;
    open = ClickDocumentArrow(folded, "one-a");
    ok = Expect("unfolded, the next render has its height back", ImAbs(open.secondHeight - folded.secondHeight) > 1.f)
         && ok;

    // Fold all: "Two" folds, its section hides the widgets; unfold all shows them again
    DrawDocument(1);
    DocFrame all = DrawDocument();
    ok = Expect("fold all: DocumentHeading() returns false", !all.widgetsShown) && ok;
    DrawDocument(-1);
    all = DrawDocument();
    ok = Expect("unfold all: DocumentHeading() returns true", all.widgetsShown) && ok;

    // The search: after fold all, "Still" is hidden by the fold of "One" (and inside it, of "One a"); a jump to it
    // unfolds both
    DrawDocument(1);
    DocFrame hidden = DrawDocument();
    RichMd::DocumentState& state =
        RichMd::GetCurrentContext()->documents[ImGui::FindWindowByName("Document window")->GetID("doc")];
    state.searchOpen = true;
    ImStrncpy(state.query, "Still", sizeof(state.query));
    DrawDocument();
    DrawDocument();
    ok = Expect("fold all hides One a", hidden.first.size() == 2 && hidden.first[1].hidden) && ok;
    ok = Expect("the search finds the folded text", state.matchCount == 1) && ok;
    state.step = 1;
    DocFrame shown;
    for (int i = 0; i < 30; ++i)
        shown = DrawDocument();
    ok = Expect("a jump to a folded match unfolds its sections",
                shown.first.size() == 2 && !shown.first[1].hidden && shown.secondHeight > hidden.secondHeight) && ok;
    state.searchOpen = false;
    DrawDocument(-1);
    DrawDocument();
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
    ok = Expect("a hidden heading is at the folded heading's place", alpha && alphaOne && alphaOne->y == alpha->y)
         && ok;
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

    // Fold all: the sections of Alpha, Beta, Gamma; "Alpha one", inside Alpha, folds too (not drawn: no arrow)
    gFoldAll = 1;
    DrawFrame();
    f = DrawFrame();
    for (const char* slug : {"title", "alpha", "beta", "gamma"})
        ok = ExpectHidden(f, slug, false) && ok;
    ok = ExpectHidden(f, "alpha-one", true) && ok;
    ok = ExpectHidden(f, "in-a-list", true) && ok;
    // Alpha unfolds alone: "Alpha one" is folded inside it
    f = ClickArrow(f, "alpha");
    ok = ExpectHidden(f, "alpha-one", false) && ok;
    ok = ExpectHidden(f, "in-a-list", true) && ok;
    // Unfold all, also what a folded section hid: fold Alpha again, then unfold all
    f = ClickArrow(f, "alpha");
    gFoldAll = -1;
    DrawFrame();
    f = DrawFrame();
    for (const char* slug : {"title", "alpha", "alpha-one", "in-a-list", "beta", "gamma"})
        ok = ExpectHidden(f, slug, false) && ok;
    ok = Expect("unfold all gives the whole height back", ImAbs(f.height - open.height) <= 1.f) && ok;

    ok = CheckDocument() && ok;

    // A context without the option: the document asks for the folds
    RichMd::DestroyContext();
    RichMd::CreateContext(RichMd::MarkdownOptions());
    gDocumentFoldable = true;
    ok = CheckDocument() && ok;
    DrawFrame();
    const float plainHeight = DrawFrame().height;
    const float clickedHeight = ClickArrow(DrawFrame(), "alpha").height;
    ok = Expect("without the option, a plain render does not fold", clickedHeight == plainHeight) && ok;

    RichMd::DestroyContext();
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    printf(ok ? "fold test: ok\n" : "fold test: FAILED\n");
    return ok ? 0 : 1;
}
