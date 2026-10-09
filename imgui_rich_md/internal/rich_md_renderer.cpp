// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
// Forked from imgui_md by Dmitry Mekhontsev (https://github.com/mekhontsev/imgui_md, its MIT license below), and
// heavily modified since: LaTeX formulas, admonitions, collapsible sections, wikilinks, task lists, images with a
// loading spinner, HTML tags (<sub>, <sup>, <kbd>, <mark>, <u>, <center>, <md-error>), and a Style that follows the
// ImGui theme. Internal: the public API is in rich_md.h.
/*
 * imgui_md: Markdown for Dear ImGui using MD4C
 * (https://github.com/mekhontsev/imgui_md)
 *
 * Copyright (c) 2021 Dmitry Mekhontsev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "rich_md_renderer.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <iterator>

namespace RichMd
{


// Inline content (text, inline code, images, formulas) ends with SameLine(), so that the next piece continues the
// line: a line is open. ImGui keeps that state (IsSameLine); fenced block renderers and widgets draw items the
// renderer does not see, so it reads it there rather than keeping its own.
static bool line_is_open()
{
	return ImGui::GetCurrentWindow()->DC.IsSameLine;
}

// Ends the open line, if any (adds no empty line)
static void end_line()
{
	if (line_is_open())
		ImGui::NewLine();
}

// Small vertical gap between markdown blocks (a fraction of the font size), below the open line if any. A zero gap
// only ends the line (a Dummy would add ImGui's item spacing)
static void add_block_gap(float gap_em)
{
	end_line();
	if (gap_em > 0.0f)
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * gap_em));
}

// The gap above a top-level block, except above the first one: true when added
bool Renderer::separate_block()
{
	if (m_skip_next_block_gap) {
		m_skip_next_block_gap = false;
		return false;
	}
	add_block_gap(style.blockGap);
	return true;
}

// A color of the style, or its automatic value
static ImVec4 resolve_color(const ImVec4& color, const ImVec4& automatic)
{
	return (color.w < 0.0f) ? automatic : color;
}


// ::md Parsing and drawing
// The renderer is md4c's client. `print()` parses a fragment with md4c, which calls back `block()`, `span()` and
// `text()` as it enters and leaves each block and span; they dispatch to `BLOCK_*()` and `SPAN_*()`, which draw as they
// go, with ImGui widgets and the window's draw list. `MarkdownRenderer` (in rich_md.cpp) derives from it, and provides
// the fonts, the images, the formulas, the code blocks, and the application's callbacks (links, headings, HTML).
// ::code
Renderer::Renderer()
{
	m_md.abi_version = 0;

	m_md.flags = MD_FLAG_TABLES | MD_FLAG_UNDERLINE | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS;

	m_md.enter_block = [](MD_BLOCKTYPE t, void* d, void* u) {
		return ((Renderer*)u)->block(t, d, true);
	};

	m_md.leave_block = [](MD_BLOCKTYPE t, void* d, void* u) {
		return ((Renderer*)u)->block(t, d, false);
	};

	m_md.enter_span = [](MD_SPANTYPE t, void* d, void* u) {
		return ((Renderer*)u)->span(t, d, true);
	};

	m_md.leave_span = [](MD_SPANTYPE t, void* d, void* u) {
		return ((Renderer*)u)->span(t, d, false);
	};

	m_md.text = [](MD_TEXTTYPE t, const MD_CHAR* text, MD_SIZE size, void* u) {
		return ((Renderer*)u)->text(t, text, text + size);
	};

	m_md.debug_log = nullptr;

	m_md.syntax = nullptr;
}
// ::endcode

void Renderer::BLOCK_UL(const MD_BLOCK_UL_DETAIL* d, bool e)
{
	if (e) {
		m_list_stack.push_back(list_info{ 0, d->mark, false, true });
	} else {
		m_list_stack.pop_back();
	}
}

void Renderer::BLOCK_OL(const MD_BLOCK_OL_DETAIL* d, bool e)
{
	if (e) {
		m_list_stack.push_back(list_info{ d->start, d->mark_delimiter, true, true });
	} else {
		m_list_stack.pop_back();
	}
}

void Renderer::BLOCK_LI(const MD_BLOCK_LI_DETAIL* d, bool e)
{
	if (e) {
		// Skip the per-item gap on the first LI of a top-level list:
		// the parent UL/OL's centralized enter-gap already provided
		// one inter-block gap, and emitting another would make
		// P->list transitions visibly larger than list->P. For nested
		// lists the centralized gap was suppressed (we are inside a
		// list), so we still need this gap to break onto a new line.
		bool is_first = m_list_stack.back().first_item_pending;
		m_list_stack.back().first_item_pending = false;
		bool is_top_level = (m_list_stack.size() == 1);
		if (!(is_first && is_top_level))
			add_block_gap(style.listItemGap);

		list_info& nfo = m_list_stack.back();
		if (d && d->is_task) {
			render_task_marker(d->task_mark == 'x' || d->task_mark == 'X');
			ImGui::SameLine();
			if (nfo.is_ol)
				++nfo.cur_ol;
		} else if (nfo.is_ol) {
			ImGui::Text("%d%c", nfo.cur_ol++, nfo.delim);
			ImGui::SameLine();
		} else {
			if (nfo.delim == '*') {
				float cx = ImGui::GetCursorPosX();
				cx -= ImGui::GetStyle().FramePadding.x * 2;
				ImGui::SetCursorPosX(cx);
				ImGui::Bullet();
			} else {
				ImGui::Text("%c", nfo.delim);
				ImGui::SameLine();
			}
		}
		m_list_block_mark = ImGui::GetItemRectMin();  // the marker: the item's first block stays on its line

		ImGui::Indent();
	} else {
		ImGui::Unindent();
	}
}

void Renderer::render_task_marker(bool checked)
{
	// Draw a small bordered square, with a check mark if `checked`.
	// Uses the current font size to scale, stays aligned with the text baseline.
	float h = ImGui::GetFontSize();
	float sz = h * 0.75f;
	ImVec2 cursor = ImGui::GetCursorScreenPos();
	float y_center = cursor.y + h * 0.5f;
	ImVec2 tl(cursor.x, y_center - sz * 0.5f);
	ImVec2 br(cursor.x + sz, y_center + sz * 0.5f);
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImU32 border = ImGui::GetColorU32(ImGuiCol_Text);
	float rounding = sz * 0.15f;
	float thickness = (sz / 12.0f > 1.0f) ? (sz / 12.0f) : 1.0f;
#if IMGUI_VERSION_NUM < 19276
	dl->AddRect(tl, br, border, rounding, 0, thickness);
#else
	dl->AddRect(tl, br, border, rounding, thickness);
#endif
	if (checked) {
		ImU32 mark = ImGui::GetColorU32(ImGuiCol_CheckMark);
		float pad = sz * 0.22f;
		ImVec2 a(tl.x + pad, tl.y + sz * 0.55f);
		ImVec2 b(tl.x + sz * 0.45f, br.y - pad);
		ImVec2 c(br.x - pad, tl.y + pad);
		float check_thick = (sz / 8.0f > 1.5f) ? (sz / 8.0f) : 1.5f;
		dl->AddLine(a, b, mark, check_thick);
		dl->AddLine(b, c, mark, check_thick);
	}
	// Reserve horizontal space; the caller issues SameLine() after.
	ImGui::Dummy(ImVec2(sz, h));
}

void Renderer::BLOCK_HR(bool e)
{
	if (!e) {
		ImGui::Separator();
	}
}

void Renderer::BLOCK_H(const MD_BLOCK_H_DETAIL* d, bool e)
{
	if (e) {
		m_hlevel = d->level;
		m_heading_text.clear();
		m_heading_top = -1.f;
	} else {
		m_hlevel = 0;
	}

	set_font(e);
	if (e)
		m_heading_font_size = ImGui::GetFontSize();

	if (!e) {
		float top = (m_heading_top >= 0.f) ? m_heading_top : ImGui::GetCursorPosY();  // an empty heading: where it ends
		if (d->level <= 2) {
			end_line();  // the underline, just below the title
			ImGui::Separator();
		}
		add_heading((int)d->level, top, false);
		heading((int)d->level, m_heading_text);
		if (m_heading_folds.back() != 0)
			fold_heading((int)d->level, top, m_heading_folds.back());
	}
}

// A foldable heading: its state is kept by its slug, in the fragment's id scope (1: open, the default), and its arrow
// in the margin toggles it. The arrow shows while the mouse is over the heading's line, always when the heading is
// folded, and always on a touch screen (no hover there). A folded heading hides what follows it, until a heading of
// its level or above.
void Renderer::fold_heading(int level, float top, ImGuiID id)
{
	ImGuiStorage* storage = ImGui::GetStateStorage();
	bool open = storage->GetInt(id, 1) != 0;
	if (m_fold_margin > 0.f) {
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		const float y = top + window->Pos.y - window->Scroll.y;
		const ImRect arrow(ImVec2(m_fold_margin_left, y),
		                   ImVec2(m_fold_margin_left + m_fold_margin, y + m_heading_font_size));
		bool hovered = false, held = false;
		if (ImGui::ItemAdd(arrow, id) && ImGui::ButtonBehavior(arrow, id, &hovered, &held)) {
			open = !open;
			storage->SetInt(id, open ? 1 : 0);
		}
		const ImRect line(ImVec2(m_fold_margin_left, y),  // (a heading without a title: the arrow's height)
		                  ImVec2(window->WorkRect.Max.x, ImMax(ImGui::GetCursorScreenPos().y, arrow.Max.y)));
		const bool lineHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(line.Min, line.Max, false);
		const bool touch = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_IsTouchScreen) != 0;
		if (!open || touch || lineHovered || hovered) {
			// RenderArrow draws in a box of the current font's size, scaled; centered a little left of the margin's
			// middle, away from the text
			const float fontSize = ImGui::GetFontSize(), scale = 0.6f * m_heading_font_size / fontSize;
			const ImVec2 center(arrow.Min.x + m_fold_margin * 0.4f, arrow.GetCenter().y);
			const ImVec2 pos(center.x - fontSize * 0.5f, center.y - fontSize * scale * 0.5f);
			const ImU32 color = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
			ImGui::RenderArrow(window->DrawList, pos, color, open ? ImGuiDir_Down : ImGuiDir_Right, scale);
		}
	}
	if (open)
		return;
	m_fold_level = level;
	m_hidden_y = top;
	m_hidden_bottom = ImGui::GetCursorPosY();
	m_hidden_id = id;
}

// The anchor of a heading, as GitHub makes it: lower case, the spaces as hyphens, the ASCII punctuation dropped but
// '-' and '_'. The other characters are kept as they are.
static std::string heading_slug(const std::string& text)
{
	std::string slug;
	for (unsigned char c : text) {
		if (c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
			slug += (char)c;
		else if (c >= 'A' && c <= 'Z')
			slug += (char)(c - 'A' + 'a');
		else if (c == ' ')
			slug += '-';
	}
	return slug;
}

// A slug made unique among those given so far: a repeated one gets -1, -2..., as GitHub numbers them (github-slugger)
std::string UniqueSlug(const std::string& text, std::unordered_map<std::string, int>& occurrences)
{
	const std::string base = heading_slug(text);
	std::string slug = base;
	while (occurrences.count(slug))
		slug = base + "-" + std::to_string(++occurrences[base]);
	occurrences[slug] = 0;
	return slug;
}

// In a document, the slugs are unique in the document, and the document gathers the headings of its fragments
void Renderer::add_heading(int level, float y, bool hidden)
{
	Heading heading;
	heading.level = level;
	heading.text = m_heading_text;
	heading.y = y;
	heading.hidden = hidden;
	heading.slug = UniqueSlug(m_heading_text, document ? document->slugOccurrences : m_slug_occurrences);
	ImGuiID details = hidden ? m_hidden_id : 0;
	// Its fold's state: also for a hidden heading, which unfold all must reach
	const bool foldable = foldableHeadings && level >= foldableHeadingsMinLevel && top_level();
	const ImGuiID fold = foldable ? ImGui::GetID(("##fold-" + heading.slug).c_str()) : 0;
	if (document) {
		document->headings.push_back(heading);
		document->headingDetails.push_back(details);
		document->headingFolds.push_back(fold);
	}
	m_headings.push_back(std::move(heading));
	m_heading_details.push_back(details);
	m_heading_folds.push_back(fold);
}

// In a document, a fold goes on from a render (or a section of widgets) to the next ones: the document keeps it
void Renderer::load_document_fold()
{
	m_fold_level = document ? document->foldLevel : 0;
	if (m_fold_level > 0) {
		m_hidden_y = document->foldY;
		m_hidden_bottom = document->foldBottom;
		m_hidden_id = document->foldId;
	}
}

void Renderer::save_document_fold()
{
	if (!document)
		return;
	document->foldLevel = m_fold_level;
	if (m_fold_level > 0) {
		document->foldY = m_hidden_y;
		document->foldBottom = m_hidden_bottom;
		document->foldId = m_hidden_id;
	}
}

bool Renderer::document_heading(int level, const std::string& text)
{
	load_document_fold();
	if (m_fold_level > 0 && level <= m_fold_level)
		m_fold_level = 0;
	Heading heading;
	heading.level = level;
	heading.text = text;
	heading.slug = UniqueSlug(text, document->slugOccurrences);
	heading.hidden = m_fold_level > 0;
	heading.y = heading.hidden ? m_hidden_y : ImGui::GetCursorPosY();
	const bool foldable = foldableHeadings && level >= foldableHeadingsMinLevel;
	const ImGuiID fold = foldable ? ImGui::GetID(("##fold-" + heading.slug).c_str()) : 0;
	document->headings.push_back(heading);
	document->headingDetails.push_back(heading.hidden ? m_hidden_id : 0);
	document->headingFolds.push_back(fold);
	if (!heading.hidden && fold != 0) {
		m_heading_font_size = ImGui::GetFontSize();
		m_fold_margin = document->foldMargin;
		m_fold_margin_left = document->foldMarginLeft;
		fold_heading(level, heading.y, fold);
	}
	save_document_fold();
	return m_fold_level == 0;
}

void Renderer::fold_all(bool folded)
{
	if (document)
		document->foldAll = folded ? 1 : -1;
	else if (m_fold_storage)
		for (ImGuiID id : m_heading_folds)
			if (id != 0)
				m_fold_storage->SetInt(id, folded ? 0 : 1);
}

// A click on a link: an anchor (#slug) waits for the end of the fragment (or of its document), where its heading is
// known; any other link goes to open_url()
void Renderer::follow_link()
{
	if (m_href.size() > 1 && m_href[0] == '#') {
		if (document)
			document->clickedAnchor = m_href.substr(1);
		else {
			m_anchor = PendingAnchor();
			m_anchor.slug = m_href.substr(1);
			m_anchor.fragment = m_fragment_id;
		}
	} else
		open_url();
}

// The anchor clicked in this fragment. One that no heading of the fragment has goes to open_url(), as any link.
void Renderer::resolve_anchor(ImGuiID fragmentId)
{
	if (m_anchor.slug.empty() || m_anchor.fragment != fragmentId)
		return;
	std::string slug = m_anchor.slug;
	if (ResolveAnchor(m_anchor, m_headings, m_heading_details) == AnchorStatus::NotFound) {
		m_href = "#" + slug;
		open_url();
		m_href.clear();
	}
}

// The scroll is measured again at the frames after it, and corrected until it holds: Dear ImGui truncates the cursor
// toward zero (ItemSize), so the content scrolled above the top of the screen grows by a pixel per item of a fractional
// height (the gaps between blocks, the fonts of the headings).
int FindAnchorHeading(PendingAnchor& anchor, const std::vector<Heading>& headings,
                      const std::vector<ImGuiID>& headingDetails)
{
	size_t i = 0;
	while (i < headings.size() && headings[i].slug != anchor.slug)
		++i;
	if (i == headings.size())
		return -2;
	if (headings[i].hidden && --anchor.frames > 0) {
		ImGui::GetStateStorage()->SetInt(headingDetails[i], 1);  // the section opens at the next frame
		return -1;
	}
	return (int)i;
}

float ScrollToShowAtTop(float y)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	return ImClamp(ImTrunc(y - window->DecoOuterSizeY1 - window->DecoInnerSizeY1), 0.f, window->ScrollMax.y);
}

bool CorrectScroll(float scroll, int& frames)
{
	if (ImFabs(ImGui::GetScrollY() - scroll) <= 1.f || --frames <= 0)
		return true;  // there, or as near as it gets
	ImGui::SetScrollY(scroll);  // applied at the next frame, where the target is measured again
	return false;
}

AnchorStatus ResolveAnchor(PendingAnchor& anchor, const std::vector<Heading>& headings,
                           const std::vector<ImGuiID>& headingDetails)
{
	int i = FindAnchorHeading(anchor, headings, headingDetails);
	if (i == -2) {
		anchor.slug.clear();
		return AnchorStatus::NotFound;
	}
	if (i == -1)
		return AnchorStatus::Waiting;
	if (CorrectScroll(ScrollToShowAtTop(headings[(size_t)i].y), anchor.frames)) {
		anchor.slug.clear();
		return AnchorStatus::Reached;
	}
	return AnchorStatus::Waiting;
}

void Renderer::heading(int, const std::string&)
{
}

void Renderer::BLOCK_DOC(bool)
{

}

// The same color is used for the admonition label and for the quote's left bar
ImVec4 Renderer::admonition_color(AdmonitionKind kind) const
{
	if (kind == AdmonitionKind::None)
		return ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
	return style.admonitionColors[(int)kind - 1];
}

static const char* admonition_label(Renderer::AdmonitionKind k)
{
	switch (k) {
	case Renderer::AdmonitionKind::Note:      return "NOTE";
	case Renderer::AdmonitionKind::Tip:       return "TIP";
	case Renderer::AdmonitionKind::Important: return "IMPORTANT";
	case Renderer::AdmonitionKind::Warning:   return "WARNING";
	case Renderer::AdmonitionKind::Caution:   return "CAUTION";
	default: return "";
	}
}

void Renderer::BLOCK_QUOTE(bool e)
{
	if (e) {
		m_quote_depth++;
		ImGui::Indent();
		// The quote frame provides the inter-block gap before its first child,
		// so suppress the dispatcher's gap for that first child. The flag is
		// cleared by the first child's dispatch, so later children still get
		// normal inter-block gaps.
		// (in a list, the first child gets no centralized gap: the flag would stay for the next top-level block)
		if (m_list_stack.empty())
			m_skip_next_block_gap = true;
		// Remember the indented column (X, Y) at quote entry, so we can draw
		// the vertical bar on the left at exit — by then the cursor has
		// moved to the end of the last rendered word.
		m_quote_start.push_back(ImGui::GetCursorScreenPos());
		// Arm admonition scan for the first text event inside this quote.
		m_admonition_scan_pending = true;
		m_admonition_kind = AdmonitionKind::None;
	} else {
		// Draw a vertical bar on the left side of the quote
		if (!m_quote_start.empty()) {
			float start_y = m_quote_start.back().y;
			float indented_x = m_quote_start.back().x;
			m_quote_start.pop_back();
			float end_y = ImGui::GetItemRectMax().y;  // the bottom of the last item of the quote
			float bar_x = indented_x - ImGui::GetStyle().IndentSpacing * 0.5f;
			bool is_admonition = (m_admonition_kind != AdmonitionKind::None);
			float thickness = is_admonition ? style.admonitionBarThickness : style.quoteBarThickness;
			ImColor bar_color = is_admonition
				? ImColor(admonition_color(m_admonition_kind))
				: ImColor(resolve_color(style.quoteBar, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]));
			ImGui::GetWindowDrawList()->AddLine(
				ImVec2(bar_x, start_y), ImVec2(bar_x, end_y),
				bar_color, thickness);
		}
		ImGui::Unindent();
		m_quote_depth--;
		m_admonition_kind = AdmonitionKind::None;
		m_admonition_scan_pending = false;
		m_admonition_skip_next_softbr = false;
	}
}

// Try to match a "[!NOTE]" / "[!TIP]" / ... marker at the start of `str`.
// On match, returns the admonition kind and sets `marker_end` to the byte
// just after the closing ']'. Case-insensitive per GitHub behavior.
static Renderer::AdmonitionKind match_admonition_marker(
	const char* str, const char* str_end, const char*& marker_end)
{
	if (str_end - str < 4 || str[0] != '[' || str[1] != '!')
		return Renderer::AdmonitionKind::None;
	const char* p = str + 2;
	const char* close = p;
	while (close < str_end && *close != ']')
		++close;
	if (close >= str_end)
		return Renderer::AdmonitionKind::None;
	auto eq_ci = [](const char* a, const char* a_end, const char* b) {
		size_t n = (size_t)(a_end - a);
		if (strlen(b) != n) return false;
		for (size_t i = 0; i < n; ++i) {
			char ca = a[i]; if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
			if (ca != b[i]) return false;
		}
		return true;
	};
	Renderer::AdmonitionKind kind = Renderer::AdmonitionKind::None;
	if      (eq_ci(p, close, "NOTE"))      kind = Renderer::AdmonitionKind::Note;
	else if (eq_ci(p, close, "TIP"))       kind = Renderer::AdmonitionKind::Tip;
	else if (eq_ci(p, close, "IMPORTANT")) kind = Renderer::AdmonitionKind::Important;
	else if (eq_ci(p, close, "WARNING"))   kind = Renderer::AdmonitionKind::Warning;
	else if (eq_ci(p, close, "CAUTION"))   kind = Renderer::AdmonitionKind::Caution;
	if (kind == Renderer::AdmonitionKind::None)
		return kind;
	marker_end = close + 1;
	return kind;
}

void Renderer::render_admonition_header(AdmonitionKind kind)
{
	ImVec4 col = admonition_color(kind);
	ImGui::PushStyleColor(ImGuiCol_Text, col);
	// Bold the label: fake it by pushing the strong flag and re-picking the font.
	bool saved_strong = m_is_strong;
	m_is_strong = true;
	set_font(true);
	ImGui::TextUnformatted(admonition_label(kind));
	set_font(false);
	m_is_strong = saved_strong;
	ImGui::PopStyleColor();
	// TextUnformatted already advanced the cursor to the next line;
	// the content starts right below, no extra NewLine needed.
}


void Renderer::BLOCK_CODE(const MD_BLOCK_CODE_DETAIL* detail, bool e)
{
    if (!can_use_child_windows())
        m_is_code = e;
    else
    {
    m_is_code_block = e;

    if (m_is_code_block) {
        m_code_block = "";
        m_code_block_sources.clear();
    }
    else {
        end_line();  // on its own line (in a list item, no block gap ended the line of the item's text)
        render_code_block();
    }

    if (detail->lang.text == NULL)
        m_code_block_language = "";
    else
        m_code_block_language = std::string(detail->lang.text, detail->lang.size);
    }
}

void Renderer::BLOCK_HTML(bool)
{

}

void Renderer::BLOCK_P(bool)
{
	// Inter-block spacing is handled centrally in block() on enter.
}

void Renderer::BLOCK_TABLE(const MD_BLOCK_TABLE_DETAIL* d, bool e)
{
	if (e) {
		// Unique ID per table in this render; labels inside cells may repeat.
		ImGui::PushID(m_table_id_counter++);
		ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp
		                      | ImGuiTableFlags_Resizable;
		if (m_table_border)
			flags |= ImGuiTableFlags_BordersInnerV
			       | ImGuiTableFlags_BordersOuterV
			       | ImGuiTableFlags_BordersOuterH
			       | ImGuiTableFlags_BordersInnerH;
		m_table_open = ImGui::BeginTable("##md", d->col_count, flags);
	} else {
		if (m_table_open)
			ImGui::EndTable();
		m_table_open = false;
		ImGui::PopID();
	}
}

void Renderer::BLOCK_THEAD(bool e)
{
	m_is_table_header = e;
	if (m_table_header_highlight) set_font(e);
}

void Renderer::BLOCK_TBODY(bool e)
{
	m_is_table_body = e;
}

void Renderer::BLOCK_TR(bool e)
{
	if (m_table_open && e) ImGui::TableNextRow();
}

// Non-left alignments (CENTER/RIGHT) can't be expressed directly in ImGui
// Tables, so we open a BeginGroup at cell entry, note the drawlist vertex
// count, render content normally, then on exit shift the new vertices by
// the appropriate X offset (cell_width - content_width, halved for center).
// Known limitation: for multi-line wrapped cells, all lines shift by the
// same offset (based on widest line), not per-line like HTML text-align.
static void begin_aligned_cell(MD_ALIGN align, int& vtx_start, float& cell_width)
{
	if (align == MD_ALIGN_DEFAULT || align == MD_ALIGN_LEFT)
		return;
	cell_width = ImGui::GetContentRegionAvail().x;
	vtx_start = ImGui::GetWindowDrawList()->VtxBuffer.Size;
	ImGui::BeginGroup();
}

// Returns the horizontal shift of the content
static float end_aligned_cell(MD_ALIGN align, int vtx_start, float cell_width)
{
	if (align == MD_ALIGN_DEFAULT || align == MD_ALIGN_LEFT)
		return 0.0f;
	ImGui::EndGroup();
	float content_w = ImGui::GetItemRectSize().x;
	if (content_w >= cell_width)
		return 0.0f;
	float offset = (align == MD_ALIGN_CENTER)
		? (cell_width - content_w) * 0.5f
		: (cell_width - content_w);
	if (offset <= 0.0f)
		return 0.0f;
	ImDrawList* dl = ImGui::GetWindowDrawList();
	for (int i = vtx_start; i < dl->VtxBuffer.Size; ++i)
		dl->VtxBuffer[i].pos.x += offset;
	return offset;
}

void Renderer::BLOCK_TH(const MD_BLOCK_TD_DETAIL* d, bool e)
{
	if (!m_table_open) return;
	if (e) {
		ImGui::TableNextColumn();
		m_cell_align = d->align;
		begin_aligned_cell(m_cell_align, m_cell_vtx_start, m_cell_width);
		m_cell_run_start = m_runs.size();
	} else {
		shift_runs(m_cell_run_start, end_aligned_cell(m_cell_align, m_cell_vtx_start, m_cell_width));
		m_cell_align = MD_ALIGN_DEFAULT;
	}
}

void Renderer::BLOCK_TD(const MD_BLOCK_TD_DETAIL* d, bool e)
{
	if (!m_table_open) return;
	if (e) {
		ImGui::TableNextColumn();
		m_cell_align = d->align;
		begin_aligned_cell(m_cell_align, m_cell_vtx_start, m_cell_width);
		m_cell_run_start = m_runs.size();
	} else {
		shift_runs(m_cell_run_start, end_aligned_cell(m_cell_align, m_cell_vtx_start, m_cell_width));
		m_cell_align = MD_ALIGN_DEFAULT;
	}
}

////////////////////////////////////////////////////////////////////////////////
void Renderer::set_href(bool e, const MD_ATTRIBUTE& src)
{
	if (e) {
		m_href.assign(src.text, src.size);
	} else {
		m_href.clear();
	}
}

void Renderer::set_img_src(bool e, const MD_ATTRIBUTE& src)
{
    if (e) {
        m_img_src.assign(src.text, src.size);
    } else {
        m_img_src.clear();
    }
}


void Renderer::set_font(bool e)
{
	if (e) {
		auto sized_font = get_font();
		ImGui::PushFont(sized_font.font, sized_font.size);
	} else {
		ImGui::PopFont();
	}
}

void Renderer::set_color(bool e)
{
	if (e) {
		ImGui::PushStyleColor(ImGuiCol_Text, get_color());
	} else {
		ImGui::PopStyleColor();
	}
}

void Renderer::line(ImColor c, bool under)
{
	ImVec2 mi = ImGui::GetItemRectMin();
	ImVec2 ma = ImGui::GetItemRectMax();

	if (!under) {
		ma.y -= ImGui::GetFontSize() / 2;
	}

	mi.y = ma.y;

	float lineThickness = ImGui::GetFontSize() / 14.5f;
	ImGui::GetWindowDrawList()->AddLine(mi, ma, c, lineThickness);
}

bool Renderer::link_item(const Style& style, const char* url)
{
	const ImGuiStyle& s = ImGui::GetStyle();
	ImVec4 underline;
	bool clicked = false;
	// A press on a link may start a text selection (which then holds the active id): the link opens on the release,
	// when the mouse did not drag, as in a browser
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		if (style.linkTooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("%s", url);
		underline = resolve_color(style.linkUnderlineHovered, s.Colors[ImGuiCol_ButtonHovered]);
		clicked = ImGui::IsMouseReleased(0) && !ImGui::IsMouseDragPastThreshold(0);
	} else {
		underline = resolve_color(style.linkUnderline, s.Colors[ImGuiCol_Button]);
	}
	line(underline, true);
	return clicked;
}

void Renderer::SPAN_A(const MD_SPAN_A_DETAIL* d, bool e)
{
	set_href(e, d->href);
	set_color(e);
}


void Renderer::SPAN_EM(bool e)
{
	m_is_em = e;
	set_font(e);
}

void Renderer::SPAN_STRONG(bool e)
{
	m_is_strong = e;
	set_font(e);
}


void Renderer::SPAN_IMG(const MD_SPAN_IMG_DETAIL* d, bool e)
{
	m_is_image = e;

	set_img_src(e, d->src);

	if (e) {

		image_info nfo;
		image_status status = get_image(nfo);
		if (status == image_status::loading) {
			draw_loading_spinner();
		} else if (status == image_status::ready) {

			// Images are drawn at the same scale as the text
			const float scale = ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi;
			nfo.size.x *= scale;
			nfo.size.y *= scale;

			ImVec2 const csz = ImGui::GetContentRegionAvail();
			if (nfo.size.x > csz.x) {
				const float r = nfo.size.y / nfo.size.x;
				nfo.size.x = csz.x;
				nfo.size.y = csz.x * r;
			}

			ImGui::Image(nfo.texture, nfo.size, nfo.uv0, nfo.uv1);
			// Stay on the same line so consecutive inline images or text continue horizontally
			ImGui::SameLine(0, 0);

			if (ImGui::IsItemHovered()) {

				//if (d->title.size) {
				//	ImGui::SetTooltip("%.*s", (int)d->title.size, d->title.text);
				//}
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

				if (ImGui::IsMouseClicked(0)) {
					follow_link();
				}
			}
		}
	}
}

void Renderer::SPAN_CODE(bool)
{

}


void Renderer::EnableLatex()
{
	m_md.flags |= MD_FLAG_LATEXMATHSPANS;
}

void Renderer::set_flag(unsigned flag, bool enable)
{
	if (enable)
		m_md.flags |= flag;
	else
		m_md.flags &= ~flag;
}

void Renderer::SPAN_LATEXMATH(bool e)
{
	if (e) {
		m_is_latex_inline = true;
		m_latex_buffer.clear();
		m_latex_source_begin = m_latex_source_end = nullptr;
	} else {
		m_is_latex_inline = false;
		render_latex_span(false);
	}
}

void Renderer::SPAN_LATEXMATH_DISPLAY(bool e)
{
	if (e) {
		m_is_latex_display = true;
		m_latex_buffer.clear();
		m_latex_source_begin = m_latex_source_end = nullptr;
	} else {
		m_is_latex_display = false;
		render_latex_span(true);
	}
}

bool Renderer::get_latex_texture(const std::string&, float, ImU32, bool, latex_texture&) const
{
	return false;  // no LaTeX in this base class
}

// Draws m_latex_buffer: inline, aligned on the text baseline; display, centered on its own line.
// Formulas are rasterized at the framebuffer density and displayed at the logical size (sharp on HiDPI).
void Renderer::render_latex_span(bool display)
{
	float pixel_scale = ImGui::GetIO().DisplayFramebufferScale.y;
	if (pixel_scale <= 0.01f)
		pixel_scale = 1.0f;
	float logical_font_size = ImGui::GetFontSize();
	ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
	const std::string delimiter = display ? "$$" : "$";
	const std::string source = delimiter + m_latex_buffer + delimiter;  // shown when it cannot be drawn, and copied

	latex_texture tex;
	if (!get_latex_texture(m_latex_buffer, logical_font_size * pixel_scale, color, display, tex)) {
		// Fallback: the formula's source, with its delimiters (in the error color, with the message
		// as a tooltip, when the formula is invalid)
		bool invalid = !tex.error.empty();
		if (invalid)
			ImGui::PushStyleColor(ImGuiCol_Text, resolve_color(style.errorColor, admonition_color(AdmonitionKind::Caution)));
		if (display)
			end_line();
		ImGui::TextUnformatted(source.c_str());
		record_run(m_latex_source_begin, m_latex_source_end, source);
		if (invalid) {
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", tex.error.c_str());
		}
		if (!display)
			ImGui::SameLine(0.0f, 0.0f);
		return;
	}
	if (tex.texture._TexData == nullptr && tex.texture._TexID == ImTextureID_Invalid)
		return;

	float logical_w = tex.size_px.x / pixel_scale;
	float logical_h = tex.size_px.y / pixel_scale;
	if (display) {
		// Display math: centered on its own line
		end_line();
		float avail = ImGui::GetContentRegionAvail().x;
		float pad_x = (avail - logical_w) * 0.5f;
		if (pad_x > 0.0f)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
		ImGui::Image(tex.texture, ImVec2(logical_w, logical_h));
		record_run(m_latex_source_begin, m_latex_source_end, source);
	} else {
		// An inline formula that does not fit on the rest of the line starts a new one, like a word, with the
		// punctuation glued after it ("$k$.")
		float glued = m_latex_source_end ? glued_width(m_latex_source_end + delimiter.size()) : 0.0f;
		if (logical_w + glued > ImGui::GetContentRegionAvail().x && line_is_open())
			ImGui::NewLine();
		// Inline math: the formula's baseline on the text baseline. ImGui::Text() draws from
		// cursor.y with the baseline at cursor.y + ascent, so the image top goes at
		// cursor.y + ascent - baseline.
		float logical_baseline = tex.baseline_px / pixel_scale;
		ImFontBaked* baked = ImGui::GetFontBaked();
		float text_ascent = baked ? baked->Ascent : logical_font_size * 0.8f;
		float saved_y = ImGui::GetCursorPosY();
		ImGui::SetCursorPosY(saved_y + text_ascent - logical_baseline);
		ImGui::Image(tex.texture, ImVec2(logical_w, logical_h));
		record_run(m_latex_source_begin, m_latex_source_end, source);
		ImGui::SameLine(0.0f, 0.0f);
		// Restore Y so the following inline content lands on the original line
		ImGui::SetCursorPosY(saved_y);
	}
}

void Renderer::SPAN_WIKILINK(const MD_SPAN_WIKILINK_DETAIL* d, bool e)
{
	// Rendered as a link whose href is the target; the click goes to open_wikilink()
	m_is_wikilink = e;
	set_href(e, d->target);
	set_color(e);
}

void Renderer::open_wikilink() const
{
}

void Renderer::SPAN_U(bool e)
{
	m_is_underline = e;
}

void Renderer::SPAN_DEL(bool e)
{
	m_is_strikethrough = e;
}

static bool is_blank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// The width of the rest of the word that goes on after str_end in the source, across the markup: "(**alignment**),"
// is one word for the reader, drawn as three spans. 0 when the span ends a word. Measured with the current font (the
// next spans may use another one: an estimate, enough to decide where the line breaks).
float Renderer::glued_width(const char* str_end) const
{
	if (str_end <= m_fragment_begin || str_end >= m_fragment_end || is_blank(str_end[-1]))
		return 0.0f;
	std::string rest;
	const char* s = str_end;
	while (s < m_fragment_end && !is_blank(*s) && *s != '<') {
		if (*s == ']' && s + 1 < m_fragment_end && s[1] == '(') {  // a link's target is not shown
			while (s < m_fragment_end && *s != ')')
				++s;
		}
		else if (*s != '*' && *s != '_' && *s != '`' && *s != '~' && *s != '[')
			rest += *s;
		++s;
	}
	return rest.empty() ? 0.0f : ImGui::CalcTextSize(rest.c_str()).x;
}

void Renderer::render_text(const char* str, const char* str_end)
{
	const ImGuiStyle& s = ImGui::GetStyle();
	bool is_lf = false;

	// <sub>/<sup>: render with a smaller font, offset vertically within
	// the current line so the baseline roughly matches GitHub's look.
	bool pushed_small_font = false;
	float base_font_size = get_font().size;
	float sub_sup_y_offset = 0.0f;
	if (m_is_sub || m_is_sup) {
		auto f = get_font();
		float small = f.size * style.subSupScale;
		ImGui::PushFont(f.font, small);
		pushed_small_font = true;
		sub_sup_y_offset = m_is_sup ? 0.0f : (base_font_size - small);
	}
	const float size = ImGui::GetFontSize();
	const float glued = (m_is_image || m_is_table_header) ? 0.0f : glued_width(str_end);

	// Mid-word break avoidance: if a new span begins mid-line and the
	// remaining width on the current line cannot fit even its first
	// word (including any leading blanks that were emitted between
	// adjacent spans), drop to a fresh line so the wrap uses the full
	// content width. Without this, ImGui's "force 1 char to fit"
	// fallback in CalcWordWrapPosition splits inside the first word
	// (e.g. "D" / "ear ImGui Bundle", or "browser u" / "sing ...").
	if (!m_is_image && !m_is_table_header && str < str_end) {
		float wl = ImGui::GetContentRegionAvail().x;
		const char* word_start = str;
		while (word_start < str_end
		       && (*word_start == ' ' || *word_start == '\t'))
			++word_start;
		const char* word_end = word_start;
		while (word_end < str_end && *word_end != ' '
		       && *word_end != '\n' && *word_end != '\t') {
			++word_end;
		}
		if (word_end > word_start) {
			// Width to fit = any leading blanks + first word (+ its glued rest, when it ends the span)
			ImVec2 chunk_sz = ImGui::CalcTextSize(str, word_end);
			if (word_end == str_end)
				chunk_sz.x += glued;
			if (chunk_sz.x > wl && line_is_open()) {
				ImGui::NewLine();
				// The leading blanks were inter-span spacing on the
				// previous line; drop them now that we've broken.
				str = word_start;
			}
		}
	}

	while (!m_is_image && str < str_end) {

		const char* te = str_end;

		if (!m_is_table_header) {
			// Inside a BeginTable cell, ContentRegionAvail.x is the cell
			// width; outside a table, it's the window content width.
			float wl = ImGui::GetContentRegionAvail().x;
			te = ImGui::GetFont()->CalcWordWrapPosition(
				size, str, str_end, wl);
			// ImGui cuts anywhere a word wider than the width it is given: here, the rest of a line that other
			// spans began ("Pan: right-dra" / "g"). The word goes whole to the next line, where only a word
			// wider than a line is cut.
			if (line_is_open() && te > str && te < str_end && !is_blank(te[-1]) && !is_blank(*te)) {
				while (te > str && !is_blank(te[-1]))
					--te;
				if (te == str) {
					ImGui::NewLine();
					continue;
				}
			}
			if (te == str) ++te;
			// The last word of the span is glued to the next span ("... way (" + "alignment"): if both do not fit,
			// the line breaks before the last word, which goes with the rest of its word to the next line
			if (te == str_end && glued > 0.0f && ImGui::CalcTextSize(str, te).x + glued > wl) {
				const char* last_word = te;
				while (last_word > str && !is_blank(last_word[-1]))
					--last_word;
				if (last_word > str)
					te = last_word;
			}
		}

		// Vertical offset for sub/sup while keeping the surrounding
		// line's Y intact (restored after the text is drawn).
		float saved_y = 0.0f;
		bool adjusted_y = false;
		if (sub_sup_y_offset != 0.0f) {
			saved_y = ImGui::GetCursorPosY();
			ImGui::SetCursorPosY(saved_y + sub_sup_y_offset);
			adjusted_y = true;
		}

		// <mark>: draw a highlight rectangle behind the text using
		// drawlist channels so the background stays under the glyphs.
		ImDrawList* dl = ImGui::GetWindowDrawList();
		bool mark_split = m_is_mark;
		if (mark_split) {
			dl->ChannelsSplit(2);
			dl->ChannelsSetCurrent(1);
		}

		if (m_is_error)
			ImGui::PushStyleColor(ImGuiCol_Text, resolve_color(style.errorColor, admonition_color(AdmonitionKind::Caution)));
		ImGui::TextUnformatted(str, te);
		record_run(str, te);
		if (m_is_error) {
			ImGui::PopStyleColor();
			if (!m_error_title.empty() && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", m_error_title.c_str());
		}

		if (mark_split) {
			dl->ChannelsSetCurrent(0);
			ImVec2 mi = ImGui::GetItemRectMin();
			ImVec2 ma = ImGui::GetItemRectMax();
			mi.x -= 2.0f; ma.x += 2.0f;
			dl->AddRectFilled(mi, ma, ImGui::GetColorU32(style.markBackground), 2.0f);
			dl->ChannelsMerge();
		}
		// <kbd>: draw a thin rounded border around the glyph run.
		if (m_is_kbd) {
			ImVec2 mi = ImGui::GetItemRectMin();
			ImVec2 ma = ImGui::GetItemRectMax();
			mi.x -= 3.0f; ma.x += 3.0f;
			ImU32 border = ImGui::GetColorU32(resolve_color(style.kbdBorder, s.Colors[ImGuiCol_Border]));
#if IMGUI_VERSION_NUM < 19276
			dl->AddRect(mi, ma, border, 3.0f, 0, 1.0f);
#else
			dl->AddRect(mi, ma, border, 3.0f, 1.0f);
#endif
		}

		// Restore Y so the next span sits on the original line.
		if (adjusted_y) {
			float cur_x = ImGui::GetCursorPosX();
			ImGui::SetCursorPos(ImVec2(cur_x, saved_y));
		}

		if (te > str && *(te - 1) == '\n') {
			is_lf = true;
		}

		if (!m_href.empty()) {
			bool tapOpened = !m_is_wikilink && tap_opens_url()
				&& ImGui::GetIO().MouseSource == ImGuiMouseSource_TouchScreen;
			if (link_item(style, m_href.c_str()) && !tapOpened) {
				if (m_is_wikilink)
					open_wikilink();
				else
					follow_link();
			}
		}
		if (m_is_underline) {
			line(s.Colors[ImGuiCol_Text], true);
		}
		if (m_is_strikethrough) {
			line(s.Colors[ImGuiCol_Text], false);
		}

		str = te;

		while (str < str_end && *str == ' ')++str;
	}

	if (pushed_small_font)
		ImGui::PopFont();

	if (!is_lf)
    {
        ImGui::SameLine(0.0f, 0.0f);
    }
}


bool Renderer::render_entity(const char* str, const char* str_end)
{
	const size_t sz = str_end - str;
	if (strncmp(str, "&nbsp;", sz) == 0) {
		ImGui::TextUnformatted(""); ImGui::SameLine();
		return true;
	}
	return false;
}

static bool skip_spaces(const std::string& d, size_t& p)
{
	for (; p < d.length(); ++p) {
		if (d[p] != ' ' && d[p] != '\t') {
			break;
		}
	}
	return p < d.length();
}

static std::string get_div_class(const char* str, const char* str_end)
{
	if (str_end <= str)return "";

	std::string d(str, str_end - str);
	if (d.back() == '>')d.pop_back();

	const char attr[] = "class";
	size_t p = d.find(attr);
	if (p == std::string::npos)return "";
	p += sizeof(attr)-1;

	if (!skip_spaces(d, p))return "";

	if (d[p] != '=')return "";
	++p;

	if (!skip_spaces(d, p))return "";

	bool has_q = false;

	if (d[p] == '"' || d[p] == '\'') {
		has_q = true;
		++p;
	}
	if (p == d.length())return "";

	if (!has_q) {
		if (!skip_spaces(d, p))return "";
	}

	size_t pe;
	for (pe = p; pe < d.length(); ++pe) {

		const char c = d[pe];

		if (has_q) {
			if (c == '"' || c == '\'') {
				break;
			}
		} else {
			if (c == ' ' || c == '\t') {
				break;
			}
		}
	}

	return d.substr(p, pe - p);

}

// Helper: extract an attribute value from an HTML tag string
// e.g. extract_html_attr("<img src=\"foo.png\" width=\"200\">", "src") -> "foo.png"
static std::string extract_html_attr(const std::string& tag, const char* name)
{
	std::string needle = std::string(name) + "=";
	auto pos = tag.find(needle);
	if (pos == std::string::npos) return "";
	pos += needle.size();
	if (pos >= tag.size()) return "";
	char quote = tag[pos];
	if (quote == '"' || quote == '\'') {
		auto end = tag.find(quote, pos + 1);
		if (end == std::string::npos) return "";
		return tag.substr(pos + 1, end - pos - 1);
	}
	// No quotes: read until space or >
	auto end = tag.find_first_of(" \t\n>", pos);
	if (end == std::string::npos) end = tag.size();
	return tag.substr(pos, end - pos);
}

static int extract_html_int_attr(const std::string& tag, const char* name, int defaultValue)
{
	std::string val = extract_html_attr(tag, name);
	if (val.empty()) return defaultValue;
	try { return std::stoi(val); }
	catch (...) { return defaultValue; }
}

static bool details_hidden(const std::vector<bool>& stack);  // defined below

// md4c hands a comment over as raw HTML: one chunk inline, one chunk per line in a block, each line followed by a
// "\n" chunk (skipped too, when it follows the end of the comment)
bool Renderer::skip_html_comment(const char* str, const char* str_end, bool afterComment)
{
	std::string chunk(str, str_end);
	size_t start = chunk.find_first_not_of(" \t\r\n");
	if (!m_in_html_comment)
	{
		if (start == std::string::npos)
			return afterComment;  // the newline after a comment line
		if (chunk.compare(start, 4, "<!--") != 0)
			return false;
		m_in_html_comment = true;
		start += 4;
	}
	if (start != std::string::npos && chunk.find("-->", start) != std::string::npos)
	{
		m_in_html_comment = false;
		m_html_comment_closed = true;
	}
	return true;
}

bool Renderer::check_html(const char* str, const char* str_end)
{
	const size_t sz = str_end - str;

	const std::string tag(str, sz);
	if (tag == "<br>" || tag == "<br/>" || tag == "<br />") {
		ImGui::NewLine();
		return true;
	}
	if (tag == "<hr>" || tag == "<hr/>" || tag == "<hr />") {
		ImGui::Separator();
		return true;
	}
	if (strncmp(str, "<u>", sz) == 0) {
		m_is_underline = true;
		return true;
	}
	if (strncmp(str, "</u>", sz) == 0) {
		m_is_underline = false;
		return true;
	}

	// Inline HTML spans with built-in rendering. The text between the
	// opening and closing tags arrives as normal MD_TEXT_NORMAL events;
	// render_text() inspects these flags and adjusts accordingly.
	if (strncmp(str, "<sub>",   sz) == 0) { m_is_sub  = true;  return true; }
	if (strncmp(str, "</sub>",  sz) == 0) { m_is_sub  = false; return true; }
	if (strncmp(str, "<sup>",   sz) == 0) { m_is_sup  = true;  return true; }
	if (strncmp(str, "</sup>",  sz) == 0) { m_is_sup  = false; return true; }
	if (strncmp(str, "<kbd>",   sz) == 0) { m_is_kbd  = true;  return true; }
	if (strncmp(str, "</kbd>",  sz) == 0) { m_is_kbd  = false; return true; }
	if (strncmp(str, "<mark>",  sz) == 0) { m_is_mark = true;  return true; }
	if (strncmp(str, "</mark>", sz) == 0) { m_is_mark = false; return true; }
	// <md-error title="reason">: text in the error color, the reason as a tooltip (a failed transclusion)
	if (sz > 9 && strncmp(str, "<md-error", 9) == 0) {
		m_is_error = true;
		m_error_title.clear();
		const char* t = strstr(str, "title=\"");
		if (t && t < str_end) {
			t += 7;
			const char* e = (const char*)memchr(t, '"', str_end - t);
			if (e) m_error_title.assign(t, e);
		}
		return true;
	}
	if (strncmp(str, "</md-error>", sz) == 0) { m_is_error = false; return true; }

	// <details> / </details>: open a CollapsingHeader whose label comes
	// from the inner <summary>...</summary> line. When collapsed, all
	// subsequent rendering is suppressed until the matching </details>.
	// Requires the standard CommonMark pattern with blank lines:
	//   <details>
	//   <summary>Label</summary>
	//
	//   Markdown content here…
	//
	//   </details>
	auto starts_with = [&](const char* prefix) {
		size_t n = strlen(prefix);
		return sz >= n && strncmp(str, prefix, n) == 0;
	};

	// <pre>...</pre>: block-level preformatted monospace text.
	// md4c treats <pre> as a CommonMark type-1 HTML block, delivering the
	// content through MD_TEXT_HTML events that include the tags themselves.
	// Buffer across chunks in case md4c splits on newlines.
	auto render_pre = [&](const char* s, const char* e) {
		// Skip one optional leading '\n' right after <pre>.
		if (s < e && *s == '\n') ++s;
		// When hidden by a collapsed <details> or a fold, consume but don't render.
		if (hidden())
			return;
		// m_is_code makes subclass get_font() return the monospace code font.
		m_is_code = true;
		auto f = get_font();
		if (f.font) ImGui::PushFont(f.font, f.size);
		while (s < e) {
			const char* eol = s;
			while (eol < e && *eol != '\n') ++eol;
			if (eol > s)
				ImGui::TextUnformatted(s, eol);
			else
				ImGui::NewLine();
			s = (eol < e) ? eol + 1 : e;
		}
		if (f.font) ImGui::PopFont();
		m_is_code = false;
	};
	auto find_close_pre = [&](const char* s, const char* e) -> const char* {
		for (const char* q = s; q + 6 <= e; ++q)
			if (strncmp(q, "</pre>", 6) == 0) return q;
		return nullptr;
	};
	if (m_in_pre) {
		const char* close = find_close_pre(str, str_end);
		if (close) {
			m_pre_buffer.append(str, close);
			render_pre(m_pre_buffer.data(), m_pre_buffer.data() + m_pre_buffer.size());
			m_pre_buffer.clear();
			m_in_pre = false;
		} else {
			m_pre_buffer.append(str, str_end);
		}
		return true;
	}
	if (starts_with("<pre>") || starts_with("<pre ")) {
		const char* gt = (const char*)memchr(str, '>', str_end - str);
		const char* content_start = gt ? gt + 1 : str_end;
		const char* close = find_close_pre(content_start, str_end);
		if (close) {
			render_pre(content_start, close);
		} else {
			m_pre_buffer.assign(content_start, str_end);
			m_in_pre = true;
		}
		return true;
	}
	if (starts_with("<details>") || starts_with("<details ")) {
		m_details_awaiting_summary = true;
		// Check for the `open` attribute (boolean HTML attribute: `<details open>`
		// or `<details open="open">`). Simple substring check between the tag
		// start and the closing '>'.
		const char* tag_end = (const char*)memchr(str, '>', str_end - str);
		if (!tag_end) tag_end = str_end;
		bool has_open_attr = false;
		for (const char* q = str; q + 4 <= tag_end; ++q) {
			if ((q == str || q[-1] == ' ' || q[-1] == '\t') &&
			    strncmp(q, "open", 4) == 0 &&
			    (q + 4 == tag_end || q[4] == ' ' || q[4] == '\t' || q[4] == '=' || q[4] == '>')) {
				has_open_attr = true;
				break;
			}
		}
		m_details_awaiting_open_default = has_open_attr;
		// Swallow the stray "\n" chunk md4c emits between <details> and
		// <summary> so the CollapsingHeader sits flush with the content
		// above it.
		m_details_suppress_next_raw_html = true;
		return true;
	}
	if (starts_with("<summary>")) {
		// Find "</summary>" on the same chunk.
		const char* p = str + strlen("<summary>");
		const char* close = nullptr;
		for (const char* q = p; q + 10 <= str_end; ++q) {
			if (strncmp(q, "</summary>", 10) == 0) { close = q; break; }
		}
		std::string raw_label = (close != nullptr)
			? std::string(p, close)
			: std::string(p, str_end);
		// Decode the common HTML entities so `<summary>&lt;pre&gt;</summary>`
		// displays as `<pre>` in the collapsing header.
		std::string label;
		label.reserve(raw_label.size());
		for (size_t i = 0; i < raw_label.size(); ) {
			if (raw_label[i] == '&') {
				if (raw_label.compare(i, 4, "&lt;")   == 0) { label += '<';  i += 4; continue; }
				if (raw_label.compare(i, 4, "&gt;")   == 0) { label += '>';  i += 4; continue; }
				if (raw_label.compare(i, 5, "&amp;")  == 0) { label += '&';  i += 5; continue; }
				if (raw_label.compare(i, 6, "&quot;") == 0) { label += '"';  i += 6; continue; }
				if (raw_label.compare(i, 6, "&apos;") == 0) { label += '\''; i += 6; continue; }
				if (raw_label.compare(i, 5, "&#39;")  == 0) { label += '\''; i += 5; continue; }
			}
			label += raw_label[i++];
		}
		if (!m_details_awaiting_summary) {
			// Orphan <summary> (not inside a <details>) — just show the label.
			ImGui::TextUnformatted(label.c_str());
			return true;
		}
		// If any ancestor <details> is collapsed, suppress this nested header
		// entirely. Still push to the stack (as closed) so the matching
		// </details> pops correctly.
		if (hidden()) {  // (a collapsed ancestor, or a fold)
			m_details_id_counter++;  // consume the id anyway: the ids of the following headers must not depend on what is open
			m_details_open_stack.push_back(false);
			m_details_awaiting_summary = false;
			m_details_awaiting_open_default = false;
			return true;
		}
		ImGui::PushID(m_details_id_counter++);
		// Honour the `open` attribute on <details open>: pre-open the
		// CollapsingHeader once (user can still collapse it afterwards).
		if (m_details_awaiting_open_default)
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		bool open = ImGui::CollapsingHeader(label.c_str());
		ImGui::PopID();
		if (!open) {  // the outermost collapsed section: the place of the headings it hides
			m_hidden_y = ImGui::GetItemRectMin().y - ImGui::GetWindowPos().y + ImGui::GetScrollY();
			m_hidden_bottom = ImGui::GetItemRectMax().y - ImGui::GetWindowPos().y + ImGui::GetScrollY();
			m_hidden_id = ImGui::GetItemID();
		}
		m_details_open_stack.push_back(open);
		if (open)
			ImGui::Indent();
		m_details_awaiting_summary = false;
		m_details_awaiting_open_default = false;
		// Eat the trailing "\n" chunk md4c emits right after </summary>;
		// rendering it would advance the cursor by a full line and
		// produce a visibly oversized gap before the body content.
		m_details_suppress_next_raw_html = true;
		return true;
	}
	if (starts_with("</details>")) {
		if (!m_details_open_stack.empty()) {
			bool was_open = m_details_open_stack.back();
			m_details_open_stack.pop_back();
			if (was_open)
				ImGui::Unindent();
		}
		m_details_awaiting_summary = false;
		// Eat the trailing "\n" chunk md4c emits right after this tag
		// so content below sits flush with the collapsible.
		m_details_suppress_next_raw_html = true;
		return true;
	}

	// <img src="..." width="..." height="...">
	if (sz >= 4 && strncmp(str, "<img", 4) == 0) {
		// Consume but don't render when hidden by a collapsed <details> or a fold.
		if (hidden())
			return true;
		std::string tag(str, str_end);
		std::string src = extract_html_attr(tag, "src");
		if (src.empty()) return false;

		int width = extract_html_int_attr(tag, "width", 0);
		int height = extract_html_int_attr(tag, "height", 0);

		m_img_src = src;
		image_info nfo;
		image_status status = get_image(nfo);
		if (status == image_status::loading) {
			draw_loading_spinner();
		} else if (status == image_status::ready) {
			const float scale = ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi;
			float natural_w = nfo.size.x * scale;
			float natural_h = nfo.size.y * scale;

			if (width > 0 && height > 0) {
				nfo.size.x = (float)width;
				nfo.size.y = (float)height;
			} else if (width > 0) {
				nfo.size.x = (float)width;
				nfo.size.y = natural_h * ((float)width / natural_w);
			} else if (height > 0) {
				nfo.size.x = natural_w * ((float)height / natural_h);
				nfo.size.y = (float)height;
			} else {
				nfo.size.x = natural_w;
				nfo.size.y = natural_h;
			}

			// Clamp to available width
			ImVec2 const csz = ImGui::GetContentRegionAvail();
			if (nfo.size.x > csz.x) {
				float r = nfo.size.y / nfo.size.x;
				nfo.size.x = csz.x;
				nfo.size.y = csz.x * r;
			}

			ImGui::Image(nfo.texture, nfo.size, nfo.uv0, nfo.uv1);
			// Stay on the same line so consecutive inline images or text continue horizontally
			ImGui::SameLine(0, 0);
		}
		m_img_src.clear();
		return true;
	}

	if (strncmp(str, "<center>", sz) == 0) {
		if (hidden())
			return true;
		if (!m_in_center) {
			m_in_center = true;
			begin_aligned_cell(MD_ALIGN_CENTER, m_center_vtx_start, m_center_width);
			m_center_run_start = m_runs.size();
		}
		return true;
	}
	if (strncmp(str, "</center>", sz) == 0) {
		if (hidden())
			return true;
		if (m_in_center) {
			m_in_center = false;
			shift_runs(m_center_run_start, end_aligned_cell(MD_ALIGN_CENTER, m_center_vtx_start, m_center_width));
		}
		return true;
	}

	const size_t div_sz = 4;
	if (strncmp(str, "<div", sz > div_sz ? div_sz : sz) == 0) {
		m_div_stack.emplace_back(get_div_class(str + div_sz, str_end));
		html_div(m_div_stack.back(), true);
		return true;
	}
	if (strncmp(str, "</div>", sz) == 0) {
		if (m_div_stack.empty())return false;
		html_div(m_div_stack.back(), false);
		m_div_stack.pop_back();
		return true;
	}
	return false;
}


void Renderer::html_div(const std::string& dclass, bool e)
{
	//Example:
#if 0
	if (dclass == "red") {
		if (e) {
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
		} else {
			ImGui::PopStyleColor();
		}
	}
#endif
    (void)dclass; (void)e;
}

void Renderer::render_code_block()
{
    m_is_code = true;
    push_code_style();

    const char* begin = m_code_block.data();
    const char *end = m_code_block.data() + m_code_block.size();
    render_text(begin, end);

    pop_code_style();
    m_is_code = false;
}

void Renderer::push_code_style()
{
	auto code_font = get_font();
	ImGui::PushFont(code_font.font, code_font.size);

    // Automatic: the text color, a little more blue
    auto automatic = ImGui::GetStyle().Colors[ImGuiCol_Text];
    automatic.z *= 1.15f;
    ImGui::PushStyleColor(ImGuiCol_Text, resolve_color(style.codeColor, automatic));

}
void Renderer::pop_code_style()
{
    ImGui::PopStyleColor();
    ImGui::PopFont();
}


void Renderer::render_inline_code(const char *str, const char *str_end)
{
    m_is_code = true;
    push_code_style();
    render_text(str, str_end);
    pop_code_style();
    m_is_code = false;
}

// True when we're inside a <details> that the user has collapsed.
// Used to suppress all drawing between <summary>…</summary> and
// </details>, while still letting check_html() see the </details>
// tag so it can close the stack.
static bool details_hidden(const std::vector<bool>& stack)
{
	for (bool open : stack)
		if (!open) return true;
	return false;
}

bool Renderer::hidden() const
{
	return m_fold_level > 0 || details_hidden(m_details_open_stack);
}

int Renderer::text(MD_TEXTTYPE type, const char* str, const char* str_end)
{
	// Even while hidden we keep processing raw HTML so </details>
	// can pop the stack; everything else is discarded.
	if (hidden() && type != MD_TEXT_HTML) {
		if (m_hlevel > 0 && (type == MD_TEXT_NORMAL || type == MD_TEXT_CODE))
			m_heading_text.append(str, str_end);  // a heading inside a collapsed section is listed, with its text
		bool searched = type == MD_TEXT_NORMAL || type == MD_TEXT_CODE || type == MD_TEXT_LATEXMATH;
		if (searched && document && !document->query.empty())  // the search finds it, at the section's header
			add_hidden_text(str, str_end);
		return 0;
	}
	bool afterComment = m_html_comment_closed;
	m_html_comment_closed = false;

	switch (type) {
	case MD_TEXT_NORMAL:
		if (m_hlevel > 0)
			m_heading_text.append(str, str_end);
		if (m_admonition_scan_pending) {
			m_admonition_scan_pending = false;
			const char* marker_end = nullptr;
			AdmonitionKind kind = match_admonition_marker(str, str_end, marker_end);
			if (kind != AdmonitionKind::None) {
				m_admonition_kind = kind;
				render_admonition_header(kind);
				m_admonition_skip_next_softbr = true;
				// Render any trailing content on the marker's own line
				// (skip leading whitespace).
				while (marker_end < str_end && (*marker_end == ' ' || *marker_end == '\t'))
					++marker_end;
				if (marker_end < str_end)
					render_text(marker_end, str_end);
				break;
			}
		}
		render_text(str, str_end);
		break;
	case MD_TEXT_CODE:
		if (m_hlevel > 0)
			m_heading_text.append(str, str_end);
        if (m_is_code_block) {
            m_code_block_sources.emplace_back(m_code_block.size(), (size_t)(str - m_fragment_begin));
            m_code_block += std::string(str, str_end);
        }
        else
            render_inline_code(str, str_end);
		break;
	case MD_TEXT_NULLCHAR:
		break;
	case MD_TEXT_BR:
		ImGui::NewLine();
		record_run(nullptr, nullptr, "\n");  // a hard line break, copied as a newline
		m_runs.back().min.x = m_runs.back().max.x;  // it has no width (its rectangle is the one of the text before)
		break;
	case MD_TEXT_SOFTBR:
		if (m_admonition_skip_next_softbr) {
			m_admonition_skip_next_softbr = false;
			break;
		}
		soft_break();
		break;
	case MD_TEXT_ENTITY:
		if (!render_entity(str, str_end)) {
			render_text(str, str_end);
		};
		break;
	case MD_TEXT_HTML:
		if (skip_html_comment(str, str_end, afterComment))
			break;
		if (m_html_gap_pending) {  // the gap of an HTML block comes before its first chunk that is not a comment
			m_html_gap_pending = false;
			separate_block();
		}
		if (!check_html(str, str_end)) {
			// Drop stray raw-HTML chunks (typically "\n") that
			// md4c emits between recognized tags inside/around a
			// <details> block — drawing them would widen the
			// collapsible's vertical spacing.
			if (m_details_suppress_next_raw_html) {
				m_details_suppress_next_raw_html = false;
			} else if (!hidden()) {
				render_text(str, str_end);
			}
		} else {
			// Recognized tag: clear any pending suppression since
			// the tag itself was already handled and any flag it
			// set should take precedence on the following chunk.
		}
		break;
	case MD_TEXT_LATEXMATH:
		if (m_is_latex_inline || m_is_latex_display) {
			if (m_latex_buffer.empty())
				m_latex_source_begin = str;
			m_latex_source_end = str_end;
			m_latex_buffer.append(str, str_end - str);
		} else {
			render_text(str, str_end);
		}
		break;
	default:
		break;
	}

	return 0;
}

int Renderer::block(MD_BLOCKTYPE type, void* d, bool e)
{
	// Any block callback ends the HTML block whose gap is pending (an HTML block holds no other block)
	m_html_gap_pending = false;

	if (type == MD_BLOCK_QUOTE || type == MD_BLOCK_LI)
		m_container_depth += e ? 1 : -1;
	// A heading of the fold's level or above, at the top level, ends the fold: it draws
	if (type == MD_BLOCK_H && e && m_fold_level > 0 && top_level()
	    && (int)((MD_BLOCK_H_DETAIL*)d)->level <= m_fold_level)
		m_fold_level = 0;

	// Suppress block rendering while inside a collapsed <details>.
	// BLOCK_HTML pairs still arrive (the tags themselves land as
	// MD_TEXT_HTML in text(), which is allowed through), so we just
	// drop any other block work here.
	if (hidden()) {
		if (type == MD_BLOCK_H) {  // a heading inside a collapsed section: listed, at the section's header
			int level = (int)((MD_BLOCK_H_DETAIL*)d)->level;
			if (e) {
				m_hlevel = (unsigned)level;
				m_heading_text.clear();
			} else {
				add_heading(level, m_hidden_y, true);
				m_hlevel = 0;
			}
		}
		return 0;
	}
	if (e)
		++m_block_number;

	// Centralized inter-block spacing: on enter of any top-level
	// "paragraph-class" block, call add_block_gap() -- unless the flag
	// m_skip_next_block_gap tells us to suppress it (true at the very
	// first block, and for the first child of a quote). Blocks nested
	// inside a list are skipped; LI handles its own spacing.
	if (e) {
		bool is_separator_eligible = false;
		switch (type) {
		case MD_BLOCK_P:
		case MD_BLOCK_H:
		case MD_BLOCK_QUOTE:
		case MD_BLOCK_HR:
		case MD_BLOCK_CODE:
		case MD_BLOCK_HTML:
		case MD_BLOCK_TABLE:
		case MD_BLOCK_UL:
		case MD_BLOCK_OL:
			is_separator_eligible = m_list_stack.empty();
			break;
		default:
			break;
		}
		if (is_separator_eligible) {
			if (type == MD_BLOCK_HTML)
				m_html_gap_pending = true;  // see text(): a block holding only a comment takes no space
			else if (separate_block() && type == MD_BLOCK_H) {
				// Extra breathing room above headers, decaying with depth:
				// H1 gets the most, H6 none. Light scheme: 0.15 em per step.
				int level = ((MD_BLOCK_H_DETAIL*)d)->level;
				int steps = 7 - level;
				if (steps > 0)
					ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * style.headerGapStep * (float)steps));
			}
		}
	}

	// Inside a list, the blocks get no centralized gap (LI spaces the items): a block that follows content of its
	// item (the item's text, a paragraph, a quote) starts on its own line, below a gap. The nested lists end the line
	// themselves (LI).
	if (e && !m_list_stack.empty()) {
		switch (type) {
		case MD_BLOCK_P:
		case MD_BLOCK_H:
		case MD_BLOCK_QUOTE:
		case MD_BLOCK_HR:
		case MD_BLOCK_CODE:
		case MD_BLOCK_TABLE: {
			ImVec2 last = ImGui::GetItemRectMin();
			if (last.x != m_list_block_mark.x || last.y != m_list_block_mark.y)
				add_block_gap(style.blockGap);
			m_list_block_mark = ImGui::GetItemRectMin();
			break;
		}
		default:
			break;
		}
	}

	switch (type)
	{
	case MD_BLOCK_DOC:
		BLOCK_DOC(e);
		break;
	case MD_BLOCK_QUOTE:
		BLOCK_QUOTE(e);
		break;
	case MD_BLOCK_UL:
		BLOCK_UL((MD_BLOCK_UL_DETAIL*)d, e);
		break;
	case MD_BLOCK_OL:
		BLOCK_OL((MD_BLOCK_OL_DETAIL*)d, e);
		break;
	case MD_BLOCK_LI:
		BLOCK_LI((MD_BLOCK_LI_DETAIL*)d, e);
		break;
	case MD_BLOCK_HR:
		BLOCK_HR(e);
		break;
	case MD_BLOCK_H:
		BLOCK_H((MD_BLOCK_H_DETAIL*)d, e);
		break;
	case MD_BLOCK_CODE:
		BLOCK_CODE((MD_BLOCK_CODE_DETAIL*)d, e);
		break;
	case MD_BLOCK_HTML:
		BLOCK_HTML(e);
		break;
	case MD_BLOCK_P:
		BLOCK_P(e);
		break;
	case MD_BLOCK_TABLE:
		BLOCK_TABLE((MD_BLOCK_TABLE_DETAIL*)d, e);
		break;
	case MD_BLOCK_THEAD:
		BLOCK_THEAD(e);
		break;
	case MD_BLOCK_TBODY:
		BLOCK_TBODY(e);
		break;
	case MD_BLOCK_TR:
		BLOCK_TR(e);
		break;
	case MD_BLOCK_TH:
		BLOCK_TH((MD_BLOCK_TD_DETAIL*)d, e);
		break;
	case MD_BLOCK_TD:
		BLOCK_TD((MD_BLOCK_TD_DETAIL*)d, e);
		break;
	default:
		assert(false);
		break;
	}

	return 0;
}

int Renderer::span(MD_SPANTYPE type, void* d, bool e)
{
	// Suppress span rendering while inside a collapsed <details>.
	if (hidden())
		return 0;
	// Any span opening before the first text in a quote means this is not
	// an admonition (the marker must be plain text).
	if (e && m_admonition_scan_pending)
		m_admonition_scan_pending = false;
	switch (type)
	{
	case MD_SPAN_EM:
		SPAN_EM(e);
		break;
	case MD_SPAN_STRONG:
		SPAN_STRONG(e);
		break;
	case MD_SPAN_A:
		SPAN_A((MD_SPAN_A_DETAIL*)d, e);
		break;
	case MD_SPAN_IMG:
		SPAN_IMG((MD_SPAN_IMG_DETAIL*)d, e);
		break;
	case MD_SPAN_CODE:
		SPAN_CODE(e);
		break;
	case MD_SPAN_DEL:
		SPAN_DEL(e);
		break;
	case MD_SPAN_LATEXMATH:
		SPAN_LATEXMATH(e);
		break;
	case MD_SPAN_LATEXMATH_DISPLAY:
		SPAN_LATEXMATH_DISPLAY(e);
		break;
	case MD_SPAN_WIKILINK:
		SPAN_WIKILINK((MD_SPAN_WIKILINK_DETAIL*)d, e);
		break;
	case MD_SPAN_U:
		SPAN_U(e);
		break;
	default:
		assert(false);
		break;
	}

	return 0;
}

int Renderer::print(const char* str, const char* str_end)
{
	m_headings.clear();
	m_heading_details.clear();
	m_heading_folds.clear();
	m_fold_storage = ImGui::GetStateStorage();
	m_slug_occurrences.clear();
	if (str >= str_end)
        return 0;

    // Inter-block spacing is emitted by block() before each top-level
    // separator-eligible block; set the flag so the very first one is
    // skipped (rendering starts flush at the caller's cursor).
    // In a document, a fragment that follows other content keeps it: the fragments read as one text.
    m_skip_next_block_gap = !document || ImGui::GetCursorPosY() <= document->contentStartY;
    m_in_html_comment = false;
    m_html_comment_closed = false;
    m_html_gap_pending = false;
    m_table_id_counter = 0;
    m_details_id_counter = 0;
    m_container_depth = 0;
    m_in_pre = false;
    m_pre_buffer.clear();
    m_runs.clear();
    m_hidden_texts.clear();
    m_code_block_matches.clear();
    m_fragment_begin = str;
    m_fragment_end = str_end;
    m_block_number = 0;

    // The fragment's selection is drawn behind its text: the text goes to channel 1, the highlight to channel 0
    ImGuiID selectionId = ImGui::GetID("##rich_md_selection");
    m_fragment_id = selectionId;
    if (m_selection_fragment == selectionId
        && (!selectableText || ImHashStr(str, (size_t)(str_end - str)) != m_selection_text_hash))
        m_selection_fragment = 0;  // not selectable anymore, or another text (its offsets mean nothing anymore)
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    bool drawSelection = m_selection_fragment == selectionId && m_selection_anchor != m_selection_focus;
    const bool drawSearch = document && !document->query.empty();  // the matches of the search, behind the text too
    if (drawSelection || drawSearch) {
        m_selection_splitter.Split(drawList, 2);
        m_selection_splitter.SetCurrentChannel(drawList, 1);
    }

	if (style.fragmentGapTop > 0.0f)
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * style.fragmentGapTop));
	// The margin of the fold arrows, at the left of the content: the document's, else this render's own
	const bool ownMargin = foldableHeadings && !document;
	const float em = get_font().size > 0.f ? get_font().size : ImGui::GetFontSize();  // the regular text's size
	m_fold_margin = document ? document->foldMargin : (ownMargin ? em * 1.5f : 0.f);
	m_fold_margin_left = document ? document->foldMarginLeft : ImGui::GetCursorScreenPos().x;
	if (ownMargin)
		ImGui::Indent(m_fold_margin);
	load_document_fold();
	int result = md_parse(str, (MD_SIZE)(str_end - str), &m_md, this);
	save_document_fold();
	if (ownMargin)
		ImGui::Unindent(m_fold_margin);
	const int fragmentRank = document ? document->fragmentCount++ : -1;
	if (!document)
		resolve_anchor(selectionId);
	else if (selectableText && document->selectFragment == fragmentRank)  // the find bar closed on a match of it
		select_range(selectionId, document->selectBegin, document->selectEnd);

    if (selectableText)
        update_selection(selectionId);
    if (drawSelection || drawSearch) {
        m_selection_splitter.SetCurrentChannel(drawList, 0);
        if (drawSearch)
            search_fragment(fragmentRank);
        if (drawSelection)
            draw_selection();
        m_selection_splitter.Merge(drawList);
    }
	if (style.fragmentGapBottom < 0.0f)
		end_line();
	else if (style.fragmentGapBottom > 0.0f)
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * style.fragmentGapBottom));
	return result;
}

////////////////////////////////////////////////////////////////////////////////

Renderer::MdSizedFont Renderer::get_font() const
{
	return MdSizedFont{nullptr, 0.0f}; // no font in this base class!

	//Example:
#if 0
	if (m_is_table_header) {
		return g_font_bold;
	}

	switch (m_hlevel)
	{
	case 0:
		return m_is_strong ? g_font_bold : g_font_regular;
	case 1:
		return g_font_bold_large;
	default:
		return g_font_bold;
	}
#endif

};

ImVec4 Renderer::default_link_color()
{
    auto col_text = ImGui::GetStyle().Colors[ImGuiCol_Text];

    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(col_text.x, col_text.y, col_text.z, h, s, v);
    h = 0.57f;
    if (v >= 0.8f)
        v = 0.8f;
    if (v <= 0.5f)
        v = 0.5f;
    if (s <= 0.5f)
        s = 0.5f;

    ImGui::ColorConvertHSVtoRGB(h, s, v, col_text.x, col_text.y, col_text.z);
    return col_text;
}


ImVec4 Renderer::link_color() const
{
	return resolve_color(style.linkColor, default_link_color());
}

ImVec4 Renderer::get_color() const
{
	if (!m_href.empty())
    {
		return link_color();
	}
	return  ImGui::GetStyle().Colors[ImGuiCol_Text];
}


Renderer::image_status Renderer::get_image(image_info& nfo) const
{
	//Use m_href to identify images

	//Example - Imgui font texture
	nfo.texture = ImGui::GetIO().Fonts->TexRef;
	nfo.size = { 100,50 };
	nfo.uv0 = { 0,0 };
	nfo.uv1 = { 1,1 };

	return image_status::ready;
};

// A rotating spinner, two lines high
void Renderer::draw_loading_spinner()
{
	float size = ImGui::GetFontSize() * 2.0f;
	ImVec2 cursor = ImGui::GetCursorScreenPos();
	ImVec2 center(cursor.x + size * 0.5f, cursor.y + size * 0.5f);
	float radius = size * 0.4f;
	float thickness = 2.0f;
	float t = (float)ImGui::GetTime();

	ImDrawList* dl = ImGui::GetWindowDrawList();
	int segments = 12;
	for (int i = 0; i < segments; i++)
	{
		float a = (float)i / (float)segments * 3.14159265358979f * 2.0f;
		// Fade based on rotation phase
		float fade = std::fmod((float)i / (float)segments + t * 1.5f, 1.0f);
		ImU32 c = ImGui::GetColorU32(ImGuiCol_Text, fade * 0.8f);
		float inner = radius * 0.5f;
		ImVec2 p1(center.x + std::cos(a) * inner, center.y + std::sin(a) * inner);
		ImVec2 p2(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius);
		dl->AddLine(p1, p2, c, thickness);
	}
	ImGui::Dummy(ImVec2(size, size));
}

bool Renderer::tap_opens_url() const
{
	return false;
}

void Renderer::open_url() const
{
	//Example:

#if 0
	if (!m_is_image) {
		SDL_OpenURL(m_href.c_str());
	} else {
		//image clicked
	}
#endif
}

////////////////////////////////////////////////////////////////////////////////
// Selectable text: the runs of text drawn by the fragment, and the selection over them

// Records the item just drawn: its rectangle, its font, and the bytes of the fragment's text it shows. A run that does
// not come from the text (a soft or hard line break) takes the byte after the run before.
void Renderer::record_run(const char* str, const char* str_end, const std::string& replacement)
{
	if (replacement.empty()) {  // a newline alone (between raw HTML tags) shows nothing
		bool newlines = true;
		for (const char* s = str; s < str_end && newlines; ++s)
			newlines = *s == '\n' || *s == '\r';
		if (newlines)
			return;
	}
	TextRun run;
	run.min = ImGui::GetItemRectMin();
	run.max = ImGui::GetItemRectMax();
	run.font = ImGui::GetFont();
	run.fontSize = ImGui::GetFontSize();
	run.block = m_block_number;
	run.href = m_href;
	run.replacement = replacement;
	if (str != nullptr && str >= m_fragment_begin && str < str_end && str_end <= m_fragment_end) {
		run.begin = (size_t)(str - m_fragment_begin);
		run.end = (size_t)(str_end - m_fragment_begin);
	} else {
		run.begin = m_runs.empty() ? 0 : m_runs.back().end;
		run.end = run.begin + 1;
		if (run.replacement.empty() && str != nullptr)
			run.replacement.assign(str, str_end);
	}
	if (m_hlevel > 0 && m_heading_top < 0.f)  // the first text of a heading: its top
		m_heading_top = run.min.y - ImGui::GetWindowPos().y + ImGui::GetScrollY();
	m_runs.push_back(std::move(run));
}

// Content that moved after it was drawn (an aligned table cell, <center>): its runs move with it
void Renderer::shift_runs(size_t first, float dx)
{
	if (dx == 0.0f)
		return;
	for (size_t i = first; i < m_runs.size(); ++i) {
		m_runs[i].min.x += dx;
		m_runs[i].max.x += dx;
	}
}

// The character boundary nearest to a position: in the run under it, else on its line, else in the first run below
size_t Renderer::offset_at(ImVec2 pos) const
{
	if (m_runs.empty())
		return 0;
	auto offsetInRun = [&](const TextRun& run) -> size_t {
		if (!run.replacement.empty())  // a formula or a soft break is selected whole
			return pos.x < (run.min.x + run.max.x) * 0.5f ? run.begin : run.end;
		const char* textEnd = m_fragment_begin + run.end;
		float x = run.min.x;
		for (const char* s = m_fragment_begin + run.begin; s < textEnd;) {
			unsigned int c;
			int n = ImTextCharFromUtf8(&c, s, textEnd);
			float w = run.font->CalcTextSizeA(run.fontSize, FLT_MAX, 0.0f, s, s + n).x;
			if (pos.x < x + w * 0.5f)
				return (size_t)(s - m_fragment_begin);
			x += w;
			s += n;
		}
		return run.end;
	};
	const TextRun* left = nullptr;   // the runs of the line, left and right of the position
	const TextRun* right = nullptr;
	for (const TextRun& run : m_runs) {
		if (pos.y < run.min.y || pos.y >= run.max.y)
			continue;
		if (pos.x >= run.min.x && pos.x < run.max.x)
			return offsetInRun(run);
		if (run.max.x <= pos.x && (!left || run.max.x > left->max.x))
			left = &run;
		if (run.min.x > pos.x && (!right || run.min.x < right->min.x))
			right = &run;
	}
	if (left)
		return left->end;
	if (right)
		return right->begin;
	for (const TextRun& run : m_runs)
		if (run.min.y > pos.y)
			return run.begin;
	return m_runs.back().end;
}

// The run showing the byte at an offset (or ending there)
const Renderer::TextRun* Renderer::run_at(size_t offset) const
{
	const TextRun* ending = nullptr;
	for (const TextRun& run : m_runs) {
		if (offset >= run.begin && offset < run.end)
			return &run;
		if (offset == run.end)
			ending = &run;
	}
	return ending;
}

// A click at an offset: one click places the selection there, two select the word, three the block
void Renderer::select_at(ImGuiID fragmentId, size_t offset, int clicks)
{
	m_selection_fragment = fragmentId;
	m_selection_text_hash = ImHashStr(m_fragment_begin, (size_t)(m_fragment_end - m_fragment_begin));
	m_selection_anchor = m_selection_focus = offset;
	m_selection_by_unit = clicks >= 2;
	const TextRun* run = run_at(offset);
	if (clicks < 2 || run == nullptr)
		return;
	size_t b = run->begin, e = run->end;
	if (clicks >= 3) {
		for (const TextRun& other : m_runs)
			if (other.block == run->block) {
				b = ImMin(b, other.begin);
				e = ImMax(e, other.end);
			}
	} else if (run->replacement.empty()) {  // a word, within the run (a formula is selected whole)
		auto inWord = [](char c) { return (unsigned char)c >= 0x80 || isalnum((unsigned char)c) || c == '_'; };
		b = e = offset;
		while (b > run->begin && inWord(m_fragment_begin[b - 1]))
			--b;
		while (e < run->end && inWord(m_fragment_begin[e]))
			++e;
	}
	m_selection_anchor = b;
	m_selection_focus = e;
}

void Renderer::select_range(ImGuiID fragmentId, size_t b, size_t e)
{
	m_selection_fragment = fragmentId;
	m_selection_text_hash = ImHashStr(m_fragment_begin, (size_t)(m_fragment_end - m_fragment_begin));
	m_selection_anchor = b;
	m_selection_focus = e;
	m_selection_by_unit = false;
}

void Renderer::select_all(ImGuiID fragmentId)
{
	m_selection_fragment = fragmentId;
	m_selection_text_hash = ImHashStr(m_fragment_begin, (size_t)(m_fragment_end - m_fragment_begin));
	m_selection_anchor = 0;
	m_selection_focus = (size_t)(m_fragment_end - m_fragment_begin);
}

// The markdown of the selection, extended to whole lines: list, heading and quote markers come along, and the markup
// of a line (bold, links, formulas) stays balanced
std::string Renderer::selected_markdown() const
{
	size_t b = ImMin(m_selection_anchor, m_selection_focus);
	size_t e = ImMax(m_selection_anchor, m_selection_focus);
	size_t size = (size_t)(m_fragment_end - m_fragment_begin);
	e = ImMin(e, size);
	while (b > 0 && m_fragment_begin[b - 1] != '\n')
		--b;
	if (e > b && m_fragment_begin[e - 1] != '\n')
		while (e < size && m_fragment_begin[e] != '\n')
			++e;
	return std::string(m_fragment_begin + b, m_fragment_begin + e);
}

// The selected parts of the runs; between two runs of a block, a space where the text between them is blank (a
// wrapped line) and nothing where it is markup (**, ](url)); a newline between two blocks
void Renderer::visible_text(size_t b, size_t e, std::string& out,
                            std::vector<std::pair<size_t, size_t>>* sourceOffsets) const
{
	auto separator = [&](const char* text, size_t at) {
		out += text;
		if (sourceOffsets)
			sourceOffsets->emplace_back(at, at);
	};
	const TextRun* previous = nullptr;
	for (const TextRun& run : m_runs) {
		if (run.end <= b || run.begin >= e)
			continue;
		size_t from = ImMax(b, run.begin), to = ImMin(e, run.end);
		std::string piece = !run.replacement.empty()
			? run.replacement
			: std::string(m_fragment_begin + from, m_fragment_begin + to);
		if (previous != nullptr) {
			if (run.block != previous->block)
				separator("\n", previous->end);
			else if (run.begin > previous->end && !out.empty() && out.back() != ' ' && out.back() != '\n'
			         && !piece.empty() && piece[0] != ' ') {
				bool blank = true;
				for (size_t i = previous->end; i < run.begin && blank; ++i)
					blank = m_fragment_begin[i] == ' ' || m_fragment_begin[i] == '\t' || m_fragment_begin[i] == '\n';
				if (blank)
					separator(" ", previous->end);
			}
		}
		out += piece;
		if (sourceOffsets) {
			if (!run.replacement.empty())
				sourceOffsets->insert(sourceOffsets->end(), piece.size(), std::make_pair(run.begin, run.end));
			else
				for (size_t i = from; i < to; ++i)
					sourceOffsets->emplace_back(i, i + 1);
		}
		previous = &run;
	}
}

std::string Renderer::selected_text() const
{
	std::string out;
	visible_text(ImMin(m_selection_anchor, m_selection_focus), ImMax(m_selection_anchor, m_selection_focus), out);
	return out;
}

// A click on a line of text starts a selection (and takes the active id, so that the window does not move), a drag
// extends it, a double click selects a word and a triple click a block, a click away from the text clears it. Ctrl+C
// (Cmd+C on macOS) copies it, Ctrl+A selects the whole fragment, a right click opens a menu.
// A press inside the selection keeps it while held (a long press on a phone opens the menu on it): a drag starts a
// new selection from the press, a release collapses it to the press; a release at no position is a cancel (a
// platform took the press away), the selection stays.
void Renderer::update_selection(ImGuiID fragmentId)
{
	ImGuiContext& g = *GImGui;
	ImVec2 mouse = ImGui::GetMousePos();
	const TextRun* hovered = nullptr;
	bool onTextLine = false;
	for (const TextRun& run : m_runs)
		if (mouse.y >= run.min.y && mouse.y < run.max.y) {
			onTextLine = true;
			if (mouse.x >= run.min.x && mouse.x < run.max.x) {
				hovered = &run;
				break;
			}
		}
	bool mouseFree = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && g.ActiveId == 0;
	if (hovered && mouseFree && hovered->href.empty())
		ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
	if (mouseFree && ImGui::IsMouseClicked(0)) {
		if (onTextLine) {
			ImGui::SetActiveID(fragmentId, g.CurrentWindow);
			ImGui::FocusWindow(g.CurrentWindow);
			size_t offset = offset_at(mouse);
			m_press_in_selection = m_selection_fragment == fragmentId && ImGui::GetMouseClickedCount(0) == 1
			                       && offset >= ImMin(m_selection_anchor, m_selection_focus)
			                       && offset < ImMax(m_selection_anchor, m_selection_focus);
			if (!m_press_in_selection)
				select_at(fragmentId, offset, ImGui::GetMouseClickedCount(0));
		} else if (m_selection_fragment == fragmentId) {
			m_selection_fragment = 0;
		}
	}
	if (g.ActiveId == fragmentId) {
		ImGui::KeepAliveID(fragmentId);
		if (!ImGui::IsMouseDown(0)) {
			if (m_press_in_selection && ImGui::IsMousePosValid())
				select_at(fragmentId, offset_at(g.IO.MouseClickedPos[0]), 1);
			m_press_in_selection = false;
			ImGui::ClearActiveID();
		} else if (m_press_in_selection) {
			if (ImGui::IsMouseDragPastThreshold(0)) {
				m_press_in_selection = false;
				select_at(fragmentId, offset_at(g.IO.MouseClickedPos[0]), 1);
				m_selection_focus = offset_at(mouse);
			}
		} else if ((!m_selection_by_unit || ImGui::IsMouseDragPastThreshold(0))  // a word or block waits for a drag
		           && ImGui::IsMousePosValid())  // no position (a platform between two events): the focus stays
			m_selection_focus = offset_at(mouse);
	}
	// The menu acts on the fragment it opens on (the selection of another fragment is dropped)
	if (mouseFree && onTextLine && ImGui::IsMouseReleased(1)) {
		m_menu_link = hovered ? hovered->href : "";
		if (m_selection_fragment != fragmentId)
			select_at(fragmentId, offset_at(mouse), 1);
		ImGui::OpenPopup("##rich_md_selection_menu");
	}
	if (m_selection_fragment == fragmentId && ImGui::IsWindowFocused() && !g.IO.WantTextInput) {
		if (m_selection_anchor != m_selection_focus
			&& ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C, ImGuiInputFlags_None))
			ImGui::SetClipboardText(selected_text().c_str());
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A, ImGuiInputFlags_None))
			select_all(fragmentId);
	}
	show_selection_menu(fragmentId);
}

// The menu of a right click: Copy, Copy as Markdown, Select All, Copy Link over a link, Fold All and Unfold All
void Renderer::show_selection_menu(ImGuiID fragmentId)
{
	if (!ImGui::BeginPopup("##rich_md_selection_menu"))
		return;
	// ImGui's names of shortcuts always say Ctrl: on macOS the key is Cmd
	const std::string modifier = ImGui::GetIO().ConfigMacOSXBehaviors ? "Cmd+" : "Ctrl+";
	bool hasSelection = m_selection_fragment == fragmentId && m_selection_anchor != m_selection_focus;
	if (!m_menu_link.empty()) {
		if (ImGui::MenuItem("Copy Link"))
			ImGui::SetClipboardText(m_menu_link.c_str());
		ImGui::Separator();
	}
	if (ImGui::MenuItem("Copy", (modifier + "C").c_str(), false, hasSelection))
		ImGui::SetClipboardText(selected_text().c_str());
	if (ImGui::MenuItem("Copy as Markdown", nullptr, false, hasSelection))
		ImGui::SetClipboardText(selected_markdown().c_str());
	ImGui::Separator();
	if (ImGui::MenuItem("Select All", (modifier + "A").c_str()))
		select_all(fragmentId);
	// The folds of the document, or of this render
	bool foldable = document != nullptr && foldableHeadings;
	for (ImGuiID id : m_heading_folds)
		foldable = foldable || id != 0;
	if (foldable) {
		ImGui::Separator();
		if (ImGui::MenuItem("Fold All"))
			fold_all(true);
		if (ImGui::MenuItem("Unfold All"))
			fold_all(false);
	}
	ImGui::EndPopup();
}

// The highlight of the selected parts of the runs
void Renderer::range_rects(size_t b, size_t e, std::vector<ImRect>& out) const
{
	for (const TextRun& run : m_runs) {
		if (run.end <= b || run.begin >= e)
			continue;
		float x0 = run.min.x, x1 = run.max.x;
		if (run.replacement.empty()) {  // a run with a replacement (a formula) is whole
			const char* text = m_fragment_begin + run.begin;
			if (b > run.begin)
				x0 += run.font->CalcTextSizeA(run.fontSize, FLT_MAX, 0.0f, text, m_fragment_begin + b).x;
			if (e < run.end)
				x1 = run.min.x + run.font->CalcTextSizeA(run.fontSize, FLT_MAX, 0.0f, text, m_fragment_begin + e).x;
		}
		out.emplace_back(ImVec2(x0, run.min.y), ImVec2(x1, run.max.y));
	}
}

void Renderer::draw_selection() const
{
	std::vector<ImRect> rects;
	range_rects(ImMin(m_selection_anchor, m_selection_focus), ImMax(m_selection_anchor, m_selection_focus), rects);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImU32 color = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
	for (const ImRect& r : rects)
		drawList->AddRectFilled(r.Min, r.Max, color);
}

// The context of a match, for the lists of matches: a few words on each side, on its line, "..." where it is cut
static constexpr size_t kContextBytes = 40;
static std::string context_before(const std::string& text, size_t at)
{
	size_t start = at > kContextBytes ? at - kContextBytes : 0;
	for (size_t i = start; i < at; ++i)
		if (text[i] == '\n')
			start = i + 1;
	bool cut = start > 0 && text[start - 1] != '\n';
	while (start < at && ((unsigned char)text[start] & 0xC0) == 0x80)
		++start;
	if (cut) {
		size_t space = text.find(' ', start);
		if (space != std::string::npos && space < at)
			start = space + 1;
	}
	return (cut ? "..." : "") + text.substr(start, at - start);
}
static std::string context_after(const std::string& text, size_t at)
{
	size_t end = ImMin(text.size(), at + kContextBytes);
	size_t newline = text.find('\n', at);
	if (newline != std::string::npos && newline < end)
		end = newline;
	bool cut = end < text.size() && text[end] != '\n';
	while (end > at && end < text.size() && ((unsigned char)text[end] & 0xC0) == 0x80)
		--end;
	if (cut) {
		size_t space = text.rfind(' ', end);
		if (space != std::string::npos && space > at)
			end = space;
	}
	return text.substr(at, end - at) + (cut ? "..." : "");
}

void Renderer::search_fragment(int fragment)
{
	DocumentRenders& d = *document;
	std::string text;
	std::vector<std::pair<size_t, size_t>> sources;
	visible_text(0, (size_t)-1, text, &sources);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const float toContent = ImGui::GetScrollY() - ImGui::GetWindowPos().y;  // a screen y to the content's coordinates
	const ImU32 matchColor = ImGui::GetColorU32(style.searchMatch);
	const ImU32 currentColor = ImGui::GetColorU32(style.searchMatchCurrent);
	std::vector<ImRect> rects;
	std::vector<DocumentMatch> matches;
	for (const TextMatch& found : FindMatches(text, d.query, d.searchOptions)) {
		DocumentMatch match;
		match.fragment = fragment;
		match.begin = sources[found.begin].first;
		match.end = sources[found.end - 1].second;
		if (!matches.empty() && matches.back().begin == match.begin && matches.back().end == match.end)
			continue;  // a formula is one match, highlighted whole, however many times its source has the query
		rects.clear();
		range_rects(match.begin, match.end, rects);
		if (rects.empty())
			continue;  // only the blank between two runs
		match.y = rects.front().Min.y + toContent;
		match.bottom = rects.back().Max.y + toContent;
		match.before = context_before(text, found.begin);
		match.text = text.substr(found.begin, found.end - found.begin);
		match.after = context_after(text, found.end);
		bool current = fragment == d.currentFragment && match.begin == d.currentBegin;
		if (current || d.highlightAll)
			for (const ImRect& r : rects)
				if (ImGui::IsRectVisible(r.Min, r.Max))
					drawList->AddRectFilled(r.Min, r.Max, current ? currentColor : matchColor);
		matches.push_back(std::move(match));
	}
	for (const HiddenText& hidden : m_hidden_texts)
		for (const TextMatch& found : FindMatches(hidden.text, d.query, d.searchOptions)) {
			DocumentMatch match;
			match.fragment = fragment;
			match.begin = hidden.sources[found.begin];
			match.end = hidden.sources[found.end - 1] + 1;
			match.y = hidden.y;
			match.bottom = hidden.bottom;
			match.before = context_before(hidden.text, found.begin);
			match.text = hidden.text.substr(found.begin, found.end - found.begin);
			match.after = context_after(hidden.text, found.end);
			match.details = hidden.details;
			match.inRuns = false;
			matches.push_back(std::move(match));
		}
	matches.insert(matches.end(), m_code_block_matches.begin(), m_code_block_matches.end());
	std::stable_sort(matches.begin(), matches.end(),
	                 [](const DocumentMatch& a, const DocumentMatch& b) { return a.begin < b.begin; });
	d.matches.insert(d.matches.end(), std::make_move_iterator(matches.begin()), std::make_move_iterator(matches.end()));
}

// The text of a collapsed section, gathered for the search: a space between two chunks where the source between them is
// blank (a line break), nothing where it is markup
void Renderer::add_hidden_text(const char* str, const char* str_end)
{
	if (str < m_fragment_begin || str_end > m_fragment_end || str >= str_end)
		return;  // not from the fragment's text
	if (m_hidden_texts.empty() || m_hidden_texts.back().details != m_hidden_id) {
		HiddenText hidden;
		hidden.details = m_hidden_id;
		hidden.y = m_hidden_y;
		hidden.bottom = m_hidden_bottom;
		m_hidden_texts.push_back(std::move(hidden));
	}
	HiddenText& hidden = m_hidden_texts.back();
	const size_t begin = (size_t)(str - m_fragment_begin), end = (size_t)(str_end - m_fragment_begin);
	if (!hidden.sources.empty()) {
		const size_t previous = hidden.sources.back() + 1;
		bool blank = begin > previous;
		for (size_t i = previous; i < begin && blank; ++i)
			blank = is_blank(m_fragment_begin[i]);
		if (blank) {
			hidden.text += ' ';
			hidden.sources.push_back(previous);
		}
	}
	hidden.text.append(str, str_end);
	for (size_t i = begin; i < end; ++i)
		hidden.sources.push_back(i);
}

// The offset in the fragment's text of a byte of the code block
size_t Renderer::code_block_source(size_t offset) const
{
	size_t source = 0;
	for (const auto& [code, fragment] : m_code_block_sources)
		if (code <= offset)
			source = fragment + (offset - code);
	return source;
}

std::vector<CodeBlockMatch> Renderer::code_block_matches(const std::string& code) const
{
	std::vector<CodeBlockMatch> matches;
	if (!document || document->query.empty())
		return matches;
	const int fragment = document->fragmentCount;  // the rank of this fragment, counted at its end
	for (const TextMatch& found : FindMatches(code, document->query, document->searchOptions)) {
		CodeBlockMatch match;
		match.begin = found.begin;
		match.end = found.end;
		bool current = fragment == document->currentFragment && code_block_source(found.begin) == document->currentBegin;
		match.current = current;
		if (current || document->highlightAll)
			match.color = ImGui::GetColorU32(current ? style.searchMatchCurrent : style.searchMatch);
		matches.push_back(match);
	}
	return matches;
}

void Renderer::add_code_block_matches(const std::string& code, const std::vector<CodeBlockMatch>& matches,
                                      const std::vector<ImRect>& rects, float blockTop)
{
	const float toContent = ImGui::GetScrollY() - ImGui::GetWindowPos().y;
	for (size_t i = 0; i < matches.size(); ++i) {
		DocumentMatch match;
		match.fragment = document->fragmentCount;
		match.begin = code_block_source(matches[i].begin);
		match.end = code_block_source(matches[i].end - 1) + 1;
		const bool known = i < rects.size();
		match.y = (known ? rects[i].Min.y : blockTop) + toContent;
		match.bottom = (known ? rects[i].Max.y : blockTop + ImGui::GetTextLineHeight()) + toContent;
		match.before = context_before(code, matches[i].begin);
		match.text = code.substr(matches[i].begin, matches[i].end - matches[i].begin);
		match.after = context_after(code, matches[i].end);
		match.inRuns = false;
		m_code_block_matches.push_back(std::move(match));
	}
}

void Renderer::soft_break()
{
    // Convert a soft break (e.g. a new line inside a paragraph into a space), except at the start of a line
    if (!line_is_open())
        return;
    ImGui::TextUnformatted(" ");
    record_run(nullptr, nullptr, " ");
    ImGui::SameLine(0.0f, 0.0f);
}

} // namespace RichMd
