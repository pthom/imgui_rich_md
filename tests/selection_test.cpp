// Selection test: text is selected with the mouse and copied with Ctrl+C, through Dear ImGui's null backend (the mouse
// and the keys are injected, the clipboard is a string of the test). Build it in Debug: a selection is drawn with a
// draw list splitter, which asserts when it is misused.
#include "imgui.h"
#include "imgui_internal.h"  // ImGuiContext::ErrorCountCurrentFrame
#include "imgui_impl_null.h"
#include "imgui_rich_md/rich_md.h"

#include <cstdio>
#include <string>

static std::string gClipboard;
static std::string gOpenedLink;
static int gPushSelectable = -1;  // -1: no PushSelectableText() around the render, else its value
static bool gForgetPop = false;   // a PushSelectableText() without its PopSelectableText()

// Where a frame drew the markdown
struct Frame
{
    ImVec2 windowPos;
    ImVec2 textOrigin;  // the top left of the markdown
    float textEndY;     // the cursor after it
};

// One frame: the markdown in a window of the given width; a movable window is placed only once
static Frame DrawFrame(const char* markdown, float width, bool movable = false)
{
    Frame frame{};
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), movable ? ImGuiCond_FirstUseEver : ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, 400.f));
    ImGui::Begin("Selection", nullptr, ImGuiWindowFlags_NoTitleBar);
    frame.windowPos = ImGui::GetWindowPos();
    frame.textOrigin = ImGui::GetCursorScreenPos();
    if (gPushSelectable >= 0)
        RichMd::PushSelectableText(gPushSelectable == 1);
    RichMd::Render(markdown);
    if (gPushSelectable >= 0 && !gForgetPop)
        RichMd::PopSelectableText();
    frame.textEndY = ImGui::GetCursorScreenPos().y;
    ImGui::End();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    return frame;
}

static void MouseTo(ImVec2 pos) { ImGui::GetIO().AddMousePosEvent(pos.x, pos.y); }
static void MouseButton(bool down) { ImGui::GetIO().AddMouseButtonEvent(0, down); }
static void CtrlC(bool down)
{
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, down);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_C, down);
}

// A drag from one position to another (one input change per frame), then Ctrl+C
static Frame DragAndCopy(const char* markdown, float width, ImVec2 from, ImVec2 to, bool movable = false)
{
    MouseTo(from);
    DrawFrame(markdown, width, movable);
    MouseButton(true);
    DrawFrame(markdown, width, movable);
    MouseTo(to);
    DrawFrame(markdown, width, movable);
    MouseButton(false);
    DrawFrame(markdown, width, movable);
    CtrlC(true);
    DrawFrame(markdown, width, movable);
    CtrlC(false);
    return DrawFrame(markdown, width, movable);
}

static float TextWidth(const char* text, bool bold = false)
{
    RichMd::SizedFont font = RichMd::GetFont(RichMd::MarkdownFontSpec(false, bold));
    return font.font->CalcTextSizeA(font.size, FLT_MAX, 0.0f, text).x;
}

static float LineHeight() { return RichMd::GetFont(RichMd::MarkdownFontSpec()).size; }

static bool Expect(const char* what, const std::string& actual, const std::string& expected)
{
    if (actual == expected)
        return true;
    printf("selection test failed: %s\n  expected: \"%s\"\n  actual:   \"%s\"\n", what, expected.c_str(),
           actual.c_str());
    return false;
}

// From inside a line to inside the next paragraph: the markup is not copied, a newline separates the paragraphs
static bool CheckPartialSelection()
{
    const char* md = "Hello **bold** world\n\nSecond paragraph\n";
    Frame f = DrawFrame(md, 600.f);
    float firstLineY = f.textOrigin.y + LineHeight() * 0.5f;
    float lastLineY = f.textEndY - ImGui::GetStyle().ItemSpacing.y - LineHeight() * 0.5f;
    gClipboard.clear();
    DragAndCopy(md, 600.f,
                ImVec2(f.textOrigin.x + TextWidth("Hello ") + 1.f, firstLineY),
                ImVec2(f.textOrigin.x + TextWidth("Sec") + 1.f, lastLineY));
    bool ok = Expect("partial selection", gClipboard, "bold world\nSec");
    // A drag from beside the end of a line (as in a browser, it starts at the end of the line)
    DragAndCopy(md, 600.f, ImVec2(f.textOrigin.x + 500.f, firstLineY),
                ImVec2(f.textOrigin.x + TextWidth("Hello ") + 1.f, firstLineY));
    return Expect("selection from beside a line", gClipboard, "bold world") && ok;
}

// Everything, in a narrow window where the paragraph wraps: a space at each wrap. The window does not move while
// selecting, and the selection survives a change of width (it is kept as offsets in the text).
static bool CheckWrappedSelection()
{
    const char* md = "one two three four five six seven eight nine ten eleven twelve\n";
    const std::string expected = "one two three four five six seven eight nine ten eleven twelve";
    Frame f = DrawFrame(md, 150.f, true);
    bool ok = true;
    if (f.textEndY - f.textOrigin.y < 3.f * LineHeight()) {
        printf("selection test failed: the paragraph does not wrap\n");
        ok = false;
    }
    gClipboard.clear();
    Frame after = DragAndCopy(md, 150.f,
                              ImVec2(f.textOrigin.x + 1.f, f.textOrigin.y + LineHeight() * 0.5f),
                              ImVec2(f.textOrigin.x + 100.f, f.textEndY + 50.f), true);
    ok = Expect("wrapped selection", gClipboard, expected) && ok;
    if (after.windowPos.x != f.windowPos.x || after.windowPos.y != f.windowPos.y) {
        printf("selection test failed: the window moved while selecting\n");
        ok = false;
    }
    gClipboard.clear();
    DrawFrame(md, 500.f, true);
    CtrlC(true);
    DrawFrame(md, 500.f, true);
    CtrlC(false);
    DrawFrame(md, 500.f, true);
    return Expect("selection after a relayout", gClipboard, expected) && ok;
}

// A hard line break, marked text, an aligned table and centered text (drawn with their own draw list splitters, or
// shifted after being drawn) under a selection: no assertion, and they are copied
static bool CheckSelectionOverSpecialBlocks()
{
    const char* md = "hard  \nbreak\n\n<mark>marked</mark> text\n\n| a | b |\n|---|--:|\n| 1 | 2 |\n\n"
                     "<center>\n\ncentered\n\n</center>\n\nend\n";
    Frame f = DrawFrame(md, 400.f);
    gClipboard.clear();
    DragAndCopy(md, 400.f, ImVec2(f.textOrigin.x + 1.f, f.textOrigin.y + LineHeight() * 0.5f),
                ImVec2(f.textOrigin.x + 300.f, f.textEndY + 50.f));
    for (int i = 0; i < 3; ++i)  // the selection is drawn during these frames
        DrawFrame(md, 400.f);
    return Expect("selection over special blocks", gClipboard, "hard\nbreak\nmarked text\na\nb\n1\n2\ncentered\nend");
}

// A drag on a widget drawn by a fenced block renderer moves the widget and selects nothing; a click beside the text
// clears a selection
static bool CheckWidgetsAndClear()
{
    static float value = 0.f;
    static ImVec2 sliderCenter;
    RichMd::RegisterFencedBlockRenderer("widget", [](const std::string&) {
        ImGui::SliderFloat("value", &value, 0.f, 1.f);
        sliderCenter = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f,
                              (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
    });
    const char* md = "Before\n\n```widget\nx\n```\n\nAfter\n";
    Frame f = DrawFrame(md, 400.f);
    gClipboard.clear();
    DragAndCopy(md, 400.f, sliderCenter, ImVec2(sliderCenter.x + 150.f, sliderCenter.y));
    bool ok = Expect("drag on a widget", gClipboard, "");
    if (value == 0.f) {
        printf("selection test failed: the slider did not move\n");
        ok = false;
    }
    // Select "Before", then click below the text: nothing is copied anymore
    DragAndCopy(md, 400.f, ImVec2(f.textOrigin.x + 1.f, f.textOrigin.y + LineHeight() * 0.5f),
                ImVec2(f.textOrigin.x + TextWidth("Before") + 5.f, f.textOrigin.y + LineHeight() * 0.5f));
    ok = Expect("selection before a clear", gClipboard, "Before") && ok;
    gClipboard.clear();
    MouseTo(ImVec2(f.textOrigin.x + 300.f, f.textEndY + 100.f));
    DrawFrame(md, 400.f);
    MouseButton(true);
    DrawFrame(md, 400.f);
    MouseButton(false);
    DrawFrame(md, 400.f);
    CtrlC(true);
    DrawFrame(md, 400.f);
    CtrlC(false);
    DrawFrame(md, 400.f);
    return Expect("after a click beside the text", gClipboard, "") && ok;
}

// A click on a link opens it when the mouse is released; a drag that starts on a link selects text and opens nothing
static bool CheckLinks()
{
    const char* md = "A [link](https://example.com) here\n";
    Frame f = DrawFrame(md, 400.f);
    ImVec2 onLink(f.textOrigin.x + TextWidth("A ") + TextWidth("link") * 0.5f, f.textOrigin.y + LineHeight() * 0.5f);
    gOpenedLink.clear();
    MouseTo(onLink);
    DrawFrame(md, 400.f);
    MouseButton(true);
    DrawFrame(md, 400.f);
    bool ok = Expect("a link, on the press", gOpenedLink, "");
    MouseButton(false);
    DrawFrame(md, 400.f);
    ok = Expect("a link, on the release", gOpenedLink, "https://example.com") && ok;
    gOpenedLink.clear();
    gClipboard.clear();
    DragAndCopy(md, 400.f, ImVec2(onLink.x - TextWidth("link") * 0.5f + 1.f, onLink.y),
                ImVec2(f.textOrigin.x + 300.f, onLink.y));
    ok = Expect("a drag from a link opens nothing", gOpenedLink, "") && ok;
    return Expect("a drag from a link selects", gClipboard, "link here") && ok;
}

// PushSelectableText(false) turns the selection off for the renders it covers; in a context whose option selectableText
// is off, PushSelectableText(true) turns it on
static bool CheckSelectableSwitch(RichMd::Context* mainContext)
{
    const char* md = "Some text\n";
    Frame f = DrawFrame(md, 400.f);
    ImVec2 from(f.textOrigin.x + 1.f, f.textOrigin.y + LineHeight() * 0.5f);
    ImVec2 to(f.textOrigin.x + 300.f, from.y);
    gClipboard.clear();
    gPushSelectable = 0;
    DragAndCopy(md, 400.f, from, to);
    bool ok = Expect("PushSelectableText(false)", gClipboard, "");

    RichMd::MarkdownOptions options;
    options.selectableText = false;
    RichMd::Context* context = RichMd::CreateContext(options);
    RichMd::SetCurrentContext(context);
    gPushSelectable = -1;
    DrawFrame(md, 400.f);  // loads the fonts of this context
    DragAndCopy(md, 400.f, from, to);
    ok = Expect("the option selectableText = false", gClipboard, "") && ok;
    gPushSelectable = 1;
    DragAndCopy(md, 400.f, from, to);
    ok = Expect("PushSelectableText(true) over the option", gClipboard, "Some text") && ok;
    gPushSelectable = -1;
    RichMd::DestroyContext(context);
    RichMd::SetCurrentContext(mainContext);

    RichMd::SetSelectableTextDefault(false);
    gClipboard.clear();
    DragAndCopy(md, 400.f, from, to);
    ok = Expect("SetSelectableTextDefault(false)", gClipboard, "") && ok;
    RichMd::SetSelectableTextDefault(true);
    DragAndCopy(md, 400.f, from, to);
    return Expect("SetSelectableTextDefault(true)", gClipboard, "Some text") && ok;
}

// A PushSelectableText() without its PopSelectableText() in the same frame is a recoverable error of Dear ImGui: it is
// reported by the next render (here counted by the error tooltip, the assert being off), and the stack is dropped
static bool CheckUnbalancedPush()
{
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigErrorRecoveryEnableAssert = false;
    io.ConfigErrorRecoveryEnableDebugLog = false;
    const char* md = "Some text\n";
    gPushSelectable = 0;
    gForgetPop = true;
    Frame f = DrawFrame(md, 400.f);
    gPushSelectable = -1;
    gForgetPop = false;
    DrawFrame(md, 400.f);
    bool ok = true;
    if (GImGui->ErrorCountCurrentFrame != 1) {
        printf("selection test failed: %d errors for a push without pop (expected 1)\n",
               GImGui->ErrorCountCurrentFrame);
        ok = false;
    }
    ImVec2 from(f.textOrigin.x + 1.f, f.textOrigin.y + LineHeight() * 0.5f);
    DragAndCopy(md, 400.f, from, ImVec2(f.textOrigin.x + 300.f, from.y));
    ok = Expect("after a push without pop", gClipboard, "Some text") && ok;
    io.ConfigErrorRecoveryEnableAssert = true;
    io.ConfigErrorRecoveryEnableDebugLog = true;
    return ok;
}

int main(int, char**)
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigMacOSXBehaviors = false;  // else the injected Ctrl becomes Cmd on macOS
    ImGui_ImplNull_Init();
    ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) { gClipboard = text; };
    ImGui::GetPlatformIO().Platform_GetClipboardTextFn = [](ImGuiContext*) { return gClipboard.c_str(); };
    RichMd::MarkdownOptions options;
    options.callbacks.OnOpenLink = [](const std::string& url) { gOpenedLink = url; };
    RichMd::Context* mainContext = RichMd::CreateContext(options);
    DrawFrame("warm up", 400.f);  // the first frame loads the fonts

    bool ok = CheckPartialSelection();
    ok = CheckWrappedSelection() && ok;
    ok = CheckSelectionOverSpecialBlocks() && ok;
    ok = CheckWidgetsAndClear() && ok;
    ok = CheckLinks() && ok;
    ok = CheckSelectableSwitch(mainContext) && ok;
    ok = CheckUnbalancedPush() && ok;

    RichMd::DestroyContext();
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    printf(ok ? "selection test: ok\n" : "selection test: FAILED\n");
    return ok ? 0 : 1;
}
