// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
#include "snippets.h"
#include "ImGuiColorTextEdit/TextEditor.h"
#include "imgui.h"
#include "../../rich_md.h"
#include "../../internal/rich_md_internal.h"

#include <algorithm>
#include <cstdio>
#include <map>


namespace Snippets
{
    void _SetTheme(TextEditor& editor, SnippetTheme palette)
    {
        if (palette == SnippetTheme::Auto)
        {
            auto& bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
            float luminance = 0.299f * bg.x + 0.587f * bg.y + 0.114f * bg.z;
            if (luminance > 0.5f)
                editor.SetPalette(TextEditor::GetLightPalette());
            else
                editor.SetPalette(TextEditor::GetDarkPalette());
        }
        else if (palette == SnippetTheme::Dark)
            editor.SetPalette(TextEditor::GetDarkPalette());
        else if (palette == SnippetTheme::Light)
            editor.SetPalette(TextEditor::GetLightPalette());
    }

    void _SetLanguage(TextEditor& editor, SnippetLanguage lang)
    {
        if (lang == SnippetLanguage::Cpp)
            editor.SetLanguage(TextEditor::Language::Cpp());
        else if (lang == SnippetLanguage::Hlsl)
            editor.SetLanguage(TextEditor::Language::Hlsl());
        else if (lang == SnippetLanguage::Glsl)
            editor.SetLanguage(TextEditor::Language::Glsl());
        else if (lang == SnippetLanguage::C)
            editor.SetLanguage(TextEditor::Language::C());
        else if (lang == SnippetLanguage::Sql)
            editor.SetLanguage(TextEditor::Language::Sql());
        else if (lang == SnippetLanguage::AngelScript)
            editor.SetLanguage(TextEditor::Language::AngelScript());
        else if (lang == SnippetLanguage::Lua)
            editor.SetLanguage(TextEditor::Language::Lua());
        else if (lang == SnippetLanguage::Python)
            editor.SetLanguage(TextEditor::Language::Python());
        else if (lang == SnippetLanguage::Markdown)
            editor.SetLanguage(TextEditor::Language::Markdown());
        else if (lang == SnippetLanguage::PlainText)
            editor.SetLanguage(nullptr);
    }

#if defined(__EMSCRIPTEN__) && defined(IMGUI_RICHMD_EMSCRIPTEN_SDL2)
    void _ProcessClipboard_Emscripten(TextEditor& editor)
    {
      if (!ImGui::IsItemHovered())
          return;

      ImGuiIO& io = ImGui::GetIO();
      auto shift = io.KeyShift;
      //auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
      // auto alt = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt;

      auto ctrl = io.KeySuper || io.KeyCtrl;

      bool shallFillBrowserClipboard = false;
      if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_Insert))
          shallFillBrowserClipboard = true;
      else if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_C))
          shallFillBrowserClipboard = true;
      else if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_X))
          shallFillBrowserClipboard = true;
      else if (!ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_Delete))
          shallFillBrowserClipboard = true;

      // ImmApp routes the clipboard to the browser (see js_clipboard_tricks in immapp)
      if (shallFillBrowserClipboard)
          ImGui::SetClipboardText(editor.GetCursorText(0).c_str());
    }
#endif // #if defined(__EMSCRIPTEN__) && defined(IMGUI_RICHMD_EMSCRIPTEN_SDL2)

    // The copy button: two overlapping sheets drawn with the draw list (no icon font needed). Only
    // the visible part of the back sheet is drawn, so the icon does not depend on the button's colors.
    static bool CopyButton(float lineHeight)
    {
        bool clicked = ImGui::Button("##copy", ImVec2(lineHeight * 1.25f, 0.f));  // the frame height, as a glyph button
        ImVec2 mi = ImGui::GetItemRectMin(), ma = ImGui::GetItemRectMax();
        float h = (ma.y - mi.y) * 0.5f;              // sheet height; width is 0.8 h
        float cx = (mi.x + ma.x) * 0.5f, cy = (mi.y + ma.y) * 0.5f;
        float offset = h * 0.25f, thickness = h * 0.11f;
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 fMin(cx - h * 0.4f - offset * 0.5f, cy - h * 0.5f + offset * 0.5f);   // front sheet, bottom left
        ImVec2 fMax(cx + h * 0.4f - offset * 0.5f, cy + h * 0.5f + offset * 0.5f);
        ImVec2 bMin(fMin.x + offset, fMin.y - offset);                                // back sheet, top right
        ImVec2 bMax(fMax.x + offset, fMax.y - offset);
        // back sheet: from the front sheet's top edge, up and around to the front sheet's right edge
        dl->PathLineTo(ImVec2(bMin.x, fMin.y));
        dl->PathLineTo(bMin);
        dl->PathLineTo(ImVec2(bMax.x, bMin.y));
        dl->PathLineTo(bMax);
        dl->PathLineTo(ImVec2(fMax.x, bMax.y));
        dl->PathStroke(col, thickness);
        dl->AddRect(fMin, fMax, col, 0.f, thickness);
        return clicked;
    }

    // The wrap button: a line of text returning under itself (drawn with the draw list, as the copy button)
    static bool WrapButton(float lineHeight, bool active)
    {
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        bool clicked = ImGui::Button("##wrap", ImVec2(lineHeight * 1.25f, 0.f));
        if (active)
            ImGui::PopStyleColor();
        ImVec2 mi = ImGui::GetItemRectMin(), ma = ImGui::GetItemRectMax();
        float h = (ma.y - mi.y) * 0.5f;
        float cx = (mi.x + ma.x) * 0.5f, cy = (mi.y + ma.y) * 0.5f;
        float left = cx - h * 0.5f, right = cx + h * 0.5f, thickness = h * 0.11f;
        float y1 = cy - h * 0.3f, y2 = cy + h * 0.3f, arrowEnd = cx - h * 0.1f, a = h * 0.22f;
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(left, y1), ImVec2(right, y1), col, thickness);
        dl->PathLineTo(ImVec2(right, y1));
        dl->PathLineTo(ImVec2(right, y2));
        dl->PathLineTo(ImVec2(arrowEnd, y2));
        dl->PathStroke(col, thickness);
        dl->AddTriangleFilled(ImVec2(arrowEnd - a, y2), ImVec2(arrowEnd, y2 - a), ImVec2(arrowEnd, y2 + a), col);
        return clicked;
    }

    static std::string AddFinalEmptyLineIfMissing(const std::string &s)
    {
        if (s.empty())
            return s;
        bool hasEmptyLine = (s.back() == '\n');
        if (hasEmptyLine)
            return s;
        else
        {
            printf("Adding final empty line last=%c\n", s.back());
            return s + "\n";
        }
    }

    // An editor per snippet id. The id of a markdown code block is its code: a block whose code changes (a narrative
    // edited while its program runs) gets a new editor, so the editors not shown for a while are dropped.
    struct SnippetEditor
    {
        TextEditor editor;
        std::string submittedCode;          // the code last given to the editor (SetText resets it: only when it changed)
        bool changed = false;               // set by the editor's change callback
        double timeClickCopyButton = -1e9;  // ImGui::GetTime()
        double lastShown = 0.0;             // ImGui::GetTime()
        size_t longestLine = 0;             // in characters, of the submitted code
        std::string contextLine;            // the line and column right-clicked (for the host's menu)
        size_t contextColumn = 0;
        std::vector<RichMd::CodeBlockMatch> shownMatches;  // the matches of a search, shown as background squiggles
        bool codeReachesButtons = false;    // at the last frame: the code reached under its buttons (they went above)
    };
    static std::map<ImGuiID, SnippetEditor> gSnippetEditors;
    // The reader's choice for the long lines, shared by all the snippets: an editor out of view is dropped, so a
    // per-snippet state would be forgotten; and wrap on in one block means wrap on in the next
    static bool gWordWrap = false;
    // ShowSideBySideSnippets: one of the snippets has its buttons above its code, so all keep a row there (with their
    // buttons, or empty), and their editors stay aligned
    static bool gButtonsRowInGroup = false;

    static size_t LongestLine(const std::string& code)
    {
        size_t longest = 0, current = 0;
        for (char c : code)
        {
            if (c == '\n')
            {
                longest = std::max(longest, current);
                current = 0;
            }
            else if (((unsigned char)c & 0xC0) != 0x80)  // one per code point
                ++current;
        }
        return std::max(longest, current);
    }

    // Drops the editors not shown for 10 seconds (at most once per frame)
    static void _DropUnusedEditors()
    {
        static int lastFrame = -1;
        if (ImGui::GetFrameCount() == lastFrame)
            return;
        lastFrame = ImGui::GetFrameCount();
        double now = ImGui::GetTime();
        for (auto it = gSnippetEditors.begin(); it != gSnippetEditors.end();)
        {
            if (now - it->second.lastShown > 10.0)
                it = gSnippetEditors.erase(it);
            else
                ++it;
        }
    }

    // The editor's position of a byte of a snippet's code: the editor shows the code unindented (its first blank lines
    // and its common indentation removed, see RichMd::Internal::Unindent), and counts the glyphs of a line, not its
    // bytes
    static TextEditor::DocPos _DocPos(const std::string& code, size_t offset, bool deIndented)
    {
        size_t line = 0, lineStart = 0;
        for (size_t i = 0; i < offset && i < code.size(); ++i)
            if (code[i] == '\n')
            {
                ++line;
                lineStart = i + 1;
            }
        size_t from = lineStart;
        if (deIndented)
        {
            size_t firstLine = 0, start = 0, indent = 0;  // the first line that is not blank, and its indentation
            while (true)
            {
                size_t end = code.find('\n', start);
                std::string lineText = code.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (lineText.find_first_not_of(" \t\r") != std::string::npos)
                {
                    indent = lineText.find_first_not_of(' ');
                    break;
                }
                if (end == std::string::npos)
                    break;
                start = end + 1;
                ++firstLine;
            }
            line = line >= firstLine ? line - firstLine : 0;
            if (code.compare(lineStart, indent, std::string(indent, ' ')) == 0)
                from = lineStart + indent;
        }
        size_t glyphs = 0;
        for (size_t i = from; i < offset && i < code.size(); ++i)
            if (((unsigned char)code[i] & 0xC0) != 0x80)
                ++glyphs;
        return TextEditor::DocPos(line, glyphs);
    }

    static bool _SameMatches(const std::vector<RichMd::CodeBlockMatch>& a, const std::vector<RichMd::CodeBlockMatch>& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const RichMd::CodeBlockMatch &x = a[i], &y = b[i];
            if (x.begin != y.begin || x.end != y.end || x.color != y.color || x.current != y.current)
                return false;
        }
        return true;
    }

    // True when the code's rows under the buttons (from the editor's top to `bottom`) reach the buttons' left edge,
    // `left` from the editor's left (the line numbers' margin estimated to 7 glyphs, as for the long lines). Wrapped,
    // from the last render: a line on several rows fills its first ones.
    static bool _CodeReachesUnder(const TextEditor& editor, float left, float bottom, float rowHeight, float glyphWidth)
    {
        size_t row = 0;
        for (size_t line = 0; line < editor.GetLineCount() && (float)row * rowHeight < bottom; ++line)
        {
            TextEditor::VisPos end = editor.DocPos2VisPos(TextEditor::DocPos(line, editor.GetLineText(line).size()));
            if (end.row > row || (float)(end.column + 7) * glyphWidth > left)
                return true;
            row = end.row + 1;
        }
        return false;
    }

    // The search of a document in a snippet (ShowCodeSnippetWithMatches)
    struct SnippetSearch
    {
        const std::vector<RichMd::CodeBlockMatch>& matches;
        bool inDocument;
    };
    struct ShownSnippet
    {
        bool changed = false;
        std::vector<ImRect> matchRects;  // on the screen
    };

    static ShownSnippet _ShowSnippet(const std::string& label_id, SnippetData* snippetDataPtr, float width,
                                     int overrideHeightInLines, const SnippetSearch* search)
    {
        SnippetData& snippetData = *snippetDataPtr;

        if (width == 0.f)
            width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x);

        auto id = ImGui::GetID(label_id.c_str());
        ImGui::PushID(label_id.c_str());
        _DropUnusedEditors();
        auto [it, inserted] = gSnippetEditors.try_emplace(id);
        SnippetEditor& state = it->second;
        if (inserted)
        {
            _SetLanguage(state.editor, snippetData.Language);
            state.editor.SetChangeCallback([&state]() { state.changed = true; });
        }
        state.lastShown = ImGui::GetTime();

        auto& editor = state.editor;
        editor.SetReadOnlyEnabled(snippetData.ReadOnly);
        editor.SetCaretsVisible(!snippetData.ReadOnly);
        editor.SetShowWhitespacesEnabled(false);
        _SetTheme(editor, snippetData.Palette);
        if (editor.GetText().empty() || snippetData.ReadOnly)
        {
            std::string displayedCode = snippetData.DeIndentCode ? RichMd::Internal::Unindent(snippetData.Code, true) : snippetData.Code;
            if (snippetData.AddFinalEmptyLine)
                displayedCode = AddFinalEmptyLineIfMissing(displayedCode);

            // SetText resets the editor (its scroll, its selection): only when the code changed. Not compared with
            // GetText(), which the editor normalizes (a final newline): the code would be set again every frame.
            std::string& submitted = state.submittedCode;
            if (submitted != displayedCode)
            {
                editor.SetText(displayedCode);  // which clears the squiggles
                submitted = displayedCode;
                state.longestLine = LongestLine(displayedCode);
                state.shownMatches.clear();
            }
        }

        // A document's search: its matches behind the code (set again when they change), the current one scrolled to
        // in a long snippet. In a document, the editor's own find is off: Ctrl+F reaches the document's.
        if (search != nullptr)
        {
            editor.SetFindReplaceEnabled(!search->inDocument);
            if (!_SameMatches(search->matches, state.shownMatches))
            {
                editor.ClearSquiggles();
                for (const RichMd::CodeBlockMatch& match : search->matches)
                {
                    TextEditor::DocPos begin = _DocPos(snippetData.Code, match.begin, snippetData.DeIndentCode);
                    if (match.color != 0)
                        editor.AddSquiggle(begin, _DocPos(snippetData.Code, match.end, snippetData.DeIndentCode), 0,
                                           match.color, "", TextEditor::SquiggleStyle::background);
                    bool wasCurrent = false;
                    for (const RichMd::CodeBlockMatch& shown : state.shownMatches)
                        wasCurrent = wasCurrent || (shown.current && shown.begin == match.begin);
                    if (match.current && !wasCurrent)
                        editor.ScrollToLine(begin.line, TextEditor::Scroll::alignMiddle);
                }
                state.shownMatches = search->matches;
            }
        }

        ImGui::BeginGroup();

        // Title Line
        bool hasTitleLine = ! snippetData.DisplayedFilename.empty() || snippetData.ShowCursorPosition;

        float lineHeight;
        float editorLineHeight;  // the editor's line pitch: it renders with ItemSpacing (0, 0)
        float glyphWidth;
        float buttonHeight;      // the frame height, with the code font (the buttons are drawn with it)
        {
            auto codeFont = RichMd::GetCodeFont();
            ImGui::PushFont(codeFont.font, codeFont.size);
            lineHeight = ImGui::GetTextLineHeightWithSpacing();
            buttonHeight = ImGui::GetFrameHeight();
            editorLineHeight = ImGui::GetTextLineHeight() * editor.GetLineSpacing();
            glyphWidth = ImGui::CalcTextSize("M").x;
            ImGui::PopFont();
        }

        // A line wider than the editor's text area (the width less the line numbers' margin): the wrap button shows,
        // and the reader's choice applies
        bool hasLongLines = (float)(state.longestLine + 7) * glyphWidth > width;
        bool wrapped = hasLongLines && gWordWrap;
        editor.SetWordWrapEnabled(wrapped);

        ImVec2 editorSize;
        {
            editorSize.x = width;

            int nbVisibleLines = 0;
            if ((snippetData.HeightInLines == 0) && (overrideHeightInLines==0))
            {
                nbVisibleLines = (int)std::count(snippetData.Code.begin(), snippetData.Code.end(), '\n') + 1;
                if (wrapped && editor.GetLineCount() > 0)  // the visual rows, as laid out at the last render
                {
                    size_t lastLine = editor.GetLineCount() - 1;
                    auto end = editor.DocPos2VisPos(TextEditor::DocPos(lastLine, editor.GetLineText(lastLine).size()));
                    nbVisibleLines = std::max(nbVisibleLines, (int)end.row + 1);
                }
            }
            else if (overrideHeightInLines != 0)
                nbVisibleLines = overrideHeightInLines;
            else
                nbVisibleLines = snippetData.HeightInLines;

            if ((snippetData.MaxHeightInLines > 0) && (nbVisibleLines > snippetData.MaxHeightInLines))
                nbVisibleLines = snippetData.MaxHeightInLines;

            // + ImGui::GetStyle().ScrollbarSize: account for a possible horizontal scrollbar
            editorSize.y = editorLineHeight * (float)nbVisibleLines + ImGui::GetStyle().ScrollbarSize;
        }

        // The buttons: the copy button, and the wrap button when a line overflows. They float over the editor's top
        // right corner (left of its vertical scrollbar), or take a row above it when the code reaches under them.
        const ImGuiStyle& style = ImGui::GetStyle();
        bool showButtons = snippetData.ShowCopyButton || hasLongLines;
        int nbButtons = (snippetData.ShowCopyButton ? 1 : 0) + (hasLongLines ? 1 : 0);
        float buttonWidth = lineHeight * 1.25f, pad = style.FramePadding.y;
        float buttonsWidth = buttonWidth * (float)nbButtons + style.ItemSpacing.x * (float)(nbButtons - 1);
        float buttonsRightMargin = pad;
        {
            // A vertical scrollbar when the lines exceed the height, less the room of a horizontal scrollbar when the
            // long lines are not wrapped
            float usableHeight = editorSize.y - style.ScrollbarSize;
            if (hasLongLines && !wrapped)
                usableHeight -= style.ScrollbarSize;
            int rows = editor.GetLineCount() > 0 ? (int)editor.GetLineCount() : 1;
            if (wrapped)
            {
                size_t lastLine = editor.GetLineCount() - 1;
                TextEditor::DocPos end(lastLine, editor.GetLineText(lastLine).size());
                rows = (int)editor.DocPos2VisPos(end).row + 1;
            }
            if ((float)rows * editorLineHeight > usableHeight)
                buttonsRightMargin += style.ScrollbarSize;
        }
        float buttonsLeft = width - buttonsRightMargin - buttonsWidth;  // from the editor's left
        state.codeReachesButtons = showButtons
            && _CodeReachesUnder(editor, buttonsLeft, pad + buttonHeight, editorLineHeight, glyphWidth);
        bool buttonsAbove = state.codeReachesButtons || (showButtons && gButtonsRowInGroup);
        auto drawButtons = [&]()
        {
            if (hasLongLines)
            {
                if (WrapButton(lineHeight, wrapped))
                    gWordWrap = !gWordWrap;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(wrapped ? "Unwrap the long lines" : "Wrap the long lines");
                if (snippetData.ShowCopyButton)
                    ImGui::SameLine();
            }
            if (snippetData.ShowCopyButton)
            {
                if (CopyButton(lineHeight))
                {
                    state.timeClickCopyButton = ImGui::GetTime();
                    ImGui::SetClipboardText(snippetData.Code.c_str());
                }
                bool wasCopiedRecently = (ImGui::GetTime() - state.timeClickCopyButton) < 0.7;
                if (wasCopiedRecently)
                    ImGui::SetTooltip("Copied!");
                else if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Copy");
            }
        };

        if (hasTitleLine)
        {
            ImVec2 topLeft = ImGui::GetCursorPos();
            ImVec2 topRight { topLeft.x + editorSize.x, topLeft.y};
            float textY = topRight.y + (float)lineHeight * 0.2f;

            if (! snippetData.DisplayedFilename.empty())
            {
                ImGui::SetCursorPos({topLeft.x, textY});
                ImGui::Text("%s", snippetData.DisplayedFilename.c_str());
            }

            if (snippetData.ShowCursorPosition)
            {
                // right aligned (the copy button sits inside the editor, not on this line)
                auto pos = editor.GetMainCursorPosition();
                char positionText[64];
                snprintf(positionText, sizeof(positionText), "L:%02zu C:%02zu", pos.line + 1, pos.index + 1);
                float textX = topRight.x - ImGui::CalcTextSize(positionText).x - ImGui::GetStyle().ItemSpacing.x;
                ImGui::SetCursorPos({textX, textY});
                ImGui::TextUnformatted(positionText);
            }
            ImGui::SetCursorPos(topRight);
            ImGui::NewLine();
        }

        auto codeFont = RichMd::GetCodeFont();
        ImGui::PushFont(codeFont.font, codeFont.size);

        if (buttonsAbove)  // right aligned with the editor
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + width - buttonsWidth);
            drawButtons();
        }
        else if (gButtonsRowInGroup)
            ImGui::Dummy(ImVec2(0.f, buttonHeight));

        // A read-only snippet is not a widget to "enter": no keyboard navigation outline when it is focused
        if (snippetData.ReadOnly)
            ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0.f, 0.f, 0.f, 0.f));
        editor.Render(std::to_string(id).c_str(), editorSize, snippetData.Border);
        if (snippetData.ReadOnly)
            ImGui::PopStyleColor();

        // The places of the matches: known when the editor was drawn (out of view, it is not laid out)
        ShownSnippet shown;
        if (search != nullptr && ImGui::IsItemVisible())
            for (const RichMd::CodeBlockMatch& match : search->matches)
            {
                ImVec2 a = editor.DocPos2ScreenPos(_DocPos(snippetData.Code, match.begin, snippetData.DeIndentCode));
                ImVec2 b = editor.DocPos2ScreenPos(_DocPos(snippetData.Code, match.end, snippetData.DeIndentCode));
                float right = (b.y == a.y) ? b.x : a.x + editor.GetGlyphWidth();  // a match cut by the word wrap
                shown.matchRects.emplace_back(a, ImVec2(right, a.y + editor.GetLineHeight()));
            }

        // The host's hooks: the mouse's line and column, read now (the editor is the last item), used after the
        // overlay, with the code font popped
        bool hostHover = false, hostContext = false;
        TextEditor::DocPos mousePos;
        if ((snippetData.OnHover || snippetData.OnContextMenu) && editor.IsMousePosOverGlyph(ImGui::GetMousePos()))
        {
            mousePos = editor.GetDocPosAtMousePos(ImGui::GetMousePos());
            hostHover = snippetData.OnHover && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_Stationary);
            hostContext = snippetData.OnContextMenu && ImGui::IsItemClicked(ImGuiMouseButton_Right);
        }

        if (showButtons && !buttonsAbove)
        {
            // Over the editor, in a small child window of their own, begun after the editor (so it is drawn above it
            // and gets the hover). The parent's cursor is restored afterwards: the overlay must not take room in the
            // layout.
            ImVec2 parentCursor = ImGui::GetCursorPos();
            ImVec2 editorMin = ImGui::GetItemRectMin(), editorMax = ImGui::GetItemRectMax();
            ImGui::SetCursorScreenPos(ImVec2(editorMin.x + buttonsLeft, editorMin.y + pad));
            ImGuiWindowFlags overlayFlags =
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav;
            ImVec2 overlaySize(buttonsWidth, buttonHeight);
            if (ImGui::BeginChild("copy_overlay", overlaySize, ImGuiChildFlags_None, overlayFlags))
                drawButtons();
            ImGui::EndChild();
            ImGui::SetCursorPos(parentCursor);
        }

        shown.changed = state.changed;
        state.changed = false;
        if (shown.changed && !snippetData.ReadOnly)
            snippetData.Code = editor.GetText();

#if defined(__EMSCRIPTEN__) && defined(IMGUI_RICHMD_EMSCRIPTEN_SDL2)
        _ProcessClipboard_Emscripten(editor);
#endif

        ImGui::PopFont();

        if (hostContext)
        {
            state.contextLine = editor.GetLineText(mousePos.line);
            state.contextColumn = mousePos.index;
            ImGui::OpenPopup("snippet_context");
        }
        if (hostHover && !ImGui::IsPopupOpen("snippet_context"))
            snippetData.OnHover(editor.GetLineText(mousePos.line), mousePos.index);
        if (snippetData.OnContextMenu && ImGui::BeginPopup("snippet_context"))
        {
            if (!snippetData.OnContextMenu(state.contextLine, state.contextColumn))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::EndGroup();
        ImGui::PopID();

        return shown;
    }

    bool ShowEditableCodeSnippet(const std::string& label_id, SnippetData* snippetData, float width,
                                 int overrideHeightInLines)
    {
        return _ShowSnippet(label_id, snippetData, width, overrideHeightInLines, nullptr).changed;
    }

    void ShowCodeSnippet(const SnippetData& snippetData, float width, int overrideHeightInLines)
    {
        auto code = snippetData.Code;
        auto nonConstSnippedData = const_cast<SnippetData&>(snippetData);  // I know...
        std::string labelId = nonConstSnippedData.Code;
        ShowEditableCodeSnippet(labelId, &nonConstSnippedData, width, overrideHeightInLines);
        nonConstSnippedData.Code = code;
    }
}

namespace RichMd { namespace Internal
{
    std::vector<ImRect> ShowCodeSnippetWithMatches(const Snippets::SnippetData& snippet,
                                                   const std::vector<CodeBlockMatch>& matches, bool inDocument)
    {
        Snippets::SnippetData data = snippet;  // read-only: its code stays
        Snippets::SnippetSearch search{matches, inDocument};
        return Snippets::_ShowSnippet(snippet.Code, &data, 0.f, 0, &search).matchRects;
    }
}}

namespace Snippets
{



    float _EditorWidth(int nbSideBySideEditors)
    {
        float margins_x = (nbSideBySideEditors + 1) * ImGui::GetStyle().ItemSpacing.x;
        float windowContentWidth = ImGui::GetContentRegionAvail().x;
        float editorWidth= (windowContentWidth - margins_x) / nbSideBySideEditors;
        return editorWidth;
    }

    void ShowSideBySideSnippets(const std::vector<SnippetData>& snippets , bool hideIfEmpty, bool equalVisibleLines)
    {
        int nbSideBySideEditors = (int)snippets.size();

        if (hideIfEmpty)
        {
            for (const auto& snippet: snippets)
                if (snippet.Code.empty())
                    nbSideBySideEditors -= 1;
            if (nbSideBySideEditors == 0)
                return;
        }

        int overrideHeightInLines = 0;
        if (equalVisibleLines)
        {
            size_t maxLines = 0;
            for (const auto& s : snippets)
                maxLines = std::max(maxLines, (size_t)std::count(s.Code.begin(), s.Code.end(), '\n'));
            overrideHeightInLines = (int)maxLines + 1;
        }

        float editorWidth = _EditorWidth(nbSideBySideEditors);

        // A snippet whose buttons went above its code at the last frame: all keep that row (the ID is the one of
        // ShowCodeSnippet, whose label is the code)
        gButtonsRowInGroup = false;
        for (const auto& snippet: snippets)
        {
            auto it = gSnippetEditors.find(ImGui::GetID(snippet.Code.c_str()));
            if (it != gSnippetEditors.end() && it->second.codeReachesButtons)
                gButtonsRowInGroup = true;
        }

        for (const auto& snippet: snippets)
        {
            bool show = !hideIfEmpty || !snippet.Code.empty();
            if (show)
            {
                ShowCodeSnippet(snippet, editorWidth, overrideHeightInLines);
                ImGui::SameLine();
            }
        }
        ImGui::NewLine();
        gButtonsRowInGroup = false;
    }


    void ShowSideBySideSnippets(const SnippetData& snippet1, const SnippetData& snippet2, bool hideIfEmpty, bool equalVisibleLines)
    {
        ShowSideBySideSnippets({snippet1, snippet2}, hideIfEmpty, equalVisibleLines);
    }
}
