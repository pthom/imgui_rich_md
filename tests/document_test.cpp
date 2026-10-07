// Document test: BeginDocument() / EndDocument(), through Dear ImGui's null backend. The renders of a document share
// their headings: the slugs are unique in the document, a link in one render reaches a heading of another, and
// DocumentHeading() gives a section of widgets its heading. RenderDocument() is the one-call form. The user errors
// (a document inside a document, an EndDocument() without its BeginDocument(), a document left open) are reported.
#include "imgui.h"
#include "imgui_internal.h"  // ImAbs, ImGuiContext::ErrorCountCurrentFrame, the child windows
#include "imgui_impl_null.h"
#include "imgui_rich_md/rich_md.h"

#include <cfloat>
#include <cstdio>
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

// A click on the first line of a text drawn at topLeft (a link opens on the release), then a few frames
static void ClickFirstLine(ImVec2 topLeft, const std::function<void()>& content)
{
    float lineHeight = RichMd::GetFont(RichMd::MarkdownFontSpec()).size;
    ImGui::GetIO().AddMousePosEvent(topLeft.x + 5.f, topLeft.y + lineHeight * 0.5f);
    DrawFrame(content);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    DrawFrame(content);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    for (int i = 0; i < 8; ++i)
        DrawFrame(content);
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    DrawFrame(content);
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
    ImVec2 topLeft;
    float scrollY = 0.f, endY = -1.f;
    auto content = [&]() {
        topLeft = ImGui::GetCursorScreenPos();
        RichMd::RenderDocument("one call", md, ImVec2(0.f, 200.f));
        for (const RichMd::Heading& h : RichMd::LastRenderHeadings())
            if (h.slug == "the-end")
                endY = h.y;
        scrollY = ImGui::GetCurrentWindow()->DC.ChildWindows.back()->Scroll.y;
    };
    DrawFrame(content);
    ClickFirstLine(topLeft, content);
    return Expect("RenderDocument(): a link scrolls to its heading", scrollY > 0.f && ImAbs(scrollY - endY) <= 1.f);
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
    ImGui_ImplNull_Init();
    RichMd::MarkdownOptions options;
    options.callbacks.OnOpenLink = [](const std::string& url) { gOpenedLink = url; };
    RichMd::CreateContext(options);
    DrawFrame([]() { RichMd::Render("warm up"); });  // the first frame loads the fonts

    bool ok = CheckMainDocument();
    ok = CheckDrawnTitle() && ok;
    ok = CheckUnknownAnchor() && ok;
    ok = CheckRenderDocument() && ok;
    ok = CheckUserErrors() && ok;

    RichMd::DestroyContext();
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    printf(ok ? "document test: ok\n" : "document test: FAILED\n");
    return ok ? 0 : 1;
}
