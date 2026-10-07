// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
#pragma once
// Helpers shared by the wrapper and its backends. Not part of the public API.
#include "../rich_md.h"
#include "rich_md_search.h"
#include "imgui_internal.h"  // ImRect
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

    // An anchor to reach (#slug), clicked in a fragment or in a document: resolved where all the headings are known, at
    // the end of the fragment or of the document. Collapsed sections open one level per frame, and the scroll is
    // corrected over a few frames (ResolveAnchor, in rich_md_renderer.cpp).
    struct PendingAnchor
    {
        std::string slug;       // empty: no anchor
        int frames = 8;         // the frames left to reach it
        ImGuiID fragment = 0;   // the fragment it was clicked in (outside a document)
    };

    // A match of the search, in a render of a document
    struct DocumentMatch
    {
        int fragment = 0;                 // the rank of its render in the document
        size_t begin = 0, end = 0;        // its bytes in the render's text
        float y = 0.f, bottom = 0.f;      // its top and bottom, in the content's coordinates
        std::string before, text, after;  // its context, for the lists of matches
        ImGuiID details = 0;              // the collapsed <details> that hides it (at its header): a jump opens it
        bool inRuns = true;               // drawn as text runs (not in a code block): it can become the selection
    };

    // What the renders of the document being drawn (between BeginDocument() and EndDocument()) share: their headings
    // (their slugs unique in the document), the anchor clicked in one of them in this frame, and the search (set by
    // BeginDocument(): each render adds its matches)
    struct DocumentRenders
    {
        std::vector<Heading> headings;
        std::vector<ImGuiID> headingDetails;  // the collapsed <details> that hides each heading (0: none)
        std::unordered_map<std::string, int> slugOccurrences;
        std::string clickedAnchor;
        float contentStartY = 0.f;  // the top of the content: a fragment below it starts with the gap of a block
        int fragmentCount = 0;      // the renders so far: the rank of the next one

        std::string query;          // empty: no search
        SearchOptions searchOptions;
        bool highlightAll = true;
        int currentFragment = -1;   // the current match, as of the last frame: its render and its first byte
        size_t currentBegin = 0;
        std::vector<DocumentMatch> matches;
        int selectFragment = -1;    // the find bar closed on a match of the render of this rank: it gets selected
        size_t selectBegin = 0, selectEnd = 0;
    };

    // A document being drawn: its headings, and its layout in this frame (decided by BeginDocument(), drawn by
    // EndDocument())
    struct DocumentFrame
    {
        ImGuiID id = 0;
        DocumentOptions options;
        DocumentRenders renders;
        ImVec2 origin;            // the top left of the document, in its window's local coordinates
        ImVec2 size;              // the size of the document
        bool panel = false;       // the table of contents beside the content
        bool line = false;        // the line above the content (the table of contents hidden, or the document narrow)
        bool narrow = false;      // narrower than options.narrowWidth: the line, which cannot show the panel
        float panelWidth = 0.f;   // pixels
        float splitterWidth = 0.f;
        float scrollY = 0.f;      // the content's scroll
    };

    // A scroll a document is making, to a heading (an anchor, a click in the table of contents) or to a match: measured
    // again at each frame, eased over the animation's duration, then corrected until it holds
    struct DocumentScroll
    {
        PendingAnchor anchor;          // a heading, when its slug is not empty
        int matchFragment = -1;        // else a match (its render and its first byte), when matchFragment >= 0
        size_t matchBegin = 0;
        int frames = 0;                // the frames left for the correction
        float from = 0.f;              // the animation: the scroll at its start, and its start time (-1: none yet)
        double start = -1.0;
        bool animated = false;         // the animation was done (or skipped)
    };

    // The state of a document between frames, by id
    struct DocumentState
    {
        DocumentScroll scroll;    // the scroll it is making
        float panelWidth = 16.f;  // the table of contents' width, in em (the reader drags its edge)
        bool panelShown = true;   // the reader can hide it
        int headingCount = 0;     // at the last frame: the table of contents needs tocMinHeadings
        int currentSection = -1;  // the heading at the top of the view, at the last frame

        // The search: the find bar and its options, the current match (its render and its first byte)
        bool searchOpen = false;
        bool focusQuery = false;  // the query's field takes the focus at the next frame
        bool selectQuery = false;  // Ctrl+F: the field selects the query (typing replaces it)
        bool focusContent = false;  // the find bar closed: the content takes the focus back (Ctrl+F reaches it)
        char query[256] = "";
        SearchOptions searchOptions;
        bool highlightAll = true;
        int currentFragment = -1;
        size_t currentBegin = 0;
        int step = 0;             // the find bar asked for the next (1) or the previous (-1) match
        bool jumpToFirst = false; // the query changed: the first match below the top of the view becomes current
        int matchCount = 0, currentMatch = -1;  // at the last frame, for the find bar
        bool matchMoved = false;                // the current match changed at this frame: the lists show it
        bool dropdownOpen = false;              // the matches with their context, under the find bar
        std::vector<int> tickMatches;           // the matches of the ticks clicked on the scrollbar, in a small window
        ImVec2 tickPopupPos;
        const char* wrapMessage = nullptr;      // the last step went past an end: the bar says it wrapped around
        float findBarHeight = 0.f;              // a narrow document: the room the find bar takes above the content
        int selectFragment = -1;                // the find bar closed on a match: its render selects it
        size_t selectBegin = 0, selectEnd = 0;
        float findBarBottom = 0.f;              // the bottom of the find bar below the content's top (a jump aims below)
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
        // Documents: the one being drawn (between BeginDocument() and EndDocument()), and the state of each, by id
        std::unique_ptr<DocumentFrame> document;
        int documentFrame = -1;    // the frame of the last BeginDocument()
        int documentNesting = 0;   // BeginDocument() inside a document (a user error): only a child window
        std::unordered_map<ImGuiID, DocumentState> documents;
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

namespace RichMd
{
    // A match of the search in a code block: its bytes in the code, its color (0: not drawn)
    struct CodeBlockMatch
    {
        size_t begin = 0, end = 0;
        ImU32 color = 0;
        bool current = false;
    };
}

#ifdef IMGUI_RICHMD_WITH_CODE_EDITOR
namespace Snippets { struct SnippetData; }
namespace RichMd { namespace Internal
{
    // A snippet with the matches of a document's search drawn behind its code (snippets.cpp): a long snippet scrolls to
    // the current one. In a document, the editor's own find is off: Ctrl+F reaches the document's. Returns the matches'
    // rectangles, on the screen.
    std::vector<ImRect> ShowCodeSnippetWithMatches(const Snippets::SnippetData& snippet,
                                                   const std::vector<CodeBlockMatch>& matches, bool inDocument);
}}
#endif
