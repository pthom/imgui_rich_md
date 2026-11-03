// Scroll anchoring test: in a document, the block at the top of the view keeps its place on the screen when the content
// above it changes height, as in a web browser. Through Dear ImGui's null backend: a jump to a heading, then a picture
// above it that loads later and grows (the heading stays at the top); the same with DocumentOptions::scrollAnchoring
// off (the heading goes down); the reader's wheel moves the view, and the new top block then keeps its place; at the
// top of the content, the view stays at the top.
#include "imgui.h"
#include "imgui_internal.h"  // ImAbs
#include "imgui_impl_null.h"
#include "imgui_rich_md/rich_md.h"

#include <cfloat>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>

static float gPictureHeight = 20.f;  // the picture's height, as the image callback gives it

static std::string Lines(int from, int to)
{
    std::string md;
    for (int i = from; i < to; ++i)
        md += "Line " + std::to_string(i) + "\n\n";
    return md;
}

// A link to a heading, a picture above the heading, and lines after it (so that the view can bring it to the top)
static const std::string kMarkdown =
    "[to the target](#the-target)\n\n" + Lines(0, 5) + "![a picture](picture.png)\n\n" + Lines(5, 15)
    + "## The target\n\n" + Lines(15, 60);

struct View
{
    ImVec2 contentTopLeft;   // the top left of the content, on the screen (it moves with the scroll)
    ImVec2 windowPos;        // the content's window, on the screen
    float scrollY = 0.f;     // the content's scroll
    float targetY = -1.f;    // the target heading, in the content's coordinates
};

// One frame: the document, in a window
static View DrawFrame(const char* id, const RichMd::DocumentOptions& options)
{
    View view;
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(600.f, 300.f), ImGuiCond_Always);
    ImGui::Begin("Document", nullptr, ImGuiWindowFlags_NoTitleBar);
    RichMd::BeginDocument(id, ImVec2(0.f, 200.f), options);
    view.contentTopLeft = ImGui::GetCursorScreenPos();
    view.windowPos = ImGui::GetWindowPos();
    view.scrollY = ImGui::GetScrollY();
    RichMd::Render(kMarkdown);
    for (const RichMd::Heading& h : RichMd::LastRenderHeadings())
        if (h.slug == "the-target")
            view.targetY = h.y;
    RichMd::EndDocument();
    ImGui::End();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    return view;
}

static View DrawFrames(const char* id, const RichMd::DocumentOptions& options, int frames)
{
    View view;
    for (int i = 0; i < frames; ++i)
        view = DrawFrame(id, options);
    return view;
}

static bool Expect(const char* what, bool condition)
{
    if (!condition)
        printf("scroll anchor test failed: %s\n", what);
    return condition;
}

// The target heading's distance below the top of the view
static float TargetOnScreen(const View& v) { return v.targetY - v.scrollY; }

// A click on the link of the first line, then the frames of the jump to the heading
static View JumpToTarget(const char* id, const RichMd::DocumentOptions& options)
{
    View view = DrawFrames(id, options, 2);
    float lineHeight = RichMd::GetFont(RichMd::MarkdownFontSpec()).size;
    ImGui::GetIO().AddMousePosEvent(view.contentTopLeft.x + 5.f, view.contentTopLeft.y + lineHeight * 0.5f);
    DrawFrame(id, options);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    DrawFrame(id, options);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    DrawFrames(id, options, 20);
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    return DrawFrames(id, options, 2);
}

// The picture loads, then grows over several frames (as a picture whose size comes with its download)
static View GrowPicture(const char* id, const RichMd::DocumentOptions& options)
{
    View view;
    for (int i = 0; i < 10; ++i) {
        gPictureHeight += 30.f;
        view = DrawFrames(id, options, 3);
    }
    return DrawFrames(id, options, 5);
}

static bool CheckJumpThenPictureGrows()
{
    RichMd::DocumentOptions options;
    options.scrollAnimation = RichMd::ScrollAnimation::Never;
    gPictureHeight = 20.f;
    View view = JumpToTarget("anchored", options);
    bool ok = Expect("the jump brings the heading to the top",
                     view.scrollY > 0.f && ImAbs(TargetOnScreen(view)) <= 1.f);
    const float targetBefore = view.targetY;
    view = GrowPicture("anchored", options);
    ok = Expect("the picture pushed the heading down the content", view.targetY > targetBefore + 250.f) && ok;
    ok = Expect("the heading stays at the top of the view", ImAbs(TargetOnScreen(view)) <= 1.f) && ok;
    if (!ok)
        printf("  the heading is %.1f px below the top of the view\n", TargetOnScreen(view));
    return ok;
}

static bool CheckAnchoringOff()
{
    RichMd::DocumentOptions options;
    options.scrollAnimation = RichMd::ScrollAnimation::Never;
    options.scrollAnchoring = false;
    gPictureHeight = 20.f;
    View view = JumpToTarget("not anchored", options);
    bool ok = Expect("anchoring off: the jump brings the heading to the top", ImAbs(TargetOnScreen(view)) <= 1.f);
    view = GrowPicture("not anchored", options);
    ok = Expect("anchoring off: the picture pushes the heading down the view", TargetOnScreen(view) > 250.f) && ok;
    return ok;
}

// The reader scrolls with the wheel: the view follows, and a change above then keeps the new top block in its place
static bool CheckReaderScrolls()
{
    RichMd::DocumentOptions options;
    options.scrollAnimation = RichMd::ScrollAnimation::Never;
    gPictureHeight = 20.f;
    View view = JumpToTarget("wheel", options);
    const float scrollBefore = view.scrollY;
    ImGui::GetIO().AddMousePosEvent(view.windowPos.x + 50.f, view.windowPos.y + 50.f);
    DrawFrame("wheel", options);
    ImGui::GetIO().AddMouseWheelEvent(0.f, -2.f);  // down
    view = DrawFrames("wheel", options, 5);
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    bool ok = Expect("the wheel scrolls the view", view.scrollY > scrollBefore + 10.f);
    const float onScreen = TargetOnScreen(view);
    ok = Expect("the wheel moved the heading above the top", onScreen < -10.f) && ok;
    view = GrowPicture("wheel", options);
    ok = Expect("after the wheel, the view keeps its place", ImAbs(TargetOnScreen(view) - onScreen) <= 1.f) && ok;
    return ok;
}

// At the top of the content, nothing is anchored: the reader sees the start of the document, the picture grows below it
static bool CheckAtTheTop()
{
    RichMd::DocumentOptions options;
    gPictureHeight = 20.f;
    View view = DrawFrames("top", options, 2);
    const float targetBefore = view.targetY;
    view = GrowPicture("top", options);
    bool ok = Expect("at the top: the view stays at the top", view.scrollY == 0.f);
    ok = Expect("at the top: the picture pushes the heading down", view.targetY > targetBefore + 250.f) && ok;
    return ok;
}

int main(int, char**)
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplNull_Init();
    RichMd::MarkdownOptions options;
    options.callbacks.OnImage = [](const std::string&) -> std::optional<RichMd::MarkdownImage> {
        RichMd::MarkdownImage image;
        image.texture_id = (ImTextureID)1;
        image.size = ImVec2(100.f, gPictureHeight);
        image.uv0 = ImVec2(0.f, 0.f);
        image.uv1 = ImVec2(1.f, 1.f);
        image.col_tint = ImVec4(1.f, 1.f, 1.f, 1.f);
        image.col_border = ImVec4(0.f, 0.f, 0.f, 0.f);
        return image;
    };
    RichMd::CreateContext(options);
    DrawFrames("warm up", RichMd::DocumentOptions(), 1);  // the first frame loads the fonts

    bool ok = CheckJumpThenPictureGrows();
    ok = CheckAnchoringOff() && ok;
    ok = CheckReaderScrolls() && ok;
    ok = CheckAtTheTop() && ok;

    RichMd::DestroyContext();
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    printf(ok ? "scroll anchor test: ok\n" : "scroll anchor test: FAILED\n");
    return ok ? 0 : 1;
}
