// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
#include "rich_md_mermaid.h"
#include "mermaid_model.h"
#include "imgui_rich_md/rich_md.h"

#include <cfloat>
#include <map>
#include <tuple>

namespace RichMd::Mermaid
{
    struct Cache
    {
        struct Entry
        {
            Diagram diagram;
            int lastFrame = 0;
        };
        std::map<std::tuple<std::string, ImFont*, float>, Entry> entries;  // a source, laid out with a font at a size
    };

    void CacheDeleter::operator()(Cache* cache) const { delete cache; }
    CachePtr CreateCache() { return CachePtr(new Cache()); }

    // Diagrams not drawn for this many frames are dropped (lazily, when a new one is parsed): an editor makes a new
    // source at each keystroke
    static const int kEvictionFrames = 60;

    RenderResult Render(const std::string& source, Cache& cache)
    {
        const int frame = ImGui::GetFrameCount();
        const auto key = std::make_tuple(source, ImGui::GetFont(), ImGui::GetFontSize());
        auto it = cache.entries.find(key);
        if (it == cache.entries.end())
        {
            for (auto i = cache.entries.begin(); i != cache.entries.end();)
                i = (frame - i->second.lastFrame > kEvictionFrames) ? cache.entries.erase(i) : std::next(i);
            it = cache.entries.emplace(key, Cache::Entry{Parse(source), frame}).first;
            if (it->second.diagram.error.empty())
                Layout(it->second.diagram);
        }
        Cache::Entry& entry = it->second;
        entry.lastFrame = frame;
        if (!entry.diagram.error.empty())
            return RenderResult{false, entry.diagram.error};
        Draw(entry.diagram);
        return RenderResult{true, ""};
    }

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
