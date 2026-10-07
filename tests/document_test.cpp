// Document test: BeginDocument() / EndDocument(), through Dear ImGui's null backend. The renders of a document share
// their headings: the slugs are unique in the document, a link in one render reaches a heading of another, and
// DocumentHeading() gives a section of widgets its heading. RenderDocument() is the one-call form. The table of
// contents: shown from tocMinHeadings headings, a click on an entry scrolls to its heading, a button hides it (a line
// then shows a button that brings it back). The search: Ctrl+F opens the find bar, a query finds its matches in the
// document, Enter, Shift+Enter and the arrows go through them (the scroll follows, animated), Escape closes the bar and
// keeps the query. The user errors (a document inside a document, an EndDocument() without its BeginDocument(), a
// document left open) are reported.
#include "imgui.h"
#include "imgui_internal.h"  // ImAbs, ImGuiContext::ErrorCountCurrentFrame, the child windows
#include "imgui_impl_null.h"
#include "imgui_rich_md/rich_md.h"
#include "imgui_rich_md/internal/rich_md_internal.h"  // the state of a document

#include <cfloat>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

static std::string gOpenedLink;

static std::string Lines(int from, int to)
{
    std::string md;
    for (int i = from; i < to; ++i)
        md += "Line " + std::to_string(i) + "\n\n";
    return md;
}

// The main document: a render, a section of widgets, a second render
static const std::string kRenderA = "[to the widgets](#try-it)\n\n## Intro\n\n" + Lines(0, 20);
static const std::string kRenderB = "[back to the intro](#intro)\n\n" + Lines(20, 30) + "## Intro\n\n" + Lines(30, 60);

struct MainDocument
{
    float scrollY = 0.f;
    ImVec2 renderA, renderB;  // the top left of the renders, on the screen
    float tryItY = 0.f;       // the place of the section of widgets, in the content coordinates
    std::vector<RichMd::Heading> headingsA, headingsB;
};
static MainDocument gMain;

static void DrawMainDocument()
{
    RichMd::BeginDocument("main", ImVec2(0.f, 200.f));
    gMain.scrollY = ImGui::GetScrollY();
    gMain.renderA = ImGui::GetCursorScreenPos();
    RichMd::Render(kRenderA);
    gMain.headingsA = RichMd::LastRenderHeadings();
    gMain.tryItY = ImGui::GetCursorPosY();
    RichMd::DocumentHeading(2, "Try it", false);
    ImGui::Button("A widget", ImVec2(200.f, 100.f));
    gMain.renderB = ImGui::GetCursorScreenPos();
    RichMd::Render(kRenderB);
    gMain.headingsB = RichMd::LastRenderHeadings();
    RichMd::EndDocument();
}

// One frame: a window that draws the given content
static void DrawFrame(const std::function<void()>& content)
{
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(600.f, 300.f), ImGuiCond_Always);
    ImGui::Begin("Document", nullptr, ImGuiWindowFlags_NoTitleBar);
    content();
    ImGui::End();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
}

// The frames a scroll of the document takes: its animation (0.3 s, at 60 frames per second), then its corrections
static const int kScrollFrames = 40;

// A click on the first line of a text drawn at topLeft (a link opens on the release), then the frames of the scroll
static void ClickFirstLine(ImVec2 topLeft, const std::function<void()>& content)
{
    float lineHeight = RichMd::GetFont(RichMd::MarkdownFontSpec()).size;
    ImGui::GetIO().AddMousePosEvent(topLeft.x + 5.f, topLeft.y + lineHeight * 0.5f);
    DrawFrame(content);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    DrawFrame(content);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    for (int i = 0; i < kScrollFrames; ++i)
        DrawFrame(content);
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    DrawFrame(content);
}

// The windows of a document: the document's own child window (its name holds "/<id>_"), its content and its table of
// contents (the children it drew in the last frame)
static ImGuiWindow* ChildNamed(ImGuiWindow* parent, const char* part)
{
    if (parent)
        for (ImGuiWindow* w : parent->DC.ChildWindows)
            if (strstr(w->Name, part) && w->Active)
                return w;
    return nullptr;
}
static ImGuiWindow* DocumentWindow(const char* id)
{
    return ChildNamed(ImGui::FindWindowByName("Document"), (std::string("/") + id + "_").c_str());
}

static bool Expect(const char* what, bool condition)
{
    if (!condition)
        printf("document test failed: %s\n", what);
    return condition;
}

// The slugs are unique in the document; a link in a render reaches the section of widgets, and a link in the second
// render a heading of the first
static bool CheckMainDocument()
{
    DrawFrame(DrawMainDocument);
    DrawFrame(DrawMainDocument);
    bool ok = Expect("the heading of the first render", gMain.headingsA.size() == 1 && gMain.headingsA[0].slug == "intro");
    ok = Expect("the repeated heading of the second render, numbered in the document",
                gMain.headingsB.size() == 1 && gMain.headingsB[0].slug == "intro-1") && ok;

    ClickFirstLine(gMain.renderA, DrawMainDocument);
    ok = Expect("a link of the first render scrolls to the section of widgets",
                ImAbs(gMain.scrollY - gMain.tryItY) <= 1.f) && ok;

    ClickFirstLine(gMain.renderB, DrawMainDocument);
    ok = Expect("a link of the second render scrolls back to a heading of the first",
                gMain.scrollY > 0.f && ImAbs(gMain.scrollY - gMain.headingsA[0].y) <= 1.f) && ok;
    ok = Expect("the anchors of the document do not reach OnOpenLink", gOpenedLink.empty()) && ok;
    return ok;
}

// A title drawn by DocumentHeading(): a markdown heading, its text as it is (the punctuation escaped)
static bool CheckDrawnTitle()
{
    std::vector<RichMd::Heading> headings;
    DrawFrame([&headings]() {
        RichMd::BeginDocument("titles", ImVec2(0.f, 200.f));
        RichMd::DocumentHeading(2, "A *star* [x]", true);
        headings = RichMd::LastRenderHeadings();
        RichMd::EndDocument();
    });
    return Expect("a drawn title, with its text as it is",
                  headings.size() == 1 && headings[0].level == 2 && headings[0].text == "A *star* [x]"
                  && headings[0].slug == "a-star-x");
}

// An anchor that no heading of the document has goes to OnOpenLink
static bool CheckUnknownAnchor()
{
    ImVec2 topLeft;
    auto content = [&topLeft]() {
        RichMd::BeginDocument("unknown", ImVec2(0.f, 200.f));
        topLeft = ImGui::GetCursorScreenPos();
        RichMd::Render("[nowhere](#nowhere)\n\n## Somewhere\n");
        RichMd::EndDocument();
    };
    gOpenedLink.clear();
    DrawFrame(content);
    ClickFirstLine(topLeft, content);
    bool ok = Expect("an unknown anchor of a document goes to OnOpenLink", gOpenedLink == "#nowhere");
    gOpenedLink.clear();
    return ok;
}

// The one-call form: a link to a heading below scrolls the document to it
static bool CheckRenderDocument()
{
    const std::string md = "[to the end](#the-end)\n\n" + Lines(0, 30) + "## The end\n\n" + Lines(30, 60);
    float scrollY = 0.f, endY = -1.f;
    auto content = [&]() {
        RichMd::RenderDocument("one call", md, ImVec2(0.f, 200.f));
        for (const RichMd::Heading& h : RichMd::LastRenderHeadings())
            if (h.slug == "the-end")
                endY = h.y;
        scrollY = ChildNamed(ChildNamed(ImGui::GetCurrentWindow(), "/one call_"), "##content")->Scroll.y;
    };
    DrawFrame(content);
    ImVec2 topLeft = ChildNamed(DocumentWindow("one call"), "##content")->DC.CursorStartPos;  // inside its padding
    ClickFirstLine(topLeft, content);
    return Expect("RenderDocument(): a link scrolls to its heading", scrollY > 0.f && ImAbs(scrollY - endY) <= 1.f);
}

// A click at a place of the screen, then the frames of the scroll
static void ClickAt(ImVec2 pos, const std::function<void()>& content)
{
    ImGui::GetIO().AddMousePosEvent(pos.x, pos.y);
    DrawFrame(content);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    DrawFrame(content);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    for (int i = 0; i < kScrollFrames; ++i)
        DrawFrame(content);
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    DrawFrame(content);
}

// The center of the k-th entry of n in the table of contents: the rows end at the bottom of its content
static ImVec2 TocEntry(ImGuiWindow* toc, int k, int n)
{
    float lineHeight = RichMd::GetFont(RichMd::MarkdownFontSpec()).size;
    float pitch = lineHeight + ImGui::GetStyle().ItemSpacing.y;
    float y = toc->DC.CursorMaxPos.y - (float)(n - 1 - k) * pitch - lineHeight * 0.5f;
    return ImVec2(toc->Pos.x + toc->Size.x * 0.5f, y);
}

static bool CheckTableOfContents()
{
    // The main document has three headings: its table of contents shows from the second frame (the first one counts
    // them); a click on the second entry (the section of widgets) scrolls to it, a click on the first back to the top
    DrawFrame(DrawMainDocument);
    DrawFrame(DrawMainDocument);
    ImGuiWindow* toc = ChildNamed(DocumentWindow("main"), "##toc");
    bool ok = Expect("a document with three headings shows its table of contents", toc != nullptr);
    if (!ok)
        return false;
    ClickAt(TocEntry(toc, 1, 3), DrawMainDocument);
    ok = Expect("a click on an entry scrolls to its heading", ImAbs(gMain.scrollY - gMain.tryItY) <= 1.f) && ok;
    ClickAt(TocEntry(toc, 2, 3), DrawMainDocument);
    ok = Expect("a click on the last entry scrolls to it",
                ImAbs(gMain.scrollY - gMain.headingsB[0].y) <= 1.f || gMain.scrollY == DocumentWindow("main")
                    ->DC.ChildWindows[0]->ScrollMax.y) && ok;

    // The button at the top left of the panel hides it; the line above the content then shows a button that brings
    // it back
    const float half = ImGui::GetFrameHeight() * 0.5f;
    ImVec2 hide(toc->DC.CursorStartPos.x + half, toc->DC.CursorStartPos.y + half);
    ClickAt(hide, DrawMainDocument);
    ok = Expect("the hide button hides the table of contents", ChildNamed(DocumentWindow("main"), "##toc") == nullptr)
         && ok;
    ImGuiWindow* document = DocumentWindow("main");
    ImVec2 show(document->DC.CursorStartPos.x + half, document->DC.CursorStartPos.y + half);
    ClickAt(show, DrawMainDocument);
    ok = Expect("the line's button shows it again", ChildNamed(DocumentWindow("main"), "##toc") != nullptr) && ok;

    // A narrow document: the line above the content instead of the panel, without the button that shows the panel
    auto narrowDocument = []() {
        RichMd::RenderDocument("narrow", "## One\n\n## Two\n\n## Three\n", ImVec2(ImGui::GetFontSize() * 30.f, 200.f));
    };
    DrawFrame(narrowDocument);
    DrawFrame(narrowDocument);
    ImGuiWindow* narrow = DocumentWindow("narrow");
    ImGuiWindow* narrowContent = ChildNamed(narrow, "##content");
    ok = Expect("a narrow document has no panel", ChildNamed(narrow, "##toc") == nullptr) && ok;
    ok = Expect("a narrow document has the line above its content",
                narrowContent && narrowContent->Pos.y >= narrow->DC.CursorStartPos.y + ImGui::GetFrameHeight()) && ok;
    // The line starts with the menu of the sections: no button to show the panel at its left
    ImGui::GetIO().AddMousePosEvent(narrow->DC.CursorStartPos.x + half, narrow->DC.CursorStartPos.y + half);
    DrawFrame(narrowDocument);
    DrawFrame(narrowDocument);
    ok = Expect("the line of a narrow document starts with the menu of the sections",
                GImGui->HoveredIdPreviousFrame == DocumentWindow("narrow")->GetID("##section")) && ok;
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);

    // Two headings (and one of level 4, beyond tocMaxLevel): no table of contents
    auto fewHeadings = []() { RichMd::RenderDocument("few", "## One\n\n## Two\n\n#### Four\n", ImVec2(0.f, 200.f)); };
    DrawFrame(fewHeadings);
    DrawFrame(fewHeadings);
    ok = Expect("two headings: no table of contents", ChildNamed(DocumentWindow("few"), "##toc") == nullptr) && ok;
    return ok;
}

// A key pressed (with its modifiers), then a number of frames
static void PressKey(ImGuiKeyChord chord, const std::function<void()>& content, int frames = kScrollFrames)
{
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiKey key = (ImGuiKey)(chord & ~ImGuiMod_Mask_);
    const ImGuiKey mods[] = {ImGuiMod_Ctrl, ImGuiMod_Shift};
    for (ImGuiKey mod : mods)
        if (chord & mod)
            io.AddKeyEvent(mod, true);
    io.AddKeyEvent(key, true);
    DrawFrame(content);
    io.AddKeyEvent(key, false);
    for (ImGuiKey mod : mods)
        if (chord & mod)
            io.AddKeyEvent(mod, false);
    for (int i = 0; i < frames; ++i)
        DrawFrame(content);
}

// The state rich_md keeps for a document of the window "Document"
static RichMd::DocumentState& DocumentStateOf(const char* id)
{
    return RichMd::GetCurrentContext()->documents[ImGui::FindWindowByName("Document")->GetID(id)];
}

static bool CheckSearch()
{
    // "Line 4" is in "Line 4" and "Line 40" to "Line 49"
    const std::string md = Lines(0, 60);
    auto content = [&md]() { RichMd::RenderDocument("search", md, ImVec2(0.f, 200.f)); };
    auto scrollY = []() { return ChildNamed(DocumentWindow("search"), "##content")->Scroll.y; };
    DrawFrame(content);
    DrawFrame(content);
    RichMd::DocumentState& state = DocumentStateOf("search");

    ClickAt(ChildNamed(DocumentWindow("search"), "##content")->DC.CursorStartPos, content);  // the focus
    PressKey(ImGuiMod_Ctrl | ImGuiKey_F, content);
    bool ok = Expect("Ctrl+F opens the find bar", state.searchOpen);
    ImGui::GetIO().AddInputCharactersUTF8("Line 4");
    for (int i = 0; i < 3; ++i)
        DrawFrame(content);
    ok = Expect("the query finds its matches in the document", state.matchCount == 11) && ok;
    ok = Expect("the first match below the top of the view is current, and visible: no scroll",
                state.currentMatch == 0 && scrollY() == 0.f) && ok;

    // Enter: the next match ("Line 40"), out of the view: the scroll goes to it, animated
    PressKey(ImGuiKey_Enter, content, 4);
    const float during = scrollY();
    for (int i = 0; i < kScrollFrames; ++i)
        DrawFrame(content);
    const float after = scrollY();
    ok = Expect("Enter goes to the next match", state.currentMatch == 1) && ok;
    ok = Expect("the scroll reaches it, animated", during > 0.f && during < after) && ok;
    PressKey(ImGuiMod_Shift | ImGuiKey_Enter, content);
    ok = Expect("Shift+Enter goes back to the previous match", state.currentMatch == 0 && scrollY() < after) && ok;
    PressKey(ImGuiKey_UpArrow, content);
    ok = Expect("the up arrow before the first match wraps to the last", state.currentMatch == 10) && ok;
    PressKey(ImGuiKey_DownArrow, content);
    ok = Expect("the down arrow after the last match wraps to the first", state.currentMatch == 0) && ok;

    state.searchOptions.wholeWords = true;
    DrawFrame(content);
    ok = Expect("whole words: \"Line 40\" does not match \"Line 4\"", state.matchCount == 1) && ok;
    state.searchOptions.wholeWords = false;

    PressKey(ImGuiKey_Escape, content);
    ok = Expect("Escape closes the find bar", !state.searchOpen) && ok;

    // Ctrl+F again: the field selects the query, a new one replaces it; Escape keeps it for the next search
    PressKey(ImGuiMod_Ctrl | ImGuiKey_F, content);
    ImGui::GetIO().AddInputCharactersUTF8("Line 5");
    DrawFrame(content);
    PressKey(ImGuiKey_Escape, content);
    ok = Expect("the query stays for the next search", std::string(state.query) == "Line 5") && ok;
    return ok;
}

// The user errors are reported, and the next document works
static bool CheckUserErrors()
{
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigErrorRecoveryEnableAssert = false;
    io.ConfigErrorRecoveryEnableDebugLog = false;

    DrawFrame([]() {
        RichMd::BeginDocument("outer", ImVec2(0.f, 200.f));
        RichMd::BeginDocument("inner", ImVec2(0.f, 100.f));  // one document at a time
        RichMd::Render("## Inner\n");
        RichMd::EndDocument();
        RichMd::EndDocument();
    });
    bool ok = Expect("a document inside a document is reported", GImGui->ErrorCountCurrentFrame == 1);

    DrawFrame([]() { RichMd::EndDocument(); });
    ok = Expect("an EndDocument() without its BeginDocument() is reported", GImGui->ErrorCountCurrentFrame == 1) && ok;

    DrawFrame([]() {
        RichMd::BeginDocument("left open", ImVec2(0.f, 200.f));
        RichMd::Render("## Left open\n");
        ImGui::EndChild();  // the application closed the child window, not the document
    });
    std::vector<RichMd::Heading> headings;
    DrawFrame([&headings]() {
        RichMd::RenderDocument("next", "## Next\n");
        headings = RichMd::LastRenderHeadings();
    });
    ok = Expect("a document left open is reported at the next one", GImGui->ErrorCountCurrentFrame >= 1) && ok;
    ok = Expect("the next document works", headings.size() == 1 && headings[0].slug == "next") && ok;

    io.ConfigErrorRecoveryEnableAssert = true;
    io.ConfigErrorRecoveryEnableDebugLog = true;
    return ok;
}

int main(int, char**)
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigMacOSXBehaviors = false;  // on macOS, ImGui reads the Ctrl key sent by the tests as Cmd
    ImGui_ImplNull_Init();
    RichMd::MarkdownOptions options;
    options.callbacks.OnOpenLink = [](const std::string& url) { gOpenedLink = url; };
    RichMd::CreateContext(options);
    DrawFrame([]() { RichMd::Render("warm up"); });  // the first frame loads the fonts

    bool ok = CheckMainDocument();
    ok = CheckDrawnTitle() && ok;
    ok = CheckUnknownAnchor() && ok;
    ok = CheckRenderDocument() && ok;
    ok = CheckTableOfContents() && ok;
    ok = CheckSearch() && ok;
    ok = CheckUserErrors() && ok;

    RichMd::DestroyContext();
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    printf(ok ? "document test: ok\n" : "document test: FAILED\n");
    return ok ? 0 : 1;
}
