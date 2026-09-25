// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
#pragma once
// Helpers shared by the wrapper and its backends. Not part of the public API.
#include "../rich_md.h"
#ifdef IMGUI_RICHMD_WITH_MERMAID
#include "../backends/mermaid/rich_md_mermaid.h"
#endif

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace RichMd
{
    class MarkdownRenderer;

    // A text with its transclusions resolved, and the files it asked for, with their modification time (min() when
    // the file was not on the file system): it is resolved again when one of them changes.
    struct ResolvedText
    {
        std::string markdown;
        std::vector<std::pair<std::string, std::filesystem::file_time_type>> files;
        double lastCheck = 0.0;  // ImGui::GetTime() of the last check of the files
    };

    // ::md Context
    // A context holds the options, the renderer (created at the first render: it loads the fonts), the fenced block
    // renderers registered by the application, and caches: the resolved transclusions (per text, resolved again when
    // one of their files changes), the Mermaid diagrams (per source, font and size). The renderer keeps the fonts, the
    // images and the formulas (a formula not drawn for 60 frames is dropped). The host services and MicroTeX are
    // global: every context shares them.
    // ::code
    // Defined here for the Python binding, which hands it out as an opaque object.
    struct Context
    {
        MarkdownOptions options;
        std::unique_ptr<MarkdownRenderer> renderer;  // created on first use (it loads the fonts)
        std::map<std::string, std::function<void(const std::string& code)>> fencedBlockRenderers;
        std::unordered_map<std::string, ResolvedText> resolvedTransclusions;  // per text
        int fragmentFrame = -1;    // frame of the last Render call
        int fragmentCounter = 0;   // Render calls in this frame (seeds their ImGui ids)
        std::vector<bool> selectableTextStack;  // PushSelectableText()
        int selectableTextFrame = -1;            // the frame of the last PushSelectableText()
#ifdef IMGUI_RICHMD_WITH_MERMAID
        Mermaid::CachePtr mermaidCache;  // created on first use
#endif
        ~Context();
    };
    // ::endcode
}

namespace RichMd { namespace Internal
{
    // Removes the common indentation (that of the first non-empty line) and the leading / trailing
    // empty lines. Code: trailing spaces are trimmed; markdown: they are kept (two are a hard line break).
    std::string Unindent(const std::string& text, bool isCode);
}}
