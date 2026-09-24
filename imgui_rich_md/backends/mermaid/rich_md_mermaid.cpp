// Part of ImGui Bundle - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_bundle
#include "rich_md_mermaid.h"
#include "imgui_rich_md/rich_md.h"

#include <cfloat>
#include <unordered_map>

namespace RichMd::Mermaid
{
    namespace
    {
        struct CacheEntry
        {
            Diagram diagram;
            int lastFrame = 0;
        };
        std::unordered_map<std::string, CacheEntry> gCache;  // source -> its diagram
        // Diagrams not drawn for this many frames are dropped (lazily, when a new one is parsed): an editor
        // makes a new source at each keystroke
        const int kEvictionFrames = 60;
    }

    RenderResult Render(const std::string& source)
    {
        int frame = ImGui::GetFrameCount();
        auto it = gCache.find(source);
        if (it == gCache.end())
        {
            for (auto i = gCache.begin(); i != gCache.end();)
                i = (frame - i->second.lastFrame > kEvictionFrames) ? gCache.erase(i) : std::next(i);
            it = gCache.emplace(source, CacheEntry{Parse(source), frame}).first;
        }
        CacheEntry& entry = it->second;
        entry.lastFrame = frame;
        Diagram& diagram = entry.diagram;
        if (!diagram.error.empty())
            return RenderResult{false, diagram.error};
        if (diagram.layoutFont != ImGui::GetFont() || diagram.layoutFontSize != ImGui::GetFontSize())
            Layout(diagram);
        Draw(diagram);
        return RenderResult{true, ""};
    }

    void ClearCache() { gCache.clear(); }

    ImFont* ItalicFont()
    {
        if (!RichMd::GetCurrentContext())
            return nullptr;
        return RichMd::GetFont(RichMd::MarkdownFontSpec(true)).font;
    }

    ImVec2 ClassLineSize(const ClassLine& line)
    {
        ImFont* italic = line.isAbstract ? ItalicFont() : nullptr;
        if (!italic)
            return ImGui::CalcTextSize(line.text.c_str());
        return italic->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.f, line.text.c_str());
    }
}
