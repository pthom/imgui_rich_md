// A development tool, not an example: renders each diagram of tests/mermaid/corpus to a PNG, and writes a page that
// shows, for each one, its source, Mermaid's own render (mermaid.js, run by the browser) and ours.
//   imgui_rich_md_mermaid_review [output_folder]      (default: ./mermaid_review), then open output_folder/index.html
// Built with the examples (desktop only): it uses their window, Dear ImGui with GLFW and OpenGL 3.
#include "common/app_glfw_gl3.h"
#include "mermaid_checks.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"  // sprintf, on macOS
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string ReadFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static std::string HtmlEscape(const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

// The comment lines at the top of a corpus file: the first one describes the diagram
static std::string Description(const std::string& source) { return MermaidChecks::ReadExpectation(source).description; }

// The rect [p0, p1] of the window (in points) as a PNG, read from the back buffer
static bool WritePng(GLFWwindow* window, ImVec2 p0, ImVec2 p1, const std::string& path, int* width)
{
    int fbW, fbH, winW, winH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glfwGetWindowSize(window, &winW, &winH);
    float scale = (float)fbW / (float)winW;
    int x0 = std::max(0, (int)(p0.x * scale)), y0 = std::max(0, (int)(p0.y * scale));
    int x1 = std::min(fbW, (int)(p1.x * scale)), y1 = std::min(fbH, (int)(p1.y * scale));
    int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0)
        return false;
    std::vector<unsigned char> pixels((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x0, fbH - y1, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::vector<unsigned char> flipped(pixels.size());
    for (int y = 0; y < h; ++y)
        std::copy_n(pixels.data() + (size_t)(h - 1 - y) * w * 3, (size_t)w * 3, flipped.data() + (size_t)y * w * 3);
    *width = (int)((float)w / scale);
    return stbi_write_png(path.c_str(), w, h, 3, flipped.data(), w * 3) != 0;
}

int main(int argc, char** argv)
{
    fs::path outDir = argc > 1 ? fs::path(argv[1]) : fs::path("mermaid_review");
    fs::create_directories(outDir);
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(MERMAID_CORPUS_DIR))
        if (entry.path().extension() == ".mmd")
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());

    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(1600, 1400, "mermaid review", nullptr, nullptr);
    if (!window)
        return 1;
    glfwMakeContextCurrent(window);

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");
    RichMd::InitializeMarkdown();

    std::ostringstream page;
    page << R"(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>Mermaid review</title>
<style>
  body { background: #1e1e1e; color: #ddd; font-family: system-ui, sans-serif; margin: 16px; }
  h2 { font-size: 16px; margin: 32px 0 4px; } p { color: #999; margin: 0 0 8px; }
  table { border-collapse: collapse; } td { vertical-align: top; padding: 8px; border: 1px solid #444; }
  th { text-align: left; color: #999; font-weight: normal; padding: 4px 8px; }
  pre.source { font-size: 12px; margin: 0; }
  .ok { color: #7c7; } li.failure { color: #f77; } li.known { color: #db8; } ul { margin: 4px 0; font-size: 13px; }
</style>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@12.0.0/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true, theme: 'dark' });
</script></head><body>
<h1>Mermaid review</h1>
<p>For each diagram of tests/mermaid/corpus: its source, Mermaid's render (mermaid.js 12.0.0), and imgui_rich_md's.</p>
)";

    for (const fs::path& file : files)
    {
        std::string source = ReadFile(file);
        std::string markdown = "```mermaid\n" + source + "\n```\n";
        std::string name = file.stem().string();
        ImVec2 p0, p1;
        MermaidChecks::Report report;
        for (int frame = 0; frame < 3; ++frame)  // the fonts load at the first frame
        {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::Begin("review", nullptr, ImGuiWindowFlags_NoDecoration);
            ImGui::BeginGroup();
            RichMd::Render(markdown);
            ImGui::EndGroup();
            float margin = ImGui::GetFontSize() * 0.5f;
            p0 = ImVec2(ImGui::GetItemRectMin().x - margin, ImGui::GetItemRectMin().y - margin);
            p1 = ImVec2(ImGui::GetItemRectMax().x + margin, ImGui::GetItemRectMax().y + margin);
            RichMd::SizedFont font = RichMd::GetFont(RichMd::MarkdownFontSpec());  // the checks, with the markdown font
            ImGui::PushFont(font.font, font.size);
            report = MermaidChecks::Evaluate(source);
            ImGui::PopFont();
            ImGui::End();
            ImGui::Render();
            int fbW, fbH;
            glfwGetFramebufferSize(window, &fbW, &fbH);
            glViewport(0, 0, fbW, fbH);
            ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
            glClearColor(bg.x, bg.y, bg.z, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (frame < 2)
                glfwSwapBuffers(window);
        }
        int width = 0;
        bool ok = WritePng(window, p0, p1, (outDir / (name + ".png")).string(), &width);
        glfwSwapBuffers(window);
        printf("%-32s %s\n", name.c_str(), ok ? "rendered" : "NOT RENDERED");
        std::string checks;
        for (const auto& issue : report.failures)
            checks += "<li class=\"failure\">" + HtmlEscape(issue.check + ": " + issue.message) + "</li>";
        for (const auto& check : report.fixedKnown)
            checks += "<li class=\"failure\">" + HtmlEscape(check + " is marked as known, but passes") + "</li>";
        for (const auto& issue : report.known)
            checks += "<li class=\"known\">(known) " + HtmlEscape(issue.check + ": " + issue.message) + "</li>";

        page << "<h2>" << HtmlEscape(name) << "</h2>\n<p>" << HtmlEscape(Description(source)) << "</p>\n"
             << "<table><tr><th>source</th><th>mermaid.js</th><th>imgui_rich_md</th></tr><tr>\n"
             << "<td><pre class=\"source\">" << HtmlEscape(source) << "</pre></td>\n"
             << "<td><pre class=\"mermaid\">" << HtmlEscape(source) << "</pre></td>\n"
             << "<td><img src=\"" << HtmlEscape(name) << ".png\" width=\"" << width << "\"></td>\n"
             << "</tr></table>\n"
             << (checks.empty() ? std::string("<p class=\"ok\">checks: ok</p>\n") : "<ul>" + checks + "</ul>\n")
             << "<p>crossings: " << report.crossings << ", bends: " << report.bends << "</p>\n";
    }
    page << "</body></html>\n";
    std::ofstream(outDir / "index.html") << page.str();
    printf("review page: %s\n", fs::absolute(outDir / "index.html").string().c_str());

    RichMd::DeInitializeMarkdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
