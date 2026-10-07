// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
#include "rich_md.h"
#include "rich_md_host.h"
#include "internal/rich_md_internal.h"
#ifdef IMGUI_RICHMD_WITH_CODE_EDITOR
#include "backends/code_editor/snippets.h"
#endif
#ifdef IMGUI_RICHMD_WITH_DOWNLOAD_IMAGES
#include "internal/rich_md_url_download.h"
#endif


#include "imgui.h"
#include "imgui_internal.h"  // RegisterUserTexture
#include "internal/rich_md_renderer.h"

// Platform includes for OpenUrlInBrowser
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX  // no min and max macros (they break std::numeric_limits<T>::min() and the like)
#endif
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#include <unistd.h>   // fork, execlp, _exit
#include <sys/wait.h> // waitpid
#elif defined(__linux__)
#include <unistd.h>
#include <sys/wait.h>
#endif

#ifdef IMGUI_RICHMD_WITH_LATEX
#include "backends/latex/rich_md_latex.h"
#endif
#ifdef IMGUI_RICHMD_WITH_MERMAID
#include "backends/mermaid/rich_md_mermaid.h"
#endif

#include "third_party/stb_image.h"

#include <algorithm>
#include <string>
#include <vector>
#include <utility>
#include <map>
#include <set>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

// Small string helpers (replace fplus, to keep the library decoupled from it).
namespace
{
    std::vector<std::string> _SplitLines(const std::string& s)
    {
        std::vector<std::string> lines;
        std::string cur;
        for (char c : s)
        {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else cur += c;
        }
        lines.push_back(cur);
        return lines;
    }

    std::string _TrimWhitespace(const std::string& s)
    {
        const char* ws = " \t\r\n\f\v";
        size_t b = s.find_first_not_of(ws);
        if (b == std::string::npos)
            return "";
        size_t e = s.find_last_not_of(ws);
        return s.substr(b, e - b + 1);
    }

    std::string _JoinLines(const std::vector<std::string>& lines)
    {
        std::string out;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (i > 0)
                out += '\n';
            out += lines[i];
        }
        return out;
    }

    // Opens an url in the default browser (no shell involved: the url is passed as a raw argument)
    void _OpenUrlInBrowser(const char* url)
    {
#if defined(__EMSCRIPTEN__)
        char js_command[1024];
        snprintf(js_command, 1024, "window.open(\"%s\");", url);
        emscripten_run_script(js_command);
#elif defined(_WIN32)
        ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif TARGET_OS_IPHONE
        (void)url;  // nothing on iOS
#elif TARGET_OS_OSX || defined(__linux__)
#if TARGET_OS_OSX
        const char* opener = "open";
#else
        const char* opener = "xdg-open";
#endif
        pid_t pid = fork();
        if (pid == 0)
        {
            execlp(opener, opener, url, nullptr);
            _exit(1);
        }
        else if (pid > 0)
            waitpid(pid, nullptr, 0);
#else
        (void)url;
#endif
    }

    // See imgui_md_internal.h
    std::string _Unindent(const std::string& text, bool isCode)
    {
        auto lines = _SplitLines(text);
        size_t indent = 0;
        for (const auto& line : lines)
            if (_TrimWhitespace(line).size() > 0)
            {
                indent = line.find_first_not_of(' ');
                break;
            }
        std::vector<std::string> processed;
        for (const auto& line : lines)
        {
            std::string processedLine = line.compare(0, indent, std::string(indent, ' ')) == 0 ? line.substr(indent) : line;
            if (isCode)
                processedLine.erase(processedLine.find_last_not_of(' ') + 1);
            processed.push_back(processedLine);
        }
        while (!processed.empty() && _TrimWhitespace(processed.front()).empty())
            processed.erase(processed.begin());
        while (!processed.empty() && _TrimWhitespace(processed.back()).empty())
            processed.pop_back();
        return _JoinLines(processed);
    }

    std::string _ToLower(const std::string& s)
    {
        std::string out = s;
        for (char& c : out)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }
}

namespace RichMd
{
    namespace Internal
    {
        std::string Unindent(const std::string& text, bool isCode) { return _Unindent(text, isCode); }
    }

    // Host services (see imgui_md_host.h)
    static HostServices gHostServices;
    // Set by OnImage_Default while an image is downloading (the renderer then draws a spinner)
    static bool gImageIsLoading = false;
    void SetHostServices(const HostServices& services) { gHostServices = services; }
    const HostServices& GetHostServices() { return gHostServices; }
#ifdef IMGUI_RICHMD_HOST_HELLO_IMGUI
    void Priv_InstallHelloImGuiHost();  // hosts/hello_imgui_host.cpp
#endif

    // Default services: the embedded assets, the file system, stderr
#ifdef IMGUI_RICHMD_EMBED_ASSETS
    extern const EmbeddedAsset imgui_richmd_embedded_assets[];
    extern const int imgui_richmd_embedded_assets_count;
#endif
#ifdef IMGUI_RICHMD_EMBED_ASSETS
    // Inflates a gzip stream (RFC 1952: header, deflate data, crc + size) with stb_image's zlib decoder
    static AssetBytes _Gunzip(const unsigned char* gz, size_t gzSize, size_t expectedSize)
    {
        if (gzSize < 18 || gz[0] != 0x1f || gz[1] != 0x8b || gz[2] != 8)
            return std::nullopt;
        unsigned char flags = gz[3];
        size_t pos = 10;
        if (flags & 4)  // FEXTRA
        {
            size_t extraLen = gz[pos] | (gz[pos + 1] << 8);
            pos += 2 + extraLen;
        }
        if (flags & 8)  // FNAME
            while (pos < gzSize && gz[pos++] != 0) {}
        if (flags & 16)  // FCOMMENT
            while (pos < gzSize && gz[pos++] != 0) {}
        if (flags & 2)  // FHCRC
            pos += 2;
        if (pos + 8 > gzSize)
            return std::nullopt;
        int outLen = 0;
        char* inflated = stbi_zlib_decode_malloc_guesssize_headerflag(
            (const char*)gz + pos, (int)(gzSize - pos - 8), (int)expectedSize, &outLen, 0 /* raw deflate */);
        if (inflated == nullptr)
            return std::nullopt;
        std::vector<uint8_t> bytes((const uint8_t*)inflated, (const uint8_t*)inflated + outLen);
        stbi_image_free(inflated);
        return bytes;
    }
#endif

    static std::string gAssetsFolder;
    void SetAssetsFolder(const std::string& folder) { gAssetsFolder = folder; }

    AssetBytes ReadAssetDefault(const std::string& assetPath)
    {
#ifdef IMGUI_RICHMD_EMBED_ASSETS
        for (int i = 0; i < imgui_richmd_embedded_assets_count; ++i)
        {
            const EmbeddedAsset& asset = imgui_richmd_embedded_assets[i];
            if (assetPath == asset.path)
                return _Gunzip(asset.data, asset.compressedSize, asset.size);
        }
#endif
        std::string path = gAssetsFolder.empty() ? assetPath : gAssetsFolder + "/" + assetPath;
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return std::nullopt;
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
#ifdef IMGUI_RICHMD_WITH_LATEX
    static bool gLatexInitFailed = false;  // one-shot: no retry (and no log) storm

    // Default LaTeX renderer: MicroTeX, initialized on first use (the host's assets may not be
    // ready before the first frame), with its fonts read through the host
    static std::optional<LatexBitmap> _RenderLatexWithMicroTeX(const std::string& latex, float fontSizePx, ImU32 color, bool displayStyle)
    {
        if (!RichMd::Latex::IsInitialized())
        {
            if (gLatexInitFailed)
                return std::nullopt;
            auto clmData = gHostServices.ReadAsset("fonts/latex/latinmodern-math.clm1");
            auto otfData = gHostServices.ReadAsset("fonts/latex/latinmodern-math.otf");
            if (!clmData || !otfData)
            {
                gHostServices.Log("LaTeX font assets not found at fonts/latex/. Formulas will be shown as plain text source.");
                gLatexInitFailed = true;
                return std::nullopt;
            }
            RichMd::Latex::InitFromMemory(*clmData, *otfData);
        }
        auto style = displayStyle ? RichMd::Latex::TexStyle::Display : RichMd::Latex::TexStyle::Text;
        RichMd::Latex::RenderedFormula formula;
        try {  // an invalid formula (MicroTeX throws) falls back to its source text
            formula = RichMd::Latex::Render(latex, fontSizePx, color, style);
        } catch (const std::exception& e) {
            LatexBitmap invalid;
            invalid.error = e.what();
            return invalid;
        }
        LatexBitmap bitmap;
        bitmap.rgba = std::move(formula.Pixels);
        bitmap.width = formula.Width;
        bitmap.height = formula.Height;
        bitmap.baselineY = formula.BaselineY;
        return bitmap;
    }
#endif

#ifdef IMGUI_RICHMD_WITH_CODE_EDITOR
    static Snippets::SnippetLanguage _SnippetLanguage(const std::string& language)
    {
        std::string lower = _ToLower(language);
        if (lower == "cpp") return Snippets::SnippetLanguage::Cpp;
        if (lower == "c") return Snippets::SnippetLanguage::C;
        if (lower == "python") return Snippets::SnippetLanguage::Python;
        if (lower == "glsl") return Snippets::SnippetLanguage::Glsl;
        if (lower == "hlsl") return Snippets::SnippetLanguage::Hlsl;
        if (lower == "sql") return Snippets::SnippetLanguage::Sql;
        if (lower == "lua") return Snippets::SnippetLanguage::Lua;
        if (lower == "angelscript") return Snippets::SnippetLanguage::AngelScript;
        return Snippets::DefaultSnippetLanguage();
    }

    // A code block in the code editor: a read-only snippet with syntax highlighting (one editor per distinct code).
    // In a document, with the matches of its search; returns their rectangles.
    static std::vector<ImRect> _RenderCodeBlockInEditor(const std::string& code, const std::string& language,
                                                        const std::vector<CodeBlockMatch>& matches, bool inDocument)
    {
        // Nothing to keep between frames: the snippet's editor (keyed by the code) holds its state
        Snippets::SnippetData snippet;
        snippet.Code = code;
        snippet.Language = _SnippetLanguage(language);
        snippet.ShowCursorPosition = false;
        snippet.ReadOnly = true;
        return Internal::ShowCodeSnippetWithMatches(snippet, matches, inDocument);
    }

    // The default code block renderer (HostServices::RenderCodeBlock)
    static void _RenderCodeBlockWithEditor(const std::string& code, const std::string& language)
    {
        _RenderCodeBlockInEditor(code, language, {}, false);
    }
#endif

    // Default UploadRgba: an ImTextureData registered with Dear ImGui (1.92+, backends with
    // ImGuiBackendFlags_RendererHasTextures): the backend creates the GPU texture at the next frame,
    // and destroys it when asked; the ImTextureData is freed once the backend reports it destroyed.
    static std::vector<ImTextureData*> gTexturesToFree;  // asked to be destroyed, freed once the backend did it

    static void _SweepDestroyedTextures()
    {
        for (auto it = gTexturesToFree.begin(); it != gTexturesToFree.end(); )
        {
            if ((*it)->Status == ImTextureStatus_Destroyed)
            {
                if (ImGui::GetCurrentContext())
                    ImGui::UnregisterUserTexture(*it);
                IM_DELETE(*it);
                it = gTexturesToFree.erase(it);
            }
            else
                ++it;
        }
    }

    MarkdownTexture UploadRgbaDefault(const unsigned char* rgba, int w, int h)
    {
        MarkdownTexture tex;
        if (!(ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures))
        {
            static bool warned = false;
            if (!warned)
                gHostServices.Log("UploadRgba: the rendering backend does not support ImTextureData (ImGuiBackendFlags_RendererHasTextures): no images, set HostServices::UploadRgba");
            warned = true;
            return tex;
        }
        ImTextureData* data = IM_NEW(ImTextureData)();
        data->Create(ImTextureFormat_RGBA32, w, h);
        memcpy(data->GetPixels(), rgba, (size_t)w * (size_t)h * 4);
        // Every pixel is to upload. Backends upload the whole texture on WantCreate anyway, except some
        // (wgpu-py's) which upload UpdateRect only, left empty by Create().
        data->UpdateRect = ImTextureRect{0, 0, (unsigned short)w, (unsigned short)h};
        data->SetStatus(ImTextureStatus_WantCreate);
        ImGui::RegisterUserTexture(data);
        tex.ref._TexData = data;
        tex.size = ImVec2((float)w, (float)h);
        tex.keepAlive = std::shared_ptr<void>(data, [](void* p) {
            auto* d = (ImTextureData*)p;
            d->SetStatus(ImTextureStatus_WantDestroy);
            gTexturesToFree.push_back(d);
        });
        return tex;
    }

    // ::md Default host services
    // Every host service has a default, installed when a context is created: textures registered with Dear ImGui
    // (created by the rendering backend at the next frame), assets embedded in the binary or read from the assets
    // folder, the code editor and MicroTeX when they are built in, logging to stderr. A host replaces any of them
    // with `SetHostServices()` before `CreateContext()`; the fields it leaves empty keep their default.
    // ::code
    static void _InstallDefaultHostServices()
    {
        if (!gHostServices.UploadRgba)
            gHostServices.UploadRgba = UploadRgbaDefault;
#ifdef IMGUI_RICHMD_WITH_CODE_EDITOR
        if (!gHostServices.RenderCodeBlock)
            gHostServices.RenderCodeBlock = _RenderCodeBlockWithEditor;
#endif
#ifdef IMGUI_RICHMD_WITH_LATEX
        if (!gHostServices.RenderLatex)
            gHostServices.RenderLatex = _RenderLatexWithMicroTeX;
#endif
        if (!gHostServices.ReadAsset)
            gHostServices.ReadAsset = ReadAssetDefault;
        if (!gHostServices.Log)
            gHostServices.Log = [](const std::string& message) { fprintf(stderr, "rich_md: %s\n", message.c_str()); };
    }
    // ::endcode

    // Default code block: monospaced text in a frame, with a copy button
    // A code block as text, with the matches of a search behind it. Returns their rectangles, on the screen (none when
    // the block is out of view).
    static std::vector<ImRect> _RenderCodeBlockPlain(const std::string& code,
                                                     const std::vector<CodeBlockMatch>& matches = {})
    {
        std::vector<ImRect> rects;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        if (ImGui::BeginChild("code", ImVec2(0.f, 0.f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
        {
            float copyWidth = ImGui::CalcTextSize("Copy").x + ImGui::GetStyle().FramePadding.x * 2.f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - copyWidth);
            if (ImGui::SmallButton("Copy"))
                ImGui::SetClipboardText(code.c_str());
            ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x);
            SizedFont codeFont = GetCodeFont();
            ImGui::PushFont(codeFont.font, codeFont.size);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            for (const CodeBlockMatch& match : matches) {  // a match is on one line (the query has no newline)
                size_t lineStart = code.rfind('\n', match.begin == 0 ? 0 : match.begin - 1);
                lineStart = (lineStart == std::string::npos || match.begin == 0) ? 0 : lineStart + 1;
                const float line = (float)std::count(code.begin(), code.begin() + (long)lineStart, '\n');
                const char* text = code.c_str();
                ImVec2 min(origin.x + ImGui::CalcTextSize(text + lineStart, text + match.begin).x,
                           origin.y + line * ImGui::GetTextLineHeight());
                ImVec2 max(min.x + ImGui::CalcTextSize(text + match.begin, text + match.end).x,
                           min.y + ImGui::GetTextLineHeight());
                if (match.color != 0)
                    ImGui::GetWindowDrawList()->AddRectFilled(min, max, match.color);  // behind the text
                rects.emplace_back(min, max);
            }
            ImGui::TextUnformatted(code.c_str());
            ImGui::PopFont();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return rects;
    }

    // A code block: through the host (the code editor), or plain
    static void _RenderCodeBlock(const std::string& code, const std::string& language)
    {
        if (gHostServices.RenderCodeBlock)
            gHostServices.RenderCodeBlock(code, language);
        else
            _RenderCodeBlockPlain(code);
    }



    // Note: font sizes below are expressed at their nominal (96 PPI) value.
    // HighDPI scaling is applied automatically at display time by ImGui via
    // ImGui::GetStyle().FontScaleDpi (set by HelloImGui from dpiWindowSizeFactor),
    // so we must *not* pre-multiply font sizes by the DPI factor here anymore.

    namespace RichMdFonts
    {
        struct MarkdownEmphasis
        {
            bool italic = false;
            bool bold = false;
        };
        struct MarkdownTextStyle
        {
            MarkdownEmphasis markdownEmphasis;
            int headerLevel = 0;
        };

        static bool operator==(const MarkdownEmphasis& lhs, const MarkdownEmphasis& rhs) {
            return (lhs.italic == rhs.italic) && (lhs.bold == rhs.bold);
        }

        static std::vector<MarkdownEmphasis> AllEmphasisVariants()
        {
            return {
                { false, false },
                { false, true },
                { true, false },
                { true, true },
            };
        }


        float MarkdownFontOptions_FontSize(const MarkdownFontOptions &self, int headerLevel)
        {
            if (headerLevel <= 0)
                return self.regularSize;
            else
            {
                int idxSizeFactors = headerLevel - 1;
                if (idxSizeFactors >= 6)
                    idxSizeFactors = 5;
                float multiplicationFactor = self.headerSizeFactors[idxSizeFactors];
                float fontSize = self.regularSize * multiplicationFactor;
                return fontSize;
            }
        };


        std::string MarkdownFontOptions_FontFilename(const MarkdownFontOptions &self, MarkdownEmphasis style)
        {
            std::string r = self.fontBasePath + "-";
            if (style.bold)
                r += "Bold";
            else
                r += "Regular";
            if (style.italic)
                r += "Italic";
            r += ".ttf";
            return r;
        }

        bool IsDefaultMarkdownEmphasis(const MarkdownEmphasis& style)
        {
            return !style.bold && !style.italic;
        }


        class FontCollection
        {
        public:
            FontCollection(const MarkdownFontOptions& options): mMarkdownFontOptions(options)
            {
                LoadFonts();
            }

            SizedFont GetFontCode() const
            {
                return {mFontCode, mMarkdownFontOptions.regularSize};
            }

            SizedFont GetDefaultFont() const
            {
                auto defaultMarkdownStyle = MarkdownTextStyle{};
                return GetFont(defaultMarkdownStyle);
            }

            SizedFont GetFont(const MarkdownTextStyle& _markdownTextStyle) const
            {
                MarkdownTextStyle markdownTextStyle = _markdownTextStyle;
                if (markdownTextStyle.headerLevel < 0)
                    markdownTextStyle.headerLevel = 0;

                float fontSize = MarkdownFontOptions_FontSize(mMarkdownFontOptions, markdownTextStyle.headerLevel);

                for (auto pair: mFonts)
                {
                    if (pair.first == markdownTextStyle.markdownEmphasis)
                        return SizedFont{ pair.second, fontSize };
                }
                IM_ASSERT(false && "Could not find font for markdown style");
                return SizedFont{ nullptr, fontSize };  // the current font, at the markdown size
            }
        private:
            // Adds a font from an asset (nullptr if the asset is missing). merge: into the last added font.
            static ImFont* AddFontFromAsset(const std::string& assetPath, float fontSize, bool merge)
            {
                auto bytes = GetHostServices().ReadAsset(assetPath);
                if (!bytes)
                    return nullptr;
                ImFontConfig cfg;
                cfg.MergeMode = merge;
                cfg.FontDataOwnedByAtlas = true;
                std::string stem = std::filesystem::path(assetPath).stem().string();
                snprintf(cfg.Name, sizeof(cfg.Name), "%s %d", stem.c_str(), (int)std::lround(fontSize));
                void* data = IM_ALLOC(bytes->size());
                memcpy(data, bytes->data(), bytes->size());
                return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, (int)bytes->size(), fontSize, &cfg);
            }

            // Loads a markdown font and merges the merge fonts (options + host) into it
            ImFont* LoadMarkdownFont(const std::string& assetPath, float fontSize) const
            {
                ImFont* font = AddFontFromAsset(assetPath, fontSize, false);
                if (font == nullptr)
                    return nullptr;
                std::vector<std::string> mergeFonts = mMarkdownFontOptions.mergeFonts;
                if (GetHostServices().DefaultMergeFonts)
                    for (const auto& f : GetHostServices().DefaultMergeFonts())
                        mergeFonts.push_back(f);
                for (const auto& mergeFont : mergeFonts)
                    if (AddFontFromAsset(mergeFont, fontSize, true) == nullptr && mMissingMergeFonts.insert(mergeFont).second)
                        GetHostServices().Log("merge font not found: " + mergeFont);
                return font;
            }

            void LoadFonts()
            {
                const char* help =
                    "RichMd needs these assets: fonts/Roboto/Roboto-{Regular,Bold,RegularItalic,BoldItalic}.ttf, "
                    "fonts/Inconsolata-Medium.ttf and images/markdown_broken_image.png "
                    "(see imgui_bundle/imgui_bundle_assets/).";
                float defaultFontLoadingSize = 16.f;  // size at loading time (then Fonts can be resized to any size)
                for (auto emphasisVariant: AllEmphasisVariants())
                {
                    std::string fontFile = MarkdownFontOptions_FontFilename(mMarkdownFontOptions, emphasisVariant);
                    ImFont* font = LoadMarkdownFont(fontFile, defaultFontLoadingSize);
                    if (font == nullptr)
                    {
                        GetHostServices().Log("Markdown font file \"" + fontFile + "\" not found! " + help);
                        IM_ASSERT(false);
                    }
                    mFonts.push_back(std::make_pair(emphasisVariant, font) );
                }

                float fontSize = MarkdownFontOptions_FontSize(mMarkdownFontOptions, 0);
                mFontCode = LoadMarkdownFont("fonts/Inconsolata-Medium.ttf", fontSize);
                if (mFontCode == nullptr) {
                    // SourceCodePro-Regular was the old default font for code
                    // we try to load it, to be nice with older users
                    mFontCode = LoadMarkdownFont("fonts/SourceCodePro-Regular.ttf", fontSize);
                }
                if (mFontCode == nullptr) {
                    GetHostServices().Log(std::string("Markdown code font not found! ") + help);
                    IM_ASSERT(false);
                }
            }

            MarkdownFontOptions mMarkdownFontOptions;
            std::vector<std::pair<MarkdownEmphasis, ImFont*>> mFonts;
            ImFont* mFontCode;
            mutable std::set<std::string> mMissingMergeFonts;  // each one is logged once, not once per markdown font
        };

    } //namespace MdFonts

    struct MarkdownCollection
    {
        MarkdownCollection(const MarkdownFontOptions& options)
            : mFontCollection(options)
        {}
        RichMdFonts::FontCollection mFontCollection;

        mutable std::map<std::string, MarkdownTexture > mLoadedImages;

        // Formula textures, keyed by source + size + color + style
        struct LatexEntry { MarkdownTexture texture; int baselineY = 0; int lastUsedFrame = 0; std::string error; };
        mutable std::map<std::string, LatexEntry> mLatexCache;
    };
    // Formulas not displayed for this many frames are dropped from the cache (lazily, when a new one is inserted)
    static const int gLatexEvictionFrames = 60;


    static Context* gCurrentContext = nullptr;

    class MarkdownRenderer : public Renderer
    {
    private:
        MarkdownOptions *mMarkdownOptions;
        MarkdownCollection mMarkdownCollection;
    public:
        MarkdownRenderer(MarkdownOptions* markdownOptions)
            : mMarkdownOptions(markdownOptions)
            , mMarkdownCollection(markdownOptions->fontOptions)
        {
            if (mMarkdownOptions->withLatex && gHostServices.RenderLatex)
                EnableLatex();
            set_flag(MD_FLAG_PERMISSIVEAUTOLINKS, mMarkdownOptions->autolinks);
            set_flag(MD_FLAG_HARD_SOFT_BREAKS, mMarkdownOptions->hardSoftBreaks);
            set_flag(MD_FLAG_WIKILINKS, (bool)mMarkdownOptions->callbacks.OnWikiLink);
        }

        std::map<std::string, MarkdownTexture >& ImageCache()
        {
            return mMarkdownCollection.mLoadedImages;
        }

        void Render(const std::string& s)
        {
            auto defaultSizedFont = mMarkdownCollection.mFontCollection.GetDefaultFont();
            ImGui::PushFont(defaultSizedFont.font, defaultSizedFont.size);

            const char * start = s.c_str();
            const char * end = start + s.size();
            this->print(start, end);
            ImGui::PopFont();
        }

        SizedFont get_font_code()
        {
            return mMarkdownCollection.mFontCollection.GetFontCode();
        }

        SizedFont GetFont(const MarkdownFontSpec& fontSpec)
        {
            RichMdFonts::MarkdownTextStyle markdownTextStyle;
            markdownTextStyle.headerLevel = fontSpec.headerLevel;
            markdownTextStyle.markdownEmphasis.bold = fontSpec.bold;
            markdownTextStyle.markdownEmphasis.italic = fontSpec.italic;
            return mMarkdownCollection.mFontCollection.GetFont(markdownTextStyle);
        }


    private:
        Renderer::MdSizedFont get_font() const override
        {
            if (m_is_code)
            {
                // https://github.com/mekhontsev/imgui_md does not handle correctly code blocks
                // so that we will never reach here...
                auto fontCode = mMarkdownCollection.mFontCollection.GetFontCode();
                return Renderer::MdSizedFont{ fontCode.font, fontCode.size };
            }
            else
            {
                RichMdFonts::MarkdownTextStyle markdownTextStyle;
                markdownTextStyle.headerLevel = m_hlevel;
                markdownTextStyle.markdownEmphasis.bold =
                    m_is_strong || (m_is_table_header && m_table_header_highlight);
                markdownTextStyle.markdownEmphasis.italic = m_is_em;
                auto font  = mMarkdownCollection.mFontCollection.GetFont(markdownTextStyle);
                return Renderer::MdSizedFont{ font.font, font.size };
            }
        };

        void open_url() const override
        {
            if (mMarkdownOptions->callbacks.OnOpenLink)
                mMarkdownOptions->callbacks.OnOpenLink(m_href);
        }

        void open_wikilink() const override
        {
            if (mMarkdownOptions->callbacks.OnWikiLink)
                mMarkdownOptions->callbacks.OnWikiLink(m_href);
        }

        void heading(int level, const std::string& text) override
        {
            if (mMarkdownOptions->callbacks.OnHeading)
                mMarkdownOptions->callbacks.OnHeading(level, text);
        }

        image_status get_image(image_info& nfo) const override
        {
            if (! mMarkdownOptions->callbacks.OnImage)
                return image_status::none;

            gImageIsLoading = false;
            std::optional<MarkdownImage> mdImage = mMarkdownOptions->callbacks.OnImage(m_img_src);
            if (! mdImage.has_value())
                return gImageIsLoading ? image_status::loading : image_status::none;

            nfo.texture = ImTextureRef(mdImage->texture_id);
            // A texture of the image cache may not have its backend id yet (created at the next frame):
            // draw it through its ImTextureRef
            const auto& cache = mMarkdownCollection.mLoadedImages;
            auto cached = cache.find(m_img_src);
            if (cached != cache.end() && cached->second.ref.GetTexID() == mdImage->texture_id)
                nfo.texture = cached->second.ref;
            nfo.size = mdImage->size;
            nfo.uv0 = mdImage->uv0;
            nfo.uv1 = mdImage->uv1;
            return image_status::ready;
        }

        void html_div(const std::string& divClass, bool openingDiv) override
        {
            if (!mMarkdownOptions->callbacks.OnHtmlDiv)
                return;

            mMarkdownOptions->callbacks.OnHtmlDiv(divClass, openingDiv);
        }

        bool check_html(const char* str, const char* str_end) override
        {
            // Give the user callback first shot at the tag; fall through to
            // the base class so built-ins (<u>, <br>, <hr>, <sub>, <sup>,
            // <kbd>, <mark>, <img>, <div>) still work if the callback is
            // unset or returns false.
            if (mMarkdownOptions->callbacks.OnHtmlSpan)
            {
                const size_t sz = (size_t)(str_end - str);
                if (sz >= 3 && str[0] == '<')
                {
                    // Skip past '<' and optional '/'
                    const char* p = str + 1;
                    bool opening = true;
                    if (p < str_end && *p == '/') { opening = false; ++p; }
                    // Tag name ends at space, '>', or '/'
                    const char* name_end = p;
                    while (name_end < str_end && *name_end != ' ' && *name_end != '>' && *name_end != '/' && *name_end != '\t')
                        ++name_end;
                    if (name_end > p)
                    {
                        std::string tag(p, name_end);
                        if (mMarkdownOptions->callbacks.OnHtmlSpan(tag, opening))
                            return true;
                    }
                }
            }
            return Renderer::check_html(str, str_end);
        }

        bool can_use_child_windows() const override
        {
            if (!mMarkdownOptions->callbacks.CanUseChildWindows)
                return true;
            return mMarkdownOptions->callbacks.CanUseChildWindows();
        }

        void render_code_block() override
        {
            // remove the last line if empty
            std::string code = m_code_block;
            auto lines = _SplitLines(code);
            if (!lines.empty() && _TrimWhitespace(lines.back()).empty())
            {
                lines.pop_back();
                code = _JoinLines(lines);
            }

            ImGui::PushID(m_code_block.c_str());
            auto& fenced = gCurrentContext->fencedBlockRenderers;
            auto it = fenced.find(_ToLower(m_code_block_language));
            if (it != fenced.end())
                it->second(code);  // a diagram, a widget: its source is not shown, nor searched
            else {
                // In a document's search: the plain code block and the code editor draw its matches; another
                // host's renderer does not, and they are at the top of the block
                std::vector<CodeBlockMatch> matches = code_block_matches(code);
                const float top = ImGui::GetCursorScreenPos().y;
                std::vector<ImRect> rects;
#ifdef IMGUI_RICHMD_WITH_CODE_EDITOR
                using RenderFn = void (*)(const std::string&, const std::string&);
                const RenderFn* render = gHostServices.RenderCodeBlock.target<RenderFn>();
                if (render != nullptr && *render == _RenderCodeBlockWithEditor)  // the host kept the code editor
                    rects = _RenderCodeBlockInEditor(code, m_code_block_language, matches, document != nullptr);
                else
#endif
                if (gHostServices.RenderCodeBlock)
                    gHostServices.RenderCodeBlock(code, m_code_block_language);
                else
                    rects = _RenderCodeBlockPlain(code, matches);
                if (!matches.empty())
                    add_code_block_matches(code, matches, rects, top);
            }
            ImGui::PopID();
        }

        // The formula's texture, from the cache or rendered through the host.
        // nullopt: LaTeX is not available; an entry with an error: the formula is invalid (cached too,
        // so that it is parsed once). The source is shown in both cases.
        std::optional<MarkdownCollection::LatexEntry> GetLatexTexture(const std::string& latex, float fontSizePx, ImU32 color, bool displayStyle) const
        {
            if (!gHostServices.RenderLatex || !gHostServices.UploadRgba)
                return std::nullopt;
            auto& cache = mMarkdownCollection.mLatexCache;
            int frame = ImGui::GetFrameCount();
            std::string key = latex + '\x1f' + std::to_string(fontSizePx) + '\x1f' + std::to_string(color) + (displayStyle ? "D" : "T");
            auto it = cache.find(key);
            if (it != cache.end())
            {
                it->second.lastUsedFrame = frame;
                return it->second;
            }
            for (auto e = cache.begin(); e != cache.end(); )
                if (frame - e->second.lastUsedFrame > gLatexEvictionFrames)
                    e = cache.erase(e);
                else
                    ++e;
            auto bitmap = gHostServices.RenderLatex(latex, fontSizePx, color, displayStyle);
            if (!bitmap)
                return std::nullopt;
            MarkdownCollection::LatexEntry entry;
            entry.error = bitmap->error;
            if (entry.error.empty() && bitmap->width > 0 && bitmap->height > 0)
                entry.texture = gHostServices.UploadRgba(bitmap->rgba.data(), bitmap->width, bitmap->height);
            entry.baselineY = bitmap->baselineY;
            entry.lastUsedFrame = frame;
            cache[key] = entry;
            return entry;
        }

        bool get_latex_texture(const std::string& latex, float fontSizePx, ImU32 color, bool display, latex_texture& out) const override
        {
            auto entry = GetLatexTexture(latex, fontSizePx, color, display);
            if (!entry)
                return false;
            if (!entry->error.empty())
            {
                out.error = entry->error;
                return false;
            }
            out.texture = entry->texture.ref;
            out.size_px = entry->texture.size;
            out.baseline_px = (float)entry->baselineY;
            return true;
        }
    };


    Context::~Context() = default;
    static std::unique_ptr<Context> gDefaultContext;   // the one created by InitializeMarkdown
    static int gContextCount = 0;                      // the contexts alive

    // The current context's renderer, created on first use: this loads the fonts, which is possible
    // any time after ImGui::CreateContext() (nullptr when no context is current)
    static MarkdownRenderer* _Renderer()
    {
        if (!gCurrentContext)
            return nullptr;
        if (!gCurrentContext->renderer)
            gCurrentContext->renderer = std::make_unique<MarkdownRenderer>(&gCurrentContext->options);
        return gCurrentContext->renderer.get();
    }

// Emscripten's FETCH, when the library is linked with -sFETCH (IMGUI_RICHMD_EMSCRIPTEN_FETCH). Not
// for pyodide side modules, where Python installs a JS fetch() based HostServices::Download instead.
#if defined(__EMSCRIPTEN__) && defined(IMGUI_RICHMD_EMSCRIPTEN_FETCH)
#include <emscripten/fetch.h>
#include <mutex>

    // Emscripten async download using emscripten_fetch
    // State for pending downloads
    struct EmscriptenDownloadState {
        MarkdownDownloadStatus status = MarkdownDownloadStatus::NotStarted;
        std::vector<uint8_t> data;
        std::string errorMessage;
    };

    static std::map<std::string, EmscriptenDownloadState> gEmscriptenDownloads;

    static void _emscripten_fetch_success(emscripten_fetch_t *fetch)
    {
        std::string url = fetch->url;
        auto& state = gEmscriptenDownloads[url];
        state.data.assign(
            reinterpret_cast<const uint8_t*>(fetch->data),
            reinterpret_cast<const uint8_t*>(fetch->data) + fetch->numBytes);
        state.status = MarkdownDownloadStatus::Ready;
        emscripten_fetch_close(fetch);
    }

    static void _emscripten_fetch_error(emscripten_fetch_t *fetch)
    {
        std::string url = fetch->url;
        auto& state = gEmscriptenDownloads[url];
        state.status = MarkdownDownloadStatus::Failed;
        state.errorMessage = "HTTP " + std::to_string(fetch->status);
        emscripten_fetch_close(fetch);
    }

    static MarkdownDownloadResult EmscriptenDownloadData(const std::string& url)
    {
        MarkdownDownloadResult result;

        auto it = gEmscriptenDownloads.find(url);
        if (it == gEmscriptenDownloads.end())
        {
            // Start async fetch
            gEmscriptenDownloads[url] = EmscriptenDownloadState{MarkdownDownloadStatus::Downloading, {}, ""};

            emscripten_fetch_attr_t attr;
            emscripten_fetch_attr_init(&attr);
            strcpy(attr.requestMethod, "GET");
            attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
            attr.onsuccess = _emscripten_fetch_success;
            attr.onerror = _emscripten_fetch_error;
            emscripten_fetch(&attr, url.c_str());

            result.status = MarkdownDownloadStatus::Downloading;
            return result;
        }

        auto& state = it->second;
        result.status = state.status;
        if (state.status == MarkdownDownloadStatus::Ready)
        {
            result.data = std::move(state.data);
            gEmscriptenDownloads.erase(it);
        }
        else if (state.status == MarkdownDownloadStatus::Failed)
        {
            result.errorMessage = state.errorMessage;
            gEmscriptenDownloads.erase(it);
        }
        return result;
    }
#endif // __EMSCRIPTEN__ && IMGUI_RICHMD_EMSCRIPTEN_FETCH

    Context* CreateContext(const MarkdownOptions& options)
    {
        Context* context = new Context();
        context->options = options;
#ifdef IMGUI_RICHMD_HOST_HELLO_IMGUI
        Priv_InstallHelloImGuiHost();  // fills the host services the application did not set
#endif
        _InstallDefaultHostServices();
        if (context->options.callbacks.OnDownloadData)  // deprecated option: it becomes the service
            gHostServices.Download = context->options.callbacks.OnDownloadData;
        if (!gHostServices.Download)
        {
#if defined(__EMSCRIPTEN__) && defined(IMGUI_RICHMD_EMSCRIPTEN_FETCH)
            gHostServices.Download = EmscriptenDownloadData;
#elif defined(IMGUI_RICHMD_WITH_DOWNLOAD_IMAGES)
            gHostServices.Download = DesktopDownloadData;
#endif
        }
#ifdef IMGUI_RICHMD_WITH_MERMAID
        context->fencedBlockRenderers["mermaid"] = RenderMermaid;
#endif
        if (!gCurrentContext)
            gCurrentContext = context;
        ++gContextCount;
        return context;
    }

    // What all the contexts share: the downloads in progress, MicroTeX. Freed with the last context.
    static void _ReleaseGlobalResources()
    {
#ifdef IMGUI_RICHMD_WITH_DOWNLOAD_IMAGES
        ClearDesktopDownloads();
#endif
#ifdef IMGUI_RICHMD_WITH_LATEX
        // Release MicroTeX resources (its own texture cache, unused here; safe if Init() was never called)
        if (RichMd::Latex::IsInitialized())
            RichMd::Latex::Release();
        gLatexInitFailed = false;
#endif
    }

    void DestroyContext(Context* context)
    {
        if (!context)
            context = gCurrentContext;
        if (!context)
            return;
        if (gCurrentContext == context)
            gCurrentContext = nullptr;
        if (gDefaultContext.get() == context)
            (void)gDefaultContext.release();  // deleted below
        // Deleting the renderer clears its caches: each cached MarkdownTexture owns its GPU texture
        // (keepAlive), so the textures are freed here, while the rendering backend is still alive.
        // The options' callbacks (which may hold Python objects) go with it.
        delete context;
        _SweepDestroyedTextures();
        if (--gContextCount == 0)
            _ReleaseGlobalResources();
    }

    void SetCurrentContext(Context* context) { gCurrentContext = context; }
    Context* GetCurrentContext() { return gCurrentContext; }

    void InitializeMarkdown(const MarkdownOptions& options)
    {
        if (gDefaultContext)
            return;
        gDefaultContext.reset(CreateContext(options));
        SetCurrentContext(gDefaultContext.get());
    }

    void DeInitializeMarkdown()
    {
        if (gDefaultContext)  // (DestroyContext(nullptr) would destroy the current context, maybe another one)
            DestroyContext(gDefaultContext.get());
    }


    // A PushSelectableText() is popped in the same frame: what an earlier frame left on the stack is an error (reported
    // as a recoverable error of Dear ImGui), then dropped
    static void _CheckSelectableTextStack(Context* context)
    {
        if (context->selectableTextStack.empty() || context->selectableTextFrame == ImGui::GetFrameCount())
            return;
        IM_ASSERT_USER_ERROR(false, "RichMd: PushSelectableText() without PopSelectableText() in the same frame");
        context->selectableTextStack.clear();
    }

    // A BeginDocument() left without its EndDocument() by an earlier frame: reported, then dropped
    static void _CheckDocumentEnded(Context* context)
    {
        if (!context->document || context->documentFrame == ImGui::GetFrameCount())
            return;
        IM_ASSERT_USER_ERROR(false, "RichMd: BeginDocument() without EndDocument() in the same frame");
        if (context->renderer)
            context->renderer->document = nullptr;
        context->document.reset();
        context->documentNesting = 0;
    }

    // Whether the headings fold: the context's option, or the current document's
    static void _SetFoldOptions(Context* context, MarkdownRenderer* renderer)
    {
        renderer->foldableHeadings = context->options.foldableHeadings
                                     || (context->document && context->document->renders.foldable);
        renderer->foldableHeadingsMinLevel = context->options.foldableHeadingsMinLevel;
    }

    // ::md Rendering a fragment
    // Each `Render()` call renders a *fragment*: `Render()` removes its common indentation and resolves its
    // transclusions (once per text: the result is cached in the context), then `RenderRaw()` gives it its own ImGui
    // id scope and hands it to the renderer, which parses it with md4c and draws it. This happens every frame:
    // rich_md keeps no document between frames, only caches (fonts, textures, formulas, diagrams). At the end, the
    // textures the backend has destroyed are freed.
    // ::code
    void RenderRaw(const std::string& markdownString)
    {
        MarkdownRenderer* renderer = _Renderer();
        if (!renderer)
        {
            std::cerr << "RichMd::Render : Markdown was not initialized!\n";
            return;
        }
        // Each fragment rendered in a frame gets its own id scope (two identical fragments must not collide)
        Context* context = gCurrentContext;
        int frame = ImGui::GetFrameCount();
        if (context->fragmentFrame != frame)
        {
            context->fragmentFrame = frame;
            context->fragmentCounter = 0;
        }
        ImGui::PushID(context->fragmentCounter++);
        // Whether its text can be selected: the last PushSelectableText(), else the option
        _CheckSelectableTextStack(context);
        _CheckDocumentEnded(context);
        renderer->selectableText = context->selectableTextStack.empty()
            ? context->options.selectableText : context->selectableTextStack.back();
        _SetFoldOptions(context, renderer);
        renderer->Render(markdownString);
        ImGui::PopID();
        _SweepDestroyedTextures();
    }
    // ::endcode

    const std::vector<Heading>& LastRenderHeadings()
    {
        static const std::vector<Heading> kNone;
        MarkdownRenderer* renderer = _Renderer();
        return renderer ? renderer->headings() : kNone;
    }

    void FoldAllHeadings(bool folded)
    {
        if (MarkdownRenderer* renderer = _Renderer())
            renderer->fold_all(folded);
    }

    // ::md Documents
    // A document is two child windows: the content, where the renders put their headings, the anchors clicked in them
    // and the matches of the search (into the context's document), and the table of contents beside it.
    // `BeginDocument()` decides the layout from the state the document kept at the last frame (the number of its
    // headings, the panel's width, whether the reader hid it), hands the query to the renders, and opens the content.
    // `EndDocument()` works in the content's window (the state of its collapsed sections, its scroll): it finds the
    // current match among those of this frame, and makes one frame of the scroll in progress. Then it closes the
    // content, draws the table of contents from the headings of this frame, which are complete, and the find bar over
    // the content. A scroll (to an anchor, an entry of the table of contents, a match) measures its target again at
    // each frame: eased over the animation, then corrected until it holds.
    // ::code
    static constexpr float kPanelMinEm = 8.f;      // the narrowest table of contents
    static constexpr float kContentMinEm = 16.f;   // the narrowest content beside it

    void BeginDocument(const char* id, ImVec2 size, const DocumentOptions& options)
    {
        IM_ASSERT(gCurrentContext && "RichMd: call CreateContext first");
        Context* context = gCurrentContext;
        _CheckDocumentEnded(context);
        if (context->document) {
            IM_ASSERT_USER_ERROR(false, "RichMd: BeginDocument() inside a document (one document at a time)");
            context->documentNesting++;  // only a child window: its renders belong to the outer document
            ImGui::BeginChild(id, size);
            return;
        }
        auto frame = std::make_unique<DocumentFrame>();
        frame->id = ImGui::GetID(id);
        frame->options = options;
        DocumentState& state = context->documents[frame->id];
        ImGui::BeginChild(id, size);
        frame->origin = ImGui::GetCursorPos();
        frame->size = ImGui::GetContentRegionAvail();
        const float em = ImGui::GetFontSize();
        const bool toc = options.toc && state.headingCount >= options.tocMinHeadings;
        frame->narrow = frame->size.x < options.narrowWidth * em;
        frame->panel = toc && state.panelShown && !frame->narrow;
        frame->line = toc && !frame->panel;
        const ImGuiChildFlags contentFlags = ImGuiChildFlags_AlwaysUseWindowPadding;
        if (state.focusContent) {
            ImGui::SetNextWindowFocus();
            state.focusContent = false;
        }
        ImVec2 contentPos = frame->origin, contentSize = frame->size;
        if (frame->panel) {
            float maxEm = ImMax(kPanelMinEm, frame->size.x / em - kContentMinEm);
            state.panelWidth = ImClamp(state.panelWidth, kPanelMinEm, maxEm);
            frame->panelWidth = state.panelWidth * em;
            bool touch = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_IsTouchScreen) != 0;
            frame->splitterWidth = em * (touch ? 1.f : 0.5f);  // a finger needs a wider edge
            float left = frame->panelWidth + frame->splitterWidth;
            contentPos.x += left;
            contentSize.x -= left;
        } else if (frame->line) {
            SizedFont font = GetFont(MarkdownFontSpec());  // the line is drawn in the markdown font
            ImGui::PushFont(font.font, font.size);
            float lineHeight = ImGui::GetFrameHeightWithSpacing();
            ImGui::PopFont();
            contentPos.y += lineHeight;
            contentSize.y -= lineHeight;
        }
        if (frame->narrow && options.search && state.searchOpen) {  // the find bar above the content, not over it
            float room = state.findBarHeight + ImGui::GetStyle().ItemSpacing.y;
            contentPos.y += room;
            contentSize.y -= room;
        }
        ImGui::SetCursorPos(contentPos);
        ImGui::BeginChild("##content", contentSize, contentFlags);
        frame->scrollY = ImGui::GetScrollY();
        frame->renders.contentStartY = ImGui::GetCursorPosY();
        frame->renders.foldable = context->options.foldableHeadings || options.foldableHeadings;
        if (frame->renders.foldable) {  // the margin of the fold arrows, at the left of all the content
            frame->renders.foldMarginLeft = ImGui::GetCursorScreenPos().x;
            frame->renders.foldMargin = GetFont(MarkdownFontSpec()).size * 1.5f;
            ImGui::Indent(frame->renders.foldMargin);
        }
        frame->renders.foldAll = state.foldAll;  // asked by the table of contents at the last frame
        state.foldAll = 0;
        if (options.search && state.searchOpen) {  // the renders find the matches of the query, and draw them
            frame->renders.query = state.query;
            frame->renders.searchOptions = state.searchOptions;
            frame->renders.highlightAll = state.highlightAll;
            frame->renders.currentFragment = state.currentFragment;
            frame->renders.currentBegin = state.currentBegin;
        }
        frame->renders.selectFragment = state.selectFragment;  // once
        frame->renders.selectBegin = state.selectBegin;
        frame->renders.selectEnd = state.selectEnd;
        state.selectFragment = -1;
        context->document = std::move(frame);
        context->documentFrame = ImGui::GetFrameCount();
        if (MarkdownRenderer* renderer = _Renderer())
            renderer->document = &context->document->renders;
    }

    // The scrolls of a document: to a heading (an anchor, a click in the table of contents) or to a match
    static void _ScrollToHeading(DocumentState& state, const std::string& slug)
    {
        state.scroll = DocumentScroll();
        state.scroll.anchor.slug = slug;
        state.scroll.frames = 8;
    }
    static void _ScrollToMatch(DocumentState& state, int fragment, size_t begin)
    {
        state.scroll = DocumentScroll();
        state.scroll.matchFragment = fragment;
        state.scroll.matchBegin = begin;
        state.scroll.frames = 8;
    }

    // The duration of an animated scroll: none when the options or the system ask for none
    static float _ScrollAnimationSeconds(const DocumentOptions& options)
    {
        if (options.scrollAnimation == ScrollAnimation::Never)
            return 0.f;
        if (options.scrollAnimation == ScrollAnimation::FollowSystem && gHostServices.PrefersReducedMotion
            && gHostServices.PrefersReducedMotion())
            return 0.f;
        return options.scrollAnimationSeconds;
    }

    // One frame of a scroll toward `scroll`, in the content's window: eased over the animation (fast at first, slow at
    // the end), then corrected until it holds. The wheel stops it. Returns true when it is over.
    static bool _ScrollToward(DocumentScroll& s, float scroll, float seconds)
    {
        if (ImGui::GetIO().MouseWheel != 0.f && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
            return true;  // the reader took over
        if (!s.animated) {
            if (s.start < 0.0 && seconds > 0.f) {
                s.from = ImGui::GetScrollY();
                s.start = ImGui::GetTime();
            }
            float t = (s.start < 0.0) ? 1.f : (float)(ImGui::GetTime() - s.start) / seconds;
            if (t < 1.f) {
                float eased = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
                ImGui::SetScrollY(s.from + (scroll - s.from) * eased);
                return false;
            }
            s.animated = true;
        }
        return CorrectScroll(scroll, s.frames);
    }

    // The scroll the document is making, in the content's window: its target is measured again at each frame
    static void _UpdateScroll(Context* context, const DocumentFrame& frame, DocumentState& state)
    {
        DocumentScroll& s = state.scroll;
        const float seconds = _ScrollAnimationSeconds(frame.options);
        if (!s.anchor.slug.empty()) {
            const DocumentRenders& renders = frame.renders;
            int i = FindAnchorHeading(s.anchor, renders.headings, renders.headingDetails);
            if (i == -2) {  // an anchor that no heading of the document has
                if (context->options.callbacks.OnOpenLink)
                    context->options.callbacks.OnOpenLink("#" + s.anchor.slug);
                s = DocumentScroll();
            } else if (i >= 0 && _ScrollToward(s, ScrollToShowAtTop(renders.headings[(size_t)i].y), seconds))
                s = DocumentScroll();
        } else if (s.matchFragment >= 0) {
            const DocumentMatch* match = nullptr;
            for (const DocumentMatch& m : frame.renders.matches)
                if (m.fragment == s.matchFragment && m.begin == s.matchBegin)
                    match = &m;
            if (match != nullptr && match->details != 0 && --s.frames > 0) {
                ImGui::GetStateStorage()->SetInt(match->details, 1);  // the section opens at the next frame
                return;
            }
            // The match comes below the find bar (and its list), else at a third of the view
            const float height = ImGui::GetWindowHeight(), em = ImGui::GetFontSize();
            float top = state.findBarBottom > 0.f ? ImMin(state.findBarBottom + em, height - 3.f * em) : height / 3.f;
            if (match == nullptr || _ScrollToward(s, ScrollToShowAtTop(match->y - top), seconds))
                s = DocumentScroll();
        }
    }

    // The current match, kept by its render and its first byte: found again at each frame. A new query (or a text that
    // changed) makes the first match below the top of the view current; a step goes to the next or the previous one,
    // and wraps around. The view scrolls to a new current match when it does not show it.
    static void _UpdateCurrentMatch(const DocumentFrame& frame, DocumentState& state)
    {
        const std::vector<DocumentMatch>& matches = frame.renders.matches;
        state.matchCount = (int)matches.size();
        if (frame.renders.query.empty() || matches.empty()) {
            state.currentMatch = -1;
            state.matchMoved = false;
            state.wrapMessage = nullptr;
            state.step = 0;
            state.jumpToFirst = false;
            return;
        }
        int current = -1;
        for (int i = 0; i < (int)matches.size(); ++i)
            if (matches[(size_t)i].fragment == state.currentFragment && matches[(size_t)i].begin == state.currentBegin)
                current = i;
        const float viewTop = frame.scrollY + state.findBarBottom;
        const float viewBottom = frame.scrollY + ImGui::GetWindowHeight();
        bool scroll = false;
        if (current < 0 || state.jumpToFirst) {
            current = 0;
            for (int i = 0; i < (int)matches.size(); ++i)
                if (matches[(size_t)i].y >= viewTop) {
                    current = i;
                    break;
                }
            scroll = state.jumpToFirst;
            if (state.jumpToFirst)
                state.wrapMessage = nullptr;
            state.jumpToFirst = false;
        }
        if (state.step != 0) {  // past an end: at once to the other end, and the find bar says so
            const int n = (int)matches.size(), next = current + state.step;
            state.wrapMessage = next >= n ? "Reached the end, continued from the top."
                                : next < 0 ? "Reached the top, continued from the end." : nullptr;
            current = (next + n) % n;
            state.step = 0;
            scroll = true;
        }
        const DocumentMatch& match = matches[(size_t)current];
        state.matchMoved = current != state.currentMatch;
        state.currentMatch = current;
        state.currentFragment = match.fragment;
        state.currentBegin = match.begin;
        if (scroll && (match.details != 0 || match.y < viewTop || match.bottom > viewBottom))
            _ScrollToMatch(state, match.fragment, match.begin);
    }

    // The icons of the document's buttons, drawn (the fonts need no glyph)
    enum class _Icon { Close, Find, List };
    static bool _IconButton(const char* id, _Icon icon)
    {
        const float size = ImGui::GetFrameHeight();
        const bool clicked = ImGui::Button(id, ImVec2(size, size));
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        const float r = size * 0.22f, thickness = ImMax(1.5f, size / 20.f);
        const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (icon == _Icon::Close) {
            drawList->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), color, thickness);
            drawList->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), color, thickness);
        } else if (icon == _Icon::Find) {  // a lens and its handle
            const ImVec2 lens(c.x - r * 0.3f, c.y - r * 0.3f);
            drawList->AddCircle(lens, r * 0.75f, color, 0, thickness);
            drawList->AddLine(ImVec2(lens.x + r * 0.55f, lens.y + r * 0.55f), ImVec2(c.x + r * 1.1f, c.y + r * 1.1f),
                              color, thickness * 1.3f);
        } else {  // three lines
            for (int k = -1; k <= 1; ++k) {
                const float y = c.y + (float)k * r * 0.8f;
                drawList->AddLine(ImVec2(c.x - r, y), ImVec2(c.x + r, y), color, thickness);
            }
        }
        return clicked;
    }

    // What EndDocument() gathers for the table of contents and the lists of matches
    struct _DocumentView
    {
        std::vector<int> listed;    // the headings in the table of contents
        int current = -1;           // the current section: the last listed heading at or above the top of the view
        std::vector<int> sections;  // per match: the listed heading above it (-1: none)
        std::vector<int> counts;    // per heading: its matches, until the next listed heading
    };

    static void _OpenSearch(DocumentState& state)
    {
        state.searchOpen = true;
        state.focusQuery = state.selectQuery = true;
    }
    // Closing the find bar: its current match becomes the selection (selected by its render at the next frame), and the
    // content takes the focus back (else it falls out of the document, and Ctrl+F no longer reaches it)
    static void _CloseSearch(const DocumentFrame& frame, DocumentState& state)
    {
        state.searchOpen = false;
        state.focusContent = true;
        state.wrapMessage = nullptr;
        if (state.currentMatch >= 0 && state.currentMatch < (int)frame.renders.matches.size()
            && frame.renders.matches[(size_t)state.currentMatch].inRuns) {
            const DocumentMatch& match = frame.renders.matches[(size_t)state.currentMatch];
            state.selectFragment = match.fragment;
            state.selectBegin = match.begin;
            state.selectEnd = match.end;
        }
    }

    static void _GoToMatch(DocumentState& state, const DocumentMatch& match)
    {
        state.wrapMessage = nullptr;
        state.currentFragment = match.fragment;
        state.currentBegin = match.begin;
        _ScrollToMatch(state, match.fragment, match.begin);
    }

    // The height of a row of the lists (the table of contents, the matches): a finger needs a taller one
    static float _RowHeight()
    {
        const bool touch = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_IsTouchScreen) != 0;
        return ImGui::GetTextLineHeight() * (touch ? 2.f : 1.f);
    }

    // The end of a text that fits in a width, cut at a space, with "..." in front when cut ("" when no word fits)
    static std::string _FitLeft(const std::string& text, float width)
    {
        size_t start = text.find_first_not_of(' ');
        std::string s = start == std::string::npos ? std::string() : text.substr(start);
        if (ImGui::CalcTextSize(s.c_str()).x <= width)
            return s;
        for (size_t space = s.find(' '); space != std::string::npos; space = s.find(' ', space + 1)) {
            std::string candidate = "..." + s.substr(space + 1);
            if (ImGui::CalcTextSize(candidate.c_str()).x <= width)
                return candidate;
        }
        return std::string();
    }
    // The start of a text that fits in a width, cut at a space, with "..." at the end when cut ("" when no word fits)
    static std::string _FitRight(const std::string& text, float width)
    {
        if (ImGui::CalcTextSize(text.c_str()).x <= width)
            return text;
        for (size_t space = text.rfind(' '); space != std::string::npos && space > 0;
             space = text.rfind(' ', space - 1)) {
            std::string candidate = text.substr(0, space) + "...";
            if (ImGui::CalcTextSize(candidate.c_str()).x <= width)
                return candidate;
        }
        return std::string();
    }

    // A match with its context on one line, "before MATCH after": the context is cut to fit, the match always shows
    static void _DrawSnippet(const DocumentMatch& match, ImVec2 pos, float width)
    {
        const float matchWidth = ImGui::CalcTextSize(match.text.c_str()).x;
        const float room = ImMax(0.f, width - matchWidth);
        const std::string before = _FitLeft(match.before, room * 0.4f);
        const std::string after = _FitRight(match.after, room - ImGui::CalcTextSize(before.c_str()).x);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
        drawList->AddText(pos, color, before.c_str());
        pos.x += ImGui::CalcTextSize(before.c_str()).x;
        drawList->AddRectFilled(pos, ImVec2(pos.x + matchWidth, pos.y + ImGui::GetTextLineHeight()),
                                ImGui::GetColorU32(_Renderer()->style.searchMatch));
        drawList->AddText(pos, color, match.text.c_str());
        drawList->AddText(ImVec2(pos.x + matchWidth, pos.y), color, after.c_str());
    }

    static const char* _SectionTitle(const DocumentFrame& frame, const _DocumentView& view, int match)
    {
        const int k = view.sections[(size_t)match];
        return k >= 0 ? frame.renders.headings[(size_t)k].text.c_str() : "(top)";
    }

    // A row of a list of matches (width 0: the available width). Returns true when it was clicked.
    static bool _MatchRow(const DocumentMatch& match, int i, const DocumentState& state, float width)
    {
        const float rowHeight = _RowHeight();
        if (width <= 0.f)
            width = ImGui::GetContentRegionAvail().x;
        ImGui::PushID(i);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::Selectable("##match", i == state.currentMatch, 0, ImVec2(width, rowHeight));
        if (i == state.currentMatch && (ImGui::IsWindowAppearing() || state.matchMoved))
            ImGui::SetScrollHereY(0.5f);  // the list shows the current match
        const float indent = ImGui::GetFontSize() * 0.5f;
        _DrawSnippet(match, ImVec2(pos.x + indent, pos.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f),
                     width - indent);
        ImGui::PopID();
        return clicked;
    }

    // The matches with their context, under the titles of their sections: the find bar's dropdown, the Search tab of
    // the table of contents. A click goes to a match. Returns true when one was clicked.
    static constexpr int kListedMatchesMax = 300;
    static bool _DrawMatchList(const DocumentFrame& frame, DocumentState& state, const _DocumentView& view)
    {
        const std::vector<DocumentMatch>& matches = frame.renders.matches;
        if (!state.searchOpen) {
            if (ImGui::Button("Find in the document")) {
                _OpenSearch(state);
                ImGui::CloseCurrentPopup();  // the line's menu
            }
            return false;
        }
        if (frame.renders.query.empty() || matches.empty()) {
            ImGui::TextDisabled(frame.renders.query.empty() ? "Type in the find bar." : "No match.");
            return false;
        }
        bool clicked = false;
        int section = -2;
        const int shown = ImMin((int)matches.size(), kListedMatchesMax);
        for (int i = 0; i < shown; ++i) {
            if (view.sections[(size_t)i] != section) {
                section = view.sections[(size_t)i];
                ImGui::TextDisabled("%s", _SectionTitle(frame, view, i));
            }
            if (_MatchRow(matches[(size_t)i], i, state, 0.f)) {
                _GoToMatch(state, matches[(size_t)i]);
                clicked = true;
            }
        }
        if (shown < (int)matches.size())
            ImGui::TextDisabled("%d more matches", (int)matches.size() - shown);
        return clicked;
    }

    // The matches as ticks on the content's scrollbar (in the content's window). A hover shows their context (not on
    // a touch screen); a click or a tap opens a small window that lists them.
    static void _DrawScrollbarTicks(const DocumentFrame& frame, DocumentState& state, const _DocumentView& view)
    {
        SizedFont font = GetFont(MarkdownFontSpec());
        ImGui::PushFont(font.font, font.size);
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const std::vector<DocumentMatch>& matches = frame.renders.matches;
        const float em = ImGui::GetFontSize();
        const float width = em * 24.f;  // the width of the context
        if (window->ScrollbarY && !matches.empty()) {
            const ImRect bar = ImGui::GetWindowScrollbarRect(window, ImGuiAxis_Y);
            const float contentHeight = window->ScrollMax.y + window->Size.y;
            const float half = ImMax(2.f, em * 0.15f);
            const Style& style = _Renderer()->style;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(bar.Min, bar.Max);
            std::vector<int> hovered;
            for (int i = 0; i < (int)matches.size(); ++i) {
                const bool current = i == state.currentMatch;
                if (!current && !state.highlightAll)
                    continue;
                const float y = bar.Min.y + matches[(size_t)i].y / contentHeight * bar.GetHeight();
                ImVec4 color = current ? style.searchMatchCurrent : style.searchMatch;
                color.w = 1.f;  // opaque, on the scrollbar
                drawList->AddRectFilled(ImVec2(bar.Min.x + 1.f, y - half), ImVec2(bar.Max.x - 1.f, y + half),
                                        ImGui::GetColorU32(color));
                const ImVec2 hoverMin(bar.Min.x, y - 2.f * half), hoverMax(bar.Max.x, y + 2.f * half);
                if (ImGui::IsMouseHoveringRect(hoverMin, hoverMax, false))  // the scrollbar is outside the clip rect
                    hovered.push_back(i);
            }
            drawList->PopClipRect();
            if (!hovered.empty() && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
                const ImVec2 at(bar.Min.x - em * 0.5f, ImGui::GetMousePos().y);
                if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_IsTouchScreen)) {
                    ImGui::SetNextWindowPos(at, ImGuiCond_Always, ImVec2(1.f, 0.5f));
                    ImGui::BeginTooltip();
                    for (size_t k = 0; k < hovered.size() && k < 5; ++k) {
                        ImGui::TextDisabled("%s", _SectionTitle(frame, view, hovered[k]));
                        const ImVec2 pos = ImGui::GetCursorScreenPos();
                        ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
                        _DrawSnippet(matches[(size_t)hovered[k]], pos, width);
                    }
                    ImGui::EndTooltip();
                }
                const ImGuiMouseButton left = ImGuiMouseButton_Left;
                if (ImGui::IsMouseReleased(left) && !ImGui::IsMouseDragPastThreshold(left)) {  // a click, not a drag
                    state.tickMatches = hovered;
                    state.tickPopupPos = at;
                    ImGui::OpenPopup("##ticks");
                }
            }
        }
        ImGui::SetNextWindowPos(state.tickPopupPos, ImGuiCond_Appearing, ImVec2(1.f, 0.5f));
        if (ImGui::BeginPopup("##ticks")) {
            const int n = (int)state.tickMatches.size();
            ImGui::TextDisabled(n == 1 ? "1 match here" : "%d matches here", n);
            for (int i : state.tickMatches)
                if (i < (int)matches.size() && _MatchRow(matches[(size_t)i], i, state, width))
                    _GoToMatch(state, matches[(size_t)i]);  // the selectable closes the window
            ImGui::EndPopup();
        }
        ImGui::PopFont();
    }

    // The find bar, over the top right of the content (in a narrow document, above it): the query, the count, the
    // previous and the next match, the list of the matches, the options. Enter goes to the next match, Shift+Enter to
    // the previous, Escape closes it.
    static void _DrawFindBar(const DocumentFrame& frame, DocumentState& state, const _DocumentView& view,
                             ImVec2 contentMin, ImVec2 contentMax)
    {
        SizedFont font = GetFont(MarkdownFontSpec());
        ImGui::PushFont(font.font, font.size);
        const float em = ImGui::GetFontSize();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float pad = em * 0.4f;
        float width = contentMax.x - contentMin.x;
        if (frame.narrow)  // BeginDocument() made its room
            ImGui::SetCursorScreenPos(ImVec2(contentMin.x, contentMin.y - state.findBarHeight - style.ItemSpacing.y));
        else {
            width = ImMin(em * 30.f, width - style.ScrollbarSize - 2.f * pad);
            ImGui::SetCursorScreenPos(ImVec2(contentMax.x - style.ScrollbarSize - pad - width, contentMin.y + pad));
        }
        // A toolbar's color: the fields stand out on it (in a light theme, the popups are as white as the fields)
        ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg);
        background.w = 1.f;  // opaque: the text below does not show through
        ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
        ImGuiChildFlags flags =
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding;
        ImGui::BeginChild("##findbar", ImVec2(width, 0.f), flags);
        ImGui::PopStyleColor();

        const float button = ImGui::GetFrameHeight();
        const float countWidth = ImGui::CalcTextSize("000/000").x;
        char count[32] = "";
        if (state.matchCount > 0)
            snprintf(count, sizeof(count), "%d/%d", state.currentMatch + 1, state.matchCount);
        else if (state.query[0] != '\0')
            snprintf(count, sizeof(count), "0/0");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 4.f * (button + style.ItemSpacing.x) - countWidth
                                - style.ItemSpacing.x);
        if (state.focusQuery) {
            ImGui::SetKeyboardFocusHere();
            state.focusQuery = false;
        }
        std::string query = state.query;
        auto callback = [](ImGuiInputTextCallbackData* data) {
            DocumentState& state = *(DocumentState*)data->UserData;
            if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {  // the arrows: the previous, the next match
                state.step = (data->EventKey == ImGuiKey_UpArrow) ? -1 : 1;
                if (data->EventKey == ImGuiKey_DownArrow)
                    state.dropdownOpen = true;  // the list shows them with their context
            } else if (state.selectQuery) {
                data->SelectAll();
                state.selectQuery = false;
            }
            return 0;
        };
        const ImGuiInputTextFlags queryFlags = ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackAlways;
        if (ImGui::InputTextWithHint("##query", "Find in the document", state.query, sizeof(state.query), queryFlags,
                                     callback, &state))
            state.jumpToFirst = true;
        const bool typing = ImGui::IsItemActive() || ImGui::IsItemDeactivated();  // Enter and Escape deactivate it
        if (typing && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
            state.step = ImGui::GetIO().KeyShift ? -1 : 1;
            state.focusQuery = true;  // Enter leaves the field: the focus comes back
        }
        if (typing && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            // Escape reverts the field to its text at its activation: the query stays for the next search
            ImStrncpy(state.query, query.c_str(), sizeof(state.query));
            _CloseSearch(frame, state);
        }
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + countWidth - ImGui::CalcTextSize(count).x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(count);
        ImGui::SameLine();
        if (ImGui::ArrowButton("##previous", ImGuiDir_Up))
            state.step = -1;
        ImGui::SameLine();
        if (ImGui::ArrowButton("##next", ImGuiDir_Down))
            state.step = 1;
        ImGui::SameLine();
        if (_IconButton("##list", _Icon::List))
            state.dropdownOpen = !state.dropdownOpen;
        ImGui::SameLine();
        if (_IconButton("##close", _Icon::Close))
            _CloseSearch(frame, state);

        // The options, on one row when it fits. No option for the diacritics: the search always ignores them, as the
        // browsers do by default ("cafe" finds "café")
        auto option = [&style](const char* label, bool* value, bool first) {
            if (!first) {
                ImGui::SameLine();
                if (ImGui::GetContentRegionAvail().x < ImGui::GetFrameHeight() + style.ItemInnerSpacing.x
                                                           + ImGui::CalcTextSize(label).x)
                    ImGui::NewLine();
            }
            ImGui::Checkbox(label, value);
        };
        option("Match case", &state.searchOptions.matchCase, true);
        option("Whole words", &state.searchOptions.wholeWords, false);
        option("Highlight all", &state.highlightAll, false);
        if (state.wrapMessage != nullptr) {
            const ImVec4 color = _Renderer()->admonition_color(Renderer::AdmonitionKind::Warning);
            ImGui::TextColored(color, "%s", state.wrapMessage);
        }

        if (state.dropdownOpen && !frame.renders.query.empty()) {  // the matches with their context, under the bar
            ImGui::Separator();
            const float rows = (float)ImMin(ImMax((int)frame.renders.matches.size(), 1), 10) + 1.f;
            float height = rows * (_RowHeight() + style.ItemSpacing.y);
            const float below = contentMax.y - ImGui::GetCursorScreenPos().y - style.WindowPadding.y - 2.f * pad;
            height = ImMin(height, frame.narrow ? frame.size.y * 0.35f : below);  // narrow: the content keeps its room
            ImGui::BeginChild("##matches", ImVec2(0.f, ImMax(height, ImGui::GetFrameHeight())));
            _DrawMatchList(frame, state, view);
            ImGui::EndChild();
        }
        ImGui::EndChild();
        state.findBarHeight = ImGui::GetItemRectSize().y;
        state.findBarBottom = frame.narrow ? 0.f : ImGui::GetItemRectMax().y - contentMin.y;  // what it covers
        ImGui::PopFont();
    }

    // The entries of the table of contents (the panel's, or the menu's): a click scrolls to the entry's heading.
    // Above them, when the headings fold: fold all, unfold all (done at the next frame; a menu stays open).
    // Returns true when an entry was clicked.
    static bool _DrawTocEntries(const DocumentFrame& frame, DocumentState& state, const _DocumentView& view)
    {
        if (frame.renders.foldMargin > 0.f) {
            if (ImGui::SmallButton("Fold all"))
                state.foldAll = 1;
            ImGui::SameLine();
            if (ImGui::SmallButton("Unfold all"))
                state.foldAll = -1;
        }
        const std::vector<Heading>& headings = frame.renders.headings;
        const int current = view.current;
        int minLevel = 6;
        for (int k : view.listed)
            minLevel = ImMin(minLevel, headings[k].level);
        const float lineHeight = ImGui::GetTextLineHeight();
        const float rowHeight = _RowHeight();
        const bool searching = !frame.renders.query.empty();
        bool clicked = false;
        for (int k : view.listed) {
            const Heading& heading = headings[k];
            ImGui::PushID(k);
            if (ImGui::Selectable("##entry", k == current, 0, ImVec2(0.f, rowHeight))) {
                _ScrollToHeading(state, heading.slug);
                clicked = true;
            }
            if (k == current && state.currentSection != current && !ImGui::IsItemVisible())
                ImGui::SetScrollHereY(0.5f);  // the table of contents follows the reader
            ImVec2 pos = ImGui::GetItemRectMin();
            pos.x += ImGui::GetStyle().FramePadding.x + (float)(heading.level - minLevel) * ImGui::GetFontSize();
            pos.y += (rowHeight - lineHeight) * 0.5f;
            // A heading that a collapsed section hides is dimmed (a click opens the section), and while searching, a
            // section without matches; the others show their count
            const int count = searching ? view.counts[(size_t)k] : 0;
            const bool dim = heading.hidden || (searching && count == 0);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImU32 color = ImGui::GetColorU32(dim ? ImGuiCol_TextDisabled : ImGuiCol_Text);
            drawList->AddText(pos, color, heading.text.c_str());
            if (count > 0) {
                char label[32];
                snprintf(label, sizeof(label), "  (%d)", count);
                pos.x += ImGui::CalcTextSize(heading.text.c_str()).x;
                drawList->AddText(pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
            }
            ImGui::PopID();
        }
        return clicked;
    }

    // The table of contents, and the matches beside it in a second tab
    static void _DrawTocTabs(const DocumentFrame& frame, DocumentState& state, const _DocumentView& view)
    {
        if (!ImGui::BeginTabBar("##tabs"))
            return;
        if (ImGui::BeginTabItem("Contents")) {
            _DrawTocEntries(frame, state, view);
            ImGui::EndTabItem();
        }
        char label[48];
        if (frame.renders.query.empty())
            snprintf(label, sizeof(label), "Search###search");
        else
            snprintf(label, sizeof(label), "Search (%d)###search", (int)frame.renders.matches.size());
        if (ImGui::BeginTabItem(label)) {
            _DrawMatchList(frame, state, view);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // The table of contents beside the content, and its edge, dragged to resize it
    static void _DrawTocPanel(const DocumentFrame& frame, DocumentState& state, const _DocumentView& view)
    {
        ImGui::SetCursorPos(frame.origin);
        ImGui::BeginChild("##toc", ImVec2(frame.panelWidth, frame.size.y), ImGuiChildFlags_Borders);
        SizedFont font = GetFont(MarkdownFontSpec());
        ImGui::PushFont(font.font, font.size);
        if (ImGui::ArrowButton("##hide", ImGuiDir_Left))
            state.panelShown = false;
        ImGui::SameLine();
        if (frame.options.search)
            _DrawTocTabs(frame, state, view);
        else {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Contents");
            ImGui::Separator();
            _DrawTocEntries(frame, state, view);
        }
        ImGui::PopFont();
        ImGui::EndChild();

        ImGui::SetCursorPos(ImVec2(frame.origin.x + frame.panelWidth, frame.origin.y));
        ImGui::InvisibleButton("##edge", ImVec2(frame.splitterWidth, frame.size.y));
        const bool active = ImGui::IsItemActive();
        if (active)
            state.panelWidth += ImGui::GetIO().MouseDelta.x / ImGui::GetFontSize();
        if (active || ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            float x = (a.x + b.x) * 0.5f;
            ImU32 color = ImGui::GetColorU32(active ? ImGuiCol_SeparatorActive : ImGuiCol_SeparatorHovered);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), color, 2.f);
        }
    }

    // The table of contents hidden, or a narrow document: a line above the content, with the current section, which
    // opens the table of contents as a menu (a combo box), a button that shows the panel again when there is room, and
    // a magnifier that opens the find bar (a touch screen has no Ctrl+F)
    static void _DrawTocLine(const DocumentFrame& frame, DocumentState& state, const _DocumentView& view)
    {
        const int current = view.current;
        ImGui::SetCursorPos(frame.origin);
        SizedFont font = GetFont(MarkdownFontSpec());
        ImGui::PushFont(font.font, font.size);
        if (!frame.narrow) {  // the panel has no room in a narrow document
            if (ImGui::ArrowButton("##show", ImGuiDir_Right))
                state.panelShown = true;
            ImGui::SameLine();
        }
        const std::string& title = current >= 0 ? frame.renders.headings[current].text : std::string("Contents");
        const float findWidth = frame.options.search ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.f;
        ImGui::SetNextItemWidth(-FLT_MIN - findWidth);
        if (ImGui::BeginCombo("##section", title.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (frame.options.search)  // a click on an entry or a match closes the menu
                _DrawTocTabs(frame, state, view);
            else
                _DrawTocEntries(frame, state, view);
            ImGui::EndCombo();
        }
        if (frame.options.search) {
            ImGui::SameLine();
            if (_IconButton("##find", _Icon::Find))
                _OpenSearch(state);
        }
        ImGui::PopFont();
    }

    void EndDocument()
    {
        Context* context = gCurrentContext;
        if (context && context->documentNesting > 0) {
            context->documentNesting--;
            ImGui::EndChild();
            return;
        }
        if (!context || !context->document) {
            IM_ASSERT_USER_ERROR(false, "RichMd: EndDocument() without BeginDocument()");
            return;
        }
        DocumentFrame& frame = *context->document;
        DocumentState& state = context->documents[frame.id];
        const std::vector<Heading>& headings = frame.renders.headings;

        // In the content's window: fold all or unfold all (the states of the renders' folds are there), the anchor
        // clicked in a render, the current match, the scroll in progress
        if (frame.renders.foldAll != 0)
            for (ImGuiID id : frame.renders.headingFolds)
                if (id != 0)
                    ImGui::GetStateStorage()->SetInt(id, frame.renders.foldAll > 0 ? 0 : 1);
        if (!frame.renders.clickedAnchor.empty())
            _ScrollToHeading(state, frame.renders.clickedAnchor);
        _UpdateCurrentMatch(frame, state);
        _UpdateScroll(context, frame, state);

        // The entries of the table of contents, and the current section: the last shown heading at or above the top
        // of the view
        _DocumentView view;
        for (int k = 0; k < (int)headings.size(); ++k) {
            if (headings[k].level > frame.options.tocMaxLevel)
                continue;
            view.listed.push_back(k);
            float top = ImMax(frame.scrollY, frame.renders.contentStartY) + ImGui::GetFontSize() * 0.5f;
            if (!headings[k].hidden && headings[k].y <= top)
                view.current = k;
        }
        // The section of each match (the matches and the headings are in the order of the document)
        view.counts.assign(headings.size(), 0);
        size_t next = 0;
        int section = -1;
        for (const DocumentMatch& match : frame.renders.matches) {
            while (next < view.listed.size() && headings[(size_t)view.listed[next]].y <= match.y + 1.f)
                section = view.listed[next++];
            view.sections.push_back(section);
            if (section >= 0)
                view.counts[(size_t)section]++;
        }
        if (frame.options.search)
            _DrawScrollbarTicks(frame, state, view);
        if (frame.renders.foldMargin > 0.f)
            ImGui::Unindent(frame.renders.foldMargin);
        ImGui::EndChild();  // the content
        const ImVec2 contentMin = ImGui::GetItemRectMin(), contentMax = ImGui::GetItemRectMax();

        state.headingCount = (int)view.listed.size();
        if (frame.panel)
            _DrawTocPanel(frame, state, view);
        else if (frame.line)
            _DrawTocLine(frame, state, view);
        state.currentSection = view.current;

        // The search: Ctrl+F (Cmd+F on macOS) in the document opens the find bar, drawn over the content; so does "/"
        // (the web's convention, where the browser takes Cmd+F too), when no text field is active. "/" is read as a
        // typed character: on some keyboard layouts, it is Shift with another key.
        if (frame.options.search) {
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F))  // routed to the focused window and its parents
                _OpenSearch(state);
            const ImGuiIO& io = ImGui::GetIO();
            if (!io.WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
                for (ImWchar c : io.InputQueueCharacters)
                    if (c == '/')
                        _OpenSearch(state);
            // Escape anywhere in the document closes the bar; an active widget that uses Escape (the query's field, a
            // text field of the application) has the priority
            if (state.searchOpen && ImGui::Shortcut(ImGuiKey_Escape))
                _CloseSearch(frame, state);
            if (state.searchOpen)
                _DrawFindBar(frame, state, view, contentMin, contentMax);
            else
                state.findBarBottom = 0.f;
        }

        if (context->renderer)
            context->renderer->document = nullptr;
        context->document.reset();
        ImGui::EndChild();  // the document
    }
    // ::endcode

    void RenderDocument(const char* id, const std::string& markdown, ImVec2 size, const DocumentOptions& options)
    {
        BeginDocument(id, size, options);
        Render(markdown);
        EndDocument();
    }

    // The text as markdown that shows it as it is: the ASCII punctuation escaped
    static std::string _EscapeMarkdown(const std::string& text)
    {
        std::string escaped;
        for (char c : text) {
            if ((unsigned char)c < 0x80 && std::ispunct((unsigned char)c))
                escaped += '\\';
            escaped += c;
        }
        return escaped;
    }

    // The section shows unless a fold hides it: its heading's own, or one above it, which goes on across the renders
    bool DocumentHeading(int level, const std::string& text, bool drawTitle)
    {
        level = ImClamp(level, 1, 6);
        Context* context = gCurrentContext;
        if (drawTitle) {  // a markdown heading: drawn as one, and recorded by its render
            RenderRaw(std::string((size_t)level, '#') + " " + _EscapeMarkdown(text));
            return !(context && context->document && context->document->renders.foldLevel > 0);
        }
        MarkdownRenderer* renderer = _Renderer();
        if (!context || !context->document || !renderer)
            return true;  // outside a document, there is nothing to record
        _SetFoldOptions(context, renderer);
        return renderer->document_heading(level, text);
    }

    // A text file: from the assets, else from the file system as is (a source file rendering itself)
    static std::optional<std::string> _ReadTextAssetOrFile(const std::string& path)
    {
        AssetBytes bytes = gHostServices.ReadAsset ? gHostServices.ReadAsset(path) : ReadAssetDefault(path);
        if (!bytes)
        {
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs)
                return std::nullopt;
            return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        }
        return std::string(bytes->begin(), bytes->end());
    }

    static std::filesystem::file_time_type _ModificationTime(const std::string& path)
    {
        std::error_code ec;
        auto time = std::filesystem::last_write_time(path, ec);
        return ec ? std::filesystem::file_time_type::min() : time;
    }

    // Whether a file asked for by a resolved text changed on disk (checked at most twice a second)
    static bool _FilesChanged(ResolvedText& resolved)
    {
        double now = ImGui::GetTime();
        if (now - resolved.lastCheck < 0.5)
            return false;
        resolved.lastCheck = now;
        for (const auto& [path, time] : resolved.files)
            if (_ModificationTime(path) != time)
                return true;
        return false;
    }

    // A text with its transclusions resolved, cached per text. It is resolved again when one of its files changes:
    // a running program shows the new version of its own narrative.
    static const std::string& _ResolveTransclusionsCached(const std::string& text)
    {
        if (text.find("![[") == std::string::npos)
            return text;
        auto& cache = gCurrentContext->resolvedTransclusions;
        auto it = cache.find(text);
        if (it != cache.end() && !_FilesChanged(it->second))
            return it->second.markdown;
        ResolvedText& resolved = cache[text];
        resolved.files.clear();
        auto readAndRecord = [&resolved](const std::string& path) {
            resolved.files.push_back({path, _ModificationTime(path)});
            return _ReadTextAssetOrFile(path);
        };
        resolved.markdown = ResolveTransclusions(text, readAndRecord);
        resolved.lastCheck = ImGui::GetTime();
        return resolved.markdown;
    }

    void Render(const std::string& markdownString)
    {
        RenderRaw(_ResolveTransclusionsCached(_Unindent(markdownString, false)));
    }

    void RenderFile(const std::string& path, const std::string& target)
    {
        RenderRaw(_ResolveTransclusionsCached("![[" + path + (target.empty() ? "" : "#" + target) + "]]"));
    }

    void PushSelectableText(bool selectable)
    {
        IM_ASSERT(gCurrentContext && "RichMd: call InitializeMarkdown first");
        _CheckSelectableTextStack(gCurrentContext);
        gCurrentContext->selectableTextStack.push_back(selectable);
        gCurrentContext->selectableTextFrame = ImGui::GetFrameCount();
    }

    void PopSelectableText()
    {
        IM_ASSERT(gCurrentContext && !gCurrentContext->selectableTextStack.empty()
                  && "RichMd: PopSelectableText() without PushSelectableText()");
        gCurrentContext->selectableTextStack.pop_back();
    }

    void SetSelectableTextDefault(bool selectable)
    {
        IM_ASSERT(gCurrentContext && "RichMd: call InitializeMarkdown first");
        gCurrentContext->options.selectableText = selectable;
    }

    void RegisterFencedBlockRenderer(const std::string& language, std::function<void(const std::string& code)> renderer)
    {
        IM_ASSERT(gCurrentContext && "RichMd: call InitializeMarkdown first");
        gCurrentContext->fencedBlockRenderers[_ToLower(language)] = std::move(renderer);
    }

    void RenderMermaid(const std::string& source)
    {
#ifdef IMGUI_RICHMD_WITH_MERMAID
        static Mermaid::CachePtr cacheWithoutContext;  // a diagram drawn outside of any context (no markdown fonts)
        Mermaid::CachePtr& cache = gCurrentContext ? gCurrentContext->mermaidCache : cacheWithoutContext;
        if (!cache)
            cache = Mermaid::CreateCache();
        Mermaid::RenderResult result = Mermaid::Render(source, *cache);
        if (result.drawn)
            return;
#endif
        IM_ASSERT(_Renderer() && "RichMd: call InitializeMarkdown first");
        ImGui::PushID(source.c_str());
        _RenderCodeBlock(source, "mermaid");
        ImGui::PopID();
#ifdef IMGUI_RICHMD_WITH_MERMAID
        ImVec4 errorColor = GetStyle().errorColor;
        if (errorColor.w < 0.f)  // automatic
            errorColor = _Renderer()->admonition_color(Renderer::AdmonitionKind::Caution);
        ImGui::PushStyleColor(ImGuiCol_Text, errorColor);
        ImGui::TextWrapped("mermaid, %s", result.error.c_str());
        ImGui::PopStyleColor();
#endif
    }

    Style& GetStyle()
    {
        IM_ASSERT(_Renderer() && "RichMd: call InitializeMarkdown first");
        return _Renderer()->style;
    }

    std::function<void(void)> GetFontLoaderFunction()
    {
        return []() { _Renderer(); };
    }


    void OnOpenLink_Default(const std::string& url)
    {
        if (strncmp(url.c_str(), "http", strlen("http")) != 0)
        {
            std::cerr << "RichMd::OnOpenLink_Default url \"" << url << "\" should start with http!\n";
            return;
        }
        _OpenUrlInBrowser(url.c_str());
    }


    static bool _IsUrl(const std::string& path)
    {
        return path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0;
    }

    static std::optional<MarkdownImage> _MakeMarkdownImage(const MarkdownTexture& tex)
    {
        MarkdownImage r;
        r.texture_id = tex.ref.GetTexID();  // 0 until the backend creates a registered texture (see get_image)
        r.size = tex.size;
        r.uv0 = { 0,0 };
        r.uv1 = {1,1};
        r.col_tint = { 1,1,1,1 };
        r.col_border = { 0,0,0,0 };
        return r;
    }

    // Decodes an encoded image (png, jpg, ...) and uploads it through the host
    static MarkdownTexture _UploadEncodedImage(const uint8_t* data, size_t size)
    {
        if (!gHostServices.UploadRgba)
            return {};
        int w = 0, h = 0, channels = 0;
        unsigned char* rgba = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
        if (rgba == nullptr)
            return {};
        MarkdownTexture tex = gHostServices.UploadRgba(rgba, w, h);
        stbi_image_free(rgba);
        return tex;
    }

    static MarkdownTexture _LoadTextureFromAsset(const std::string& assetPath)
    {
        auto bytes = gHostServices.ReadAsset(assetPath);
        if (!bytes)
            return {};
        return _UploadEncodedImage(bytes->data(), bytes->size());
    }

    static MarkdownTexture _LoadTextureFromEncodedData(const std::vector<uint8_t>& data)
    {
        return _UploadEncodedImage(data.data(), data.size());
    }

    // The broken-image texture is loaded once and kept in the image cache
    // (the cache owns the textures: a texture returned as a temporary would be
    // freed at the end of the frame, leaving a dangling id).
    static const MarkdownTexture& _BrokenImageTexture()
    {
        auto& imageCache = _Renderer()->ImageCache();
        std::string errorImage = "images/markdown_broken_image.png";
        auto it = imageCache.find(errorImage);
        if (it == imageCache.end())
            it = imageCache.emplace(errorImage, _LoadTextureFromAsset(errorImage)).first;
        return it->second;
    }

    // Cache image_path as broken (no retry on the next frames) and return the broken-image
    static std::optional<MarkdownImage> _BrokenImage(const std::string& image_path)
    {
        auto& imageCache = _Renderer()->ImageCache();
        imageCache[image_path] = _BrokenImageTexture();
        const auto& tex = imageCache.at(image_path);
        if (tex.Valid())
            return _MakeMarkdownImage(tex);
        return std::nullopt;
    }

    std::optional<MarkdownImage> OnImage_Default(const std::string& image_path)
    {
        MarkdownRenderer* renderer = _Renderer();
        if (!renderer)
        {
            std::cerr << "Did you initialize RichMd?\n";
            return std::nullopt;
        }

        auto & imageCache = renderer->ImageCache();

        // If already cached, return it
        if (imageCache.find(image_path) != imageCache.end())
            return _MakeMarkdownImage(imageCache.at(image_path));

        // URL images go through the host's download service
        if (_IsUrl(image_path) && gHostServices.Download)
        {
            auto result = gHostServices.Download(image_path);
            switch (result.status)
            {
            case MarkdownDownloadStatus::Ready:
                imageCache[image_path] = _LoadTextureFromEncodedData(result.data);
                return _MakeMarkdownImage(imageCache.at(image_path));

            case MarkdownDownloadStatus::Downloading:
                // The renderer draws a spinner (not cached: called again next frame)
                gImageIsLoading = true;
                return std::nullopt;

            case MarkdownDownloadStatus::Failed:
                if (!result.errorMessage.empty())
                    std::cerr << "rich_md: download failed for " << image_path << ": " << result.errorMessage << "\n";
                return _BrokenImage(image_path);

            case MarkdownDownloadStatus::NotStarted:
            default:
                return _BrokenImage(image_path);
            }
        }

        // Handle local asset images
        MarkdownTexture tex = _LoadTextureFromAsset(image_path);
        if (!tex.Valid())
            return _BrokenImage(image_path);
        imageCache[image_path] = tex;
        return _MakeMarkdownImage(imageCache.at(image_path));
    }

    ImVec4 LinkColor()
    {
        MarkdownRenderer* renderer = _Renderer();
        return renderer ? renderer->link_color() : Renderer::default_link_color();
    }

    // Same look and behaviour as the links inside markdown
    void RenderTextAsLink(const char* text, const char* url)
    {
        static const Style defaultStyle;
        MarkdownRenderer* renderer = _Renderer();
        const Style& style = renderer ? renderer->style : defaultStyle;
        ImGui::PushStyleColor(ImGuiCol_Text, LinkColor());
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        if (Renderer::link_item(style, url))
            _OpenUrlInBrowser(url);
    }

    bool HasLatex() { return (bool)gHostServices.RenderLatex; }
    bool HasUrlImages() { return gCurrentContext && (bool)gHostServices.Download; }
    bool HasCodeEditor() { return (bool)gHostServices.RenderCodeBlock; }

    SizedFont GetCodeFont()
    {
        IM_ASSERT(_Renderer() && "RichMd: call InitializeMarkdown first");
        return _Renderer()->get_font_code();
    }

    SizedFont GetFont(const MarkdownFontSpec& fontSpec)
    {
        IM_ASSERT(_Renderer() && "RichMd: call InitializeMarkdown first");
        return _Renderer()->GetFont(fontSpec);
    }


    // Renders a markdown string (after having unindented its main indentation)
    void RenderUnindented(const std::string& markdownString)
    {
        Render(markdownString);
    }

} // namespace RichMdBrowser
