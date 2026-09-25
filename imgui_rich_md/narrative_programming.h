// Part of imgui_rich_md - MIT License - Copyright (c) 2022-2026 Pascal Thomet - https://github.com/pthom/imgui_rich_md
#pragma once
// The resolver of narrative programming, without ImGui: it reads files through a callback. rich_md.h includes it.
#include <functional>
#include <optional>
#include <string>


namespace RichMd
{
    // =================================================================================================================
    //                                      Narrative programming
    // =================================================================================================================
    /*::md Narrative programming
    A program can carry its own narrative: markdown sections and code regions in its comments and strings, that
    markdown transcludes (`![[file.cpp#Name]]`). See the
    [presentation](https://github.com/pthom/imgui_rich_md/blob/main/docs/narrative_programming/narrative_programming.md)
    and the
    [specification](https://github.com/pthom/imgui_rich_md/blob/main/docs/narrative_programming/narrative_programming_spec.md).

    A source file (C, C++, GLSL, JavaScript, Python) names parts of itself with annotations:
    ```cpp
    // ::md Area                    a markdown section, in line comments (// or #)
    // The area of a *circle*.
    // ::code                      its associated code
    float Area(float r) { return 3.14159f * r * r; }
    // ::endcode                   closes the code and the section

    // ::md Intro                   a section without code is closed by ::endmd
    // # Circles
    // ::endmd

    // ::code Main loop             a named code region (regions may nest)
    for (int i = 0; i < n; ++i) {}
    // ::endcode
    ```
    A section may also be a block comment or a Python string (triple quotes) whose first line is `::md Name` after
    the opener: it ends with the comment or the string, or continues into the code after it when its last line is
    `::code`.

    A markdown text transcludes them with an embed alone on its line (Obsidian's syntax):
    ```text
    ![[circle.cpp#Area]]            the prose of a section
    ![[circle.cpp#Area#code]]       its associated code, as a code block
    ![[circle.cpp#Main loop]]       a code region
    ![[#Area]]                      a section of the current file: a source file can be its own narrative
    ![[circle.cpp]]                 a whole source file, as code
    ![[notes.md]]                   a markdown document, whole...
    ![[notes.md#Setup#Linux]]       ...or under a heading (Linux, under Setup)
    ```
    Paths are relative to the file holding the embed (a text without a file: the assets, then the file system).
    Transclusions resolve recursively. An error (a missing file or name, a malformed annotation, a cycle)
    renders the embed in the error color, with the reason as a tooltip.

    ::code
    */

    // Reads a text file for ResolveTransclusions, or returns std::nullopt when it does not exist
    using ReadTextFile = std::function<std::optional<std::string>(const std::string& path)>;

    // ResolveTransclusions replaces the embeds of a markdown text: readFile reads a file (or returns std::nullopt);
    // currentFile is the file the text comes from, if any. Render() calls it with the host's ReadAsset.
    std::string ResolveTransclusions(const std::string& markdown, const ReadTextFile& readFile,
                                     const std::string& currentFile = "");
    // ::endcode
}
