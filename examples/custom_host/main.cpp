// imgui_rich_md custom_host: plugging the library into an application's own services (RichMd::HostServices):
// its textures, its asset storage, its log. SetHostServices() comes before CreateContext(); every service
// left empty keeps its default.
#include "common/app_glfw_gl3.h"
#include "imgui_rich_md/rich_md.h"
#include "imgui_rich_md/rich_md_host.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

static const char* kPage = R"md(
# A custom host
This page is rendered through the host services of this example (`RichMd::HostServices`), installed
with `RichMd::SetHostServices()` before `RichMd::CreateContext()`.

## Textures: `UploadRgba`
Every image and formula becomes a texture through `UploadRgba`. Here, plain OpenGL textures owned by the
application, as an engine without Dear ImGui's texture protocol would do: `MarkdownTexture::keepAlive`
deletes one when the markdown caches drop it. Textures created so far: **@@TEXTURES@@**.

A formula, when the library is built with LaTeX: $e^{i\pi} + 1 = 0$

## Assets: `ReadAsset`
The image below is not a file: `ReadAsset` serves it from the application's memory (think of a pack file,
or a virtual file system). The fonts still come from `RichMd::ReadAssetDefault()`.

![generated](generated/gradient.ppm)

An asset that exists nowhere, for which `ReadAsset` returns `std::nullopt`: ![missing](images/missing.png)

## Warnings: `Log`
This example asks for a merge font that does not exist (`fonts/missing_icons.ttf`), on purpose: the
warning goes to the log panel below, instead of stderr.
)md";

// The application's own asset storage
static std::map<std::string, std::vector<uint8_t>> gMemoryAssets;
// The application's own log
static std::vector<std::string> gLog;
static int gTexturesCreated = 0;

// A PPM image (a format stb_image reads): a color gradient
static std::vector<uint8_t> MakeGradientPpm(int w, int h)
{
    char header[32];
    int headerSize = snprintf(header, sizeof(header), "P6\n%d %d\n255\n", w, h);
    std::vector<uint8_t> ppm(header, header + headerSize);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            ppm.push_back((uint8_t)(255 * x / (w - 1)));
            ppm.push_back((uint8_t)(255 * y / (h - 1)));
            ppm.push_back(160);
        }
    return ppm;
}

static RichMd::AssetBytes ReadAssetFromMemory(const std::string& assetPath)
{
    auto it = gMemoryAssets.find(assetPath);
    if (it != gMemoryAssets.end())
        return it->second;
    return RichMd::ReadAssetDefault(assetPath);  // the fonts, embedded in the library
}

static RichMd::MarkdownTexture UploadRgbaWithOpenGL(const unsigned char* rgba, int w, int h)
{
    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    ++gTexturesCreated;

    RichMd::MarkdownTexture texture;
    texture.ref = ImTextureRef((ImTextureID)(intptr_t)textureId);
    texture.size = ImVec2((float)w, (float)h);
    texture.keepAlive = std::shared_ptr<void>(nullptr, [textureId](void*) { glDeleteTextures(1, &textureId); });
    return texture;
}

static void Gui()
{
    std::string page = kPage;
    page.replace(page.find("@@TEXTURES@@"), 12, std::to_string(gTexturesCreated));
    RichMd::Render(page);

    ImGui::SeparatorText("Log");
    for (const std::string& message : gLog)
        ImGui::TextWrapped("%s", message.c_str());
}

int main(int, char**)
{
    gMemoryAssets["generated/gradient.ppm"] = MakeGradientPpm(256, 96);

    RichMd::HostServices services;
    services.UploadRgba = UploadRgbaWithOpenGL;
    services.ReadAsset = ReadAssetFromMemory;
    services.Log = [](const std::string& message) { gLog.push_back(message); };
    RichMd::SetHostServices(services);  // before CreateContext (called by RunApp)

    RichMd::MarkdownOptions options;
#ifdef IMGUI_RICHMD_WITH_LATEX
    options.withLatex = true;
#endif
    options.fontOptions.mergeFonts = {"fonts/missing_icons.ttf"};  // on purpose: see the log panel
    return RunApp("imgui_rich_md custom host", ImVec2(900.f, 900.f), options, Gui);
}
