// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
#pragma once
// Helpers shared by the wrapper and its backends. Not part of the public API.
#include "rich_md.h"
#ifdef IMGUI_RICHMD_WITH_MERMAID
#include "backends/mermaid/rich_md_mermaid.h"
#endif

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

namespace RichMd
{
    class MarkdownRenderer;

    // A context: its options, its renderer (and the fonts), its caches. Defined here for the Python binding, which
    // hands it out as an opaque object.
    struct Context
    {
        MarkdownOptions options;
        std::unique_ptr<MarkdownRenderer> renderer;  // created on first use (it loads the fonts)
        std::map<std::string, std::function<void(const std::string& code)>> fencedBlockRenderers;
        std::unordered_map<std::string, std::string> resolvedImports;  // text -> text with its @import resolved
        int fragmentFrame = -1;    // frame of the last Render call
        int fragmentCounter = 0;   // Render calls in this frame (seeds their ImGui ids)
#ifdef IMGUI_RICHMD_WITH_MERMAID
        Mermaid::CachePtr mermaidCache;  // created on first use
#endif
        ~Context();
    };
}

namespace RichMd { namespace Internal
{
    // Removes the common indentation (that of the first non-empty line) and the leading / trailing
    // empty lines. Code: trailing spaces are trimmed; markdown: they are kept (two are a hard line break).
    std::string Unindent(const std::string& text, bool isCode);
}}
