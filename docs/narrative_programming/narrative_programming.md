# Narrative Programming

> **A notebook is a document containing a program.  
> A narrative program is a program containing a document.**

A **narrative program** is an ordinary source file that contains its own narrative.

The file remains a normal program: it can be run from the command line, edited in an IDE, type-checked, tested, versioned and refactored as usual.

But selected comments or strings can contain Markdown, and parts of the source can be included in that Markdown.

This makes it possible to present the same file as an **explorable document**, combining:

- explanations;
- mathematical notation;
- selected source excerpts;
- images, plots and other program output;
- interactive controls and visualizations.

The source remains organized as a program. The rendered document can organize the same material as a story.

## A first example

A documented function can keep its explanation immediately beside its implementation:

```python
r"""::md Escape
### Escape time

Iterate $z \leftarrow z^2 + c$ and count the steps until
$|z| > 2$, after which $z$ escapes to infinity.

::code
"""
def escape_time(z: np.ndarray, c: np.ndarray, max_iter: int = 80) -> np.ndarray:
    ...
# ::endcode
```

`::md Escape` defines a named Markdown section.

`::code` associates the following source code with that section. Code boundaries are explicit: blank lines, indentation and language syntax do not affect them.

`::endcode` ends the code, and the section with it.

The result is one documented source section called `Escape`, containing some prose and its associated implementation.

## Prose does not have to include code

A Markdown section contained entirely in a string or block comment ends naturally with that container:

```python
r"""::md Intro
# The Mandelbrot set is a map of Julia sets

Click on the Mandelbrot set to choose $c$.
The corresponding Julia set is shown alongside it.
"""
```

No `::endmd` is necessary.

Source code following the string is not implicitly captured:

```python
r"""::md Intro
Some explanation.
"""

do_something()
```

`do_something()` is ordinary source code and is not part of `Intro`.

Crossing from prose into source is always explicit through `::code`.

## Building a narrative

Named sections can be included elsewhere using Obsidian-style transclusion syntax:

```python
r"""::md Overview
## What you are looking at

The escape procedure is at the heart of the algorithm:

![[#Escape]]

And here is its implementation:

![[#Escape#code]]
"""
```

Here:

```text
![[#Escape]]
```

includes the Markdown part of `Escape`, while:

```text
![[#Escape#code]]
```

includes its associated source code.

The source code is automatically rendered as code using the language of the source file. There is no need to add a Markdown code fence.

This separation allows documentation to remain close to the implementation while the rendered explanation presents it wherever it makes most sense.

## Named code regions

Smaller pieces of source can also be named independently:

```python
# ::code Iteration
for i in range(max_iter):
    z = z * z + c
    ...
# ::endcode Iteration
```

They can then be included directly:

```text
![[#Iteration]]
```

Because `Iteration` names a code region, it is rendered as source code automatically.

Code regions may be nested. A nested region remains part of its enclosing code excerpt while also being independently addressable.

For example:

```python
r"""::md Escape
### Escape time
::code
"""
def escape_time(...):
    # ::code Iteration
    for i in range(max_iter):
        ...
    # ::endcode Iteration

    return count
# ::endcode
```

Both of these are valid:

```text
![[#Escape#code]]
![[#Iteration]]
```

The first includes the complete implementation associated with `Escape`; the second includes only the inner iteration.

Annotation lines themselves are omitted from rendered code, and extracted code is dedented: `Iteration`, indented inside the function, is rendered flush left.

## Including material from other files

Transclusion is not limited to the current source file.

A relative path can be specified before the section name:

```text
![[../src/fractals.py#Escape]]
![[../src/fractals.py#Escape#code]]
![[../src/fractals.py#Iteration]]
```

Paths are relative to the file containing the reference.

The same syntax works from Markdown embedded in source files and from standalone Markdown documents:

```markdown
# Understanding escape-time fractals

The basic idea is:

![[../src/fractals.py#Escape]]

Its implementation is:

![[../src/fractals.py#Escape#code]]

The central iteration is particularly simple:

![[../src/fractals.py#Iteration]]
```

This syntax is inspired by Obsidian transclusions such as `![[note#heading]]`.

`#code` is an extension for narrative source files: it selects the source code associated with a documented section.

Whole files can be included too, and a Markdown document can be included by heading, as in Obsidian:

```text
![[notes.md]]            a whole Markdown document
![[notes.md#Setup]]      the part of it under the heading "Setup"
![[fractals.py]]         a whole source file, as code
```

## Source order and narrative order

An important consequence is that **source order and narrative order are independent**.

A program might naturally define:

```text
escape_time()
mandelbrot_image()
julia_image()
GUI
```

while its explanation might want to present:

```text
Introduction
→ Mandelbrot map
→ Julia sets
→ escape algorithm
→ implementation details
```

There is no need to reorganize the program to accommodate the explanation.

For example:

```python
r"""::md Story
## What you are looking at

![[#Escape]]
![[#Mandelbrot]]
![[#Julia]]

## Why the boundary matters

A Julia set is connected exactly when its parameter $c$
belongs to the Mandelbrot set.
"""
```

The source can remain organized **as a program**, while the document is organized **as a narrative**.

## Syntax at a glance

Source annotations:

```text
::md Name
::code
::code Name
::endcode
::endcode Name
::endmd
::endmd Name
```

Transclusions:

```text
![[#Escape]]                       Markdown section
![[#Escape#code]]                  Its associated code
![[#Iteration]]                    Named code region

![[other.py#Escape]]               Markdown from another file
![[other.py#Escape#code]]          Its associated code
![[other.py#Iteration]]            Named code from another file

![[other.py]]                      A whole source file, as code
![[notes.md#Setup]]                A Markdown document's part under a heading
```

Names may contain spaces:

```text
::code Main iteration
...
::endcode Main iteration

![[#Main iteration]]
```

Names on closing annotations are optional. When present, they must match the region being closed.

## Why not a notebook?

A notebook starts with a document and embeds executable code inside it. Its document and cell structure are the primary artifact.

A narrative program goes in the opposite direction.

The **program remains the primary artifact**. Documentation annotations describe and compose views of its source; they do not divide the program into execution cells or change the semantics of the host language.

This is particularly useful for examples, scientific applications and educational programs that should remain ordinary source files while also explaining themselves.

The rendered document can itself be interactive: explanations can appear beside plots, images, simulations and controls implemented by the program.

In the future, an embedded source editor could make this relationship bidirectional: selected regions could be edited and re-executed directly from the explorable document while the underlying artifact remains an ordinary source file.

## Why now: programs written with AI

As AI writes more of the code, code may look like assembly to most people: it works, and nobody reads it.

What becomes scarce is not writing code but **understanding** it. A narrative program keeps that understanding in the program:

- **Checkable explanations.** The prose sits beside the real code it describes (`![[#Escape#code]]`), not a paraphrase that can drift.
- **Explorable behavior.** When the code is unreadable, change a parameter and watch the result.
- **A shared contract.** Humans own the story; AI works inside the sections and keeps prose and code in step.
- **Lasting intent.** The reasons stay in the file, for the next person or the next AI.

The risk is narratives that nobody reads, or that are fluent but wrong. The defense: prose tied to real code and real behavior.

Narrative programming may matter more, not less.
