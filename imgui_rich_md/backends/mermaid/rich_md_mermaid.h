// Part of ImGui Bundle - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_bundle
//
// Mermaid diagrams, drawn natively: the entry points of the backend, for rich_md.cpp. Applications call
// RichMd::RenderMermaid (rich_md.h); the supported subset: docs/mermaid.md. The backend's model and pipeline:
// mermaid_model.h.
#pragma once
#include <string>

namespace RichMd::Mermaid
{
    struct RenderResult
    {
        bool drawn = false;
        std::string error;  // when not drawn: the parse error, "line N: ..."
    };
    // Parses (once per source), lays out (when the font changes) and draws, at the cursor
    RenderResult Render(const std::string& source);
    // Forgets the diagrams parsed so far
    void ClearCache();
}
