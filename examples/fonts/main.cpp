// imgui_rich_md fonts: fonts that the library does not ship, merged into every markdown font
// (MarkdownFontOptions::mergeFonts): CJK, emoji, icons. CMake downloads them into the build folder
// (see examples/CMakeLists.txt), which becomes the assets folder.
#include "common/app_glfw_gl3.h"
#include "imgui_rich_md/rich_md.h"
#include "IconsFontAwesome6.h"  // ICON_FA_ macros (IconFontCppHeaders)

#include <string>

static const char* kPage = R"md(
# Fonts
The markdown fonts (Roboto, Inconsolata) come with the library. Other fonts are merged into all of them
with `MarkdownFontOptions::mergeFonts`: a CJK font, an emoji font, an icon font... Since Dear ImGui 1.92,
glyphs are loaded on demand, so even a large font costs nothing until one of its characters is displayed.

This example merges three fonts that CMake downloaded into the build folder (they are not part of the
library): Noto Sans JP, Noto Emoji and FontAwesome 6.

## CJK: 日本語
日本語の文章も、ほかのテキストと同じように折り返されます。Dear ImGui の改行処理は CJK の規則を知っているので、単語の間に空白がなくても、ウィンドウの幅に合わせて自然に改行されます。

Every style works: **太字**, *斜体*, `コード`. There is no italic CJK font: the regular glyphs are merged
into the italic font too.

## Emoji: ✅ 🚀
Monochrome emoji, from Noto Emoji: ✅ ☕ ❤ 🚀 🎉 🌍

Most emoji are above U+FFFF: Dear ImGui displays them when built with `IMGUI_USE_WCHAR32`, as in these
examples. Color emoji need Dear ImGui's FreeType loader (`ImGuiFreeTypeLoaderFlags_LoadColor`) and a color
font such as Noto Color Emoji; the library does nothing special for them.

## Icons: @@ROCKET@@
FontAwesome lives in Unicode's private use area: its glyphs come from the macros of `IconsFontAwesome6.h`
([IconFontCppHeaders](https://github.com/juliettef/IconFontCppHeaders)), in every style:
@@ROCKET@@ regular, **@@HEART@@ bold**, *@@CHECK@@ italic*, `@@COPY@@ code`.

## The code
```cpp
RichMd::SetAssetsFolder(folder);  // where the fonts are
RichMd::MarkdownOptions options;
options.fontOptions.mergeFonts = {"fonts/NotoSansJP-Regular.otf", "fonts/NotoEmoji.ttf", "fonts/fa-solid-900.ttf"};
RichMd::InitializeMarkdown(options);

RichMd::Render("Launch " ICON_FA_ROCKET);
```

## Licenses
Noto Sans JP and Noto Emoji: SIL Open Font License 1.1. FontAwesome Free: SIL OFL 1.1 for the fonts, CC BY 4.0
for the icons. IconFontCppHeaders: zlib.
)md";

static std::string ReplaceAll(std::string text, const std::string& from, const std::string& to)
{
    for (size_t pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size()))
        text.replace(pos, from.size(), to);
    return text;
}

static void Gui()
{
    static const std::string page = ReplaceAll(ReplaceAll(ReplaceAll(ReplaceAll(kPage,
        "@@ROCKET@@", ICON_FA_ROCKET), "@@HEART@@", ICON_FA_HEART), "@@CHECK@@", ICON_FA_CHECK), "@@COPY@@", ICON_FA_COPY);
    RichMd::Render(page);
}

int main(int, char**)
{
    RichMd::SetAssetsFolder(FONTS_EXAMPLE_ASSETS);  // the downloaded fonts; the library's own are embedded
    RichMd::MarkdownOptions options;
    options.fontOptions.mergeFonts = {"fonts/NotoSansJP-Regular.otf", "fonts/NotoEmoji.ttf", "fonts/fa-solid-900.ttf"};
    return RunApp("imgui_rich_md fonts", ImVec2(900.f, 900.f), options, Gui);
}
