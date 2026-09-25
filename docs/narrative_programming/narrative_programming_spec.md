# Narrative Programming: Source Annotation Specification

This document defines the syntax and semantics of source annotations and transclusions used by narrative programs.

## 1. Concepts

The annotation system defines two kinds of named objects in source files:

**Markdown sections (`md`)** contain Markdown prose and may have one associated anonymous code region.

**Code regions (`code`)** identify explicit ranges of source code. Code regions may be named and nested.

Annotations affect documentation extraction and presentation only. They do not alter the execution semantics of the host language.

Markdown documents can also be transcluded: whole, or the part under one of their headings (see [section 11](#11-transclusion)).

## 2. Annotation syntax

The annotation directives are:

```text
::md Name
::code [Name]
::endcode [Name]
::endmd [Name]
```

Annotations must occur within comments or supported string constructs of the host language.

Whitespace preceding or following a directive is determined by extraction from the host-language comment/string and is not part of the directive.

### 2.1 Where directives are recognized

A directive is the first token of its line, once the line's comment token (for line comments) or the container's opening delimiter (for the first line of a string or block comment) is removed. It is followed by a space and a name, or by the end of the line.

| Language | Line comments | Bounded containers |
|---|---|---|
| Python | `#` | strings: `"""`, `'''`, with or without a prefix such as `r` |
| C, C++, GLSL, JavaScript | `//` | block comments: `/* */` or `/** */` |

For example, `r"""::md Intro` opens a section on the first line of a string, and `# ::code` opens a code region in a line comment. A comment that starts with a C++ global-scope name (`// ::GetTickCount() ...`) is not a directive: `::GetTickCount()` is not a directive keyword.

The opening delimiter starts its line, after indentation: `r"""::md Intro` opens a section, `doc = """::md Intro` does not. `/**::md Intro` opens a section too; Doxygen or JSDoc then take it as the documentation of the declaration that follows (`/*` stays out of these tools).

The language of a file is given by its extension: `.py`, `.pyi` (Python); `.c`, `.h`, `.cpp`, `.cc`, `.cxx`, `.hpp`, `.hh` (C, C++); `.glsl`, `.vert`, `.frag` (GLSL); `.js`, `.ts` (JavaScript, TypeScript); `.md` (Markdown documents, see [section 11.5](#115-headings-of-markdown-documents)). A file of another kind has no annotations: it can only be transcluded whole.

## 3. Names

A name is the trimmed remainder of the annotation line following the directive:

```text
::md Escape time
::code Main iteration
```

Here the names are respectively `Escape time` and `Main iteration`.

Names are not host-language identifiers and may contain spaces.

A Markdown section must be named: an unnamed section could not be referenced. A code region may be unnamed only when it is the associated code of a Markdown section (see [section 5](#5-associated-code)).

Named `md` sections and named `code` regions share a namespace within a file. A name must identify at most one object.

Characters reserved by the transclusion grammar, in particular `#`, `|`, `[` and `]`, are not permitted in names.

Name matching is exact and case-sensitive.

## 4. Markdown sections

`::md Name` opens a Markdown section.

A Markdown section is closed:

- by the end of its container, when `::md` occurs inside a bounded host-language container such as a string or block comment, unless the section has been extended into source code by `::code`;
- by the `::endcode` of its associated code, when it has one (see [section 5](#5-associated-code));
- otherwise, by `::endmd`.

For example:

```python
r"""::md Intro
# Introduction

Some prose.
"""
```

is a complete Markdown section.

Source following the container is not part of that section.

For line-comment Markdown without associated code, where there is no enclosing multi-line container providing a boundary, `::endmd` is required:

```python
# ::md Intro
# # Introduction
#
# Some prose.
# ::endmd Intro
```

Blank lines may separate the comment lines of such a section. A line of source code inside it is an error (a missing `::endmd` or `::code`).

Inside the prose of a section, the only directive is the unnamed `::code` that opens its associated code.

## 5. Associated code

An unnamed `::code` within an open Markdown section opens the code region associated with that section:

```python
r"""::md Escape
Explanation.
::code
"""
def escape_time(...):
    ...
# ::endcode
```

An associated code region:

- belongs to the enclosing Markdown section;
- is unnamed independently;
- is addressable through the Markdown section's `#code` selector;
- must be explicitly terminated by `::endcode`, which also closes the Markdown section.

A Markdown section may have at most one associated anonymous code region.

An `::endmd` immediately following that `::endcode` is allowed and has no further effect (a redundant close, for authors who prefer symmetric markers). When it carries a name, the name must match the section.

`::code` written inside the section's container (the string or block comment) marks the crossing: the section continues into the source that follows the container.
The associated code starts on the line after the closing delimiter.

## 6. Named code regions

`::code Name` opens an independently addressable code region:

```python
# ::code Main iteration
for i in range(max_iter):
    ...
# ::endcode Main iteration
```

Every code region has an explicit `::endcode`.

Named code regions may occur independently or inside another code region.

## 7. Nesting

Code regions may be nested.

`::endcode` closes the innermost open code region.

For example:

```python
# ::code Outer
before()

# ::code Inner
inside()
# ::endcode Inner

after()
# ::endcode Outer
```

Extracting `Outer` produces conceptually:

```python
before()

inside()

after()
```

Extracting `Inner` produces:

```python
inside()
```

Annotation lines are excluded from extracted source, but source contained in nested regions remains part of enclosing regions.

## 8. Closing names

Closing directives may optionally repeat the name of the object being closed:

```text
::endcode Main iteration
::endmd Escape
```

The name has no effect on nesting.

A closing directive always closes the innermost currently open object of the corresponding kind.

If a closing name is specified, it must match the name of that object. A mismatch is an error.

An anonymous code region must use an unnamed `::endcode`.

## 9. Extraction

### 9.1 Code

Extracting a code region:

1. takes the source lines between its opening and closing directives;
2. removes the lines that hold only an annotation directive (including those of nested regions);
3. removes the blank lines at its start and at its end (blank lines inside the region are kept);
4. removes the longest common leading whitespace of its non-blank lines, compared character by character (tabs and spaces are never mixed up).

A region nested inside a function is therefore extracted flush left, with its relative indentation kept:

```python
def escape_time(z, c, max_iter):
    # ::code Iteration
    for i in range(max_iter):
        z = z * z + c
    # ::endcode Iteration
```

`![[#Iteration]]` renders:

```python
for i in range(max_iter):
    z = z * z + c
```

### 9.2 Markdown

Extracting a Markdown section:

- **in a string or block comment**: takes the lines after the line holding `::md` (up to the end of the container, or up to `::code`), then removes their longest common leading whitespace, as for code. Markdown in an indented string, such as a docstring, is extracted flush left.
- **in line comments**: removes the comment token (`#`, `//`) from each line, then the longest common leading whitespace of the remaining text. `# # Title` becomes `# Title`, and the relative indentation of nested lists or indented blocks is kept.

In both cases, blank lines at the start and at the end are removed.

## 10. Code rendering

Transcluded code regions are rendered as code automatically.

The author does not surround a code transclusion with Markdown fences:

```text
![[#Escape#code]]
```

rather than:

````text
```python
![[#Escape#code]]
```
````

The rendering language is inferred from the source file when possible.

For example:

```text
![[example.py#Iteration]]       → Python
![[algorithm.cpp#Iteration]]    → C++
![[shader.glsl#Lighting]]       → GLSL
```

If no language can be inferred, the region is rendered as untyped source code.

This requirement is semantic; an implementation need not literally construct Markdown backtick fences.

An implementation that does produce a fenced block makes its fence longer than any run of backticks starting a line of the code (a Python string holding Markdown fences, for example).

## 11. Transclusion

Markdown may transclude named objects using Obsidian-style embed syntax:

```text
![[path#target]]
```

`path` may be omitted to refer to the file containing the transclusion:

```text
![[#target]]
```

The target is resolved in the namespace of the referenced file.

A transclusion must be alone on its line. Inside a paragraph, `![[...]]` is not a transclusion.
Inside a fenced code block, it is not a transclusion either: a document can show the syntax.

### 11.1 Markdown section

If the target identifies an `md` section:

```text
![[#Escape]]
```

the Markdown content of that section is transcluded.

Its associated code is not included implicitly.

### 11.2 Associated code

The synthetic child selector `#code` selects the code associated with an `md` section:

```text
![[#Escape#code]]
```

or across files:

```text
![[../src/fractals.py#Escape#code]]
```

It is an error to use `#code` on a Markdown section that has no associated code.

### 11.3 Named code region

If the target identifies a named `code` region:

```text
![[#Main iteration]]
```

that source region is transcluded directly.

No `#code` selector is required.

### 11.4 Whole files

A transclusion without a target includes the whole file:

```text
![[notes.md]]           a Markdown document, as Markdown
![[fractals.py]]        a source file, as code
```

A source file included whole is rendered as code, as it is: its annotations are shown, and its lines keep their numbers.

### 11.5 Headings of Markdown documents

In a Markdown document, a target names a heading, as in Obsidian:

```text
![[notes.md#Setup]]
![[notes.md#Setup#Linux]]
![[#Setup]]
```

The part of the document under that heading is transcluded: the heading and what follows it, up to the next heading of the same or a higher level. A path of several headings (`#Setup#Linux`) selects a subheading under a heading.

Heading matching is exact and case-sensitive, on the heading's text.

Headings are ATX headings (`#` to `######`) outside fenced code blocks. An optional closing sequence (`## Setup ##`) is not part of the text.

A final `#code` step is always the associated-code selector, never a heading name: `![[notes.md#Setup#code]]` is an error (a Markdown document has no associated code), not a subheading named `code`.

## 12. Relative paths

A path in a transclusion is resolved relative to the file containing the transclusion:

```text
![[../src/fractals.py#Escape]]
```

Resolution must not depend on the process working directory.

The initial specification requires explicit filenames and extensions for cross-file references. Project-wide basename lookup and implicit extension resolution are outside the specification.

A text that comes from no file (a string given to a renderer) has no base: `![[#target]]` is an error in it, and where its relative paths are looked up is up to the implementation.

## 13. Transclusion semantics

Transclusion is a presentation operation.

Transcluding a source region:

- reads the referenced source;
- extracts the requested Markdown or code;
- renders that material;
- does not execute the referenced source;
- does not modify the referenced source.

Transcluded Markdown may itself contain transclusions.

Implementations must detect recursive transclusion cycles and report them as errors rather than recurse indefinitely.

When the result is Markdown text, the transcluded material replaces the transclusion line, separated from the surrounding lines by one blank line. A transclusion whose material is empty (an empty section) removes its line.

## 14. Error conditions

At minimum, implementations should diagnose:

- duplicate names within a source file;
- an unnamed `::md`;
- an unnamed `::code` outside a Markdown section;
- unmatched `::endcode`;
- unmatched `::endmd`;
- mismatched optional closing names;
- an unclosed explicit code region;
- more than one associated anonymous code region in an `md` section;
- a missing transclusion file;
- a missing transclusion target (a name, or a heading);
- `#code` applied to a section without associated code, or to a Markdown document;
- recursive transclusion cycles.
- a directive other than the unnamed `::code` inside the prose of a section;
- a string or block comment that starts with a directive other than `::md`;
- a line of source code inside the prose of a section written in line comments;
- a reserved character in a name;
- a target of several names in a source file (a source file has no headings).

A source file with an annotation error provides no object: every transclusion from it is an error.

Diagnostics should identify the source file and annotation or transclusion responsible for the error.

## 15. Summary grammar

Conceptually:

```text
md-open       ::= "::md" name
code-open     ::= "::code" [name]
code-close    ::= "::endcode" [name]
md-close      ::= "::endmd" [name]

local-ref     ::= "![[" "#" target [ "#code" ] "]]"
external-ref  ::= "![[" path [ "#" target [ "#code" ] ] "]]"
target        ::= name | heading { "#" heading }
```

`#code` is valid only when `target` resolves to an `md` section.

If `target` resolves directly to a named code region, the code region itself is rendered.

The exact lexical rules for paths and names should follow the restrictions defined above rather than the simplified grammar shown here.

## 16. Relation to the earlier syntax

This specification replaces the syntax used until 2026 by rich_md and Hello ImGui's documentation tools (`@@md#Name` ... `@@/md` markers, `@import "file" {md_id=Name}` directives). Implementations do not need to read it.

## 17. Conformance cases

The folder [tests/narrative/cases](../../tests/narrative/cases) of imgui_rich_md holds test cases. Each case is a folder with source files, a `doc.md` with transclusions, and `expected.md`, the result of resolving `doc.md`. Two implementations run them: imgui_rich_md's (C++) and the documentation tool of Hello ImGui (Python). In the expected results, errors use imgui_rich_md's format (`<md-error title="reason">`) and messages, which are not part of this specification.
