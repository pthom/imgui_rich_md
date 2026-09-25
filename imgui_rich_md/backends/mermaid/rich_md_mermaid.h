// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
#pragma once
/*::md Mermaid backend
Mermaid diagrams, drawn natively: the entry points of the backend, for `rich_md.cpp`. Applications call
`RichMd::RenderMermaid` (`rich_md.h`); the supported subset: `docs/mermaid.md`. The backend's model and pipeline:
`mermaid_model.h`.
*/
#include <memory>
#include <string>

namespace RichMd::Mermaid
{
    // ::code Entry points
    // The diagrams parsed and laid out so far, per source, font and size: each RichMd context has one
    struct Cache;
    struct CacheDeleter
    {
        void operator()(Cache* cache) const;
    };
    using CachePtr = std::unique_ptr<Cache, CacheDeleter>;
    CachePtr CreateCache();

    struct RenderResult
    {
        bool drawn = false;
        std::string error;  // when not drawn: the parse error, "line N: ..."
    };
    // Parses (once per source), lays out (once per font and size) and draws, at the cursor
    RenderResult Render(const std::string& source, Cache& cache);
    // ::endcode
}
