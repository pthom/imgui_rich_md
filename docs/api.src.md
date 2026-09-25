# imgui_rich_md: API

The public API is in [rich_md.h](../imgui_rich_md/rich_md.h), with
[narrative_programming.h](../imgui_rich_md/narrative_programming.h) (the resolver of transclusions, without ImGui) and
[rich_md_host.h](../imgui_rich_md/rich_md_host.h) (for the hosts that provide their own textures, assets or downloads).
In Python, the same API is in snake_case:
[rich_md.pyi](https://github.com/pthom/imgui_bundle/blob/main/bindings/imgui_bundle/rich_md.pyi) in Dear ImGui Bundle.

This page is assembled from the headers by [narrative programming](narrative_programming/narrative_programming.md):
its source is [api.src.md](api.src.md).

## Rendering
![[../imgui_rich_md/rich_md.h#Rendering]]
![[../imgui_rich_md/rich_md.h#Rendering#code]]

## Contexts
![[../imgui_rich_md/rich_md.h#Contexts]]
![[../imgui_rich_md/rich_md.h#Contexts#code]]

## Options and callbacks
![[../imgui_rich_md/rich_md.h#Options and callbacks]]
![[../imgui_rich_md/rich_md.h#Options and callbacks#code]]

## Narrative programming
![[../imgui_rich_md/narrative_programming.h#Narrative programming]]
![[../imgui_rich_md/narrative_programming.h#Narrative programming#code]]

![[../imgui_rich_md/rich_md.h#Rendering files]]
![[../imgui_rich_md/rich_md.h#Rendering files#code]]

## Extensions
![[../imgui_rich_md/rich_md.h#Extensions]]
![[../imgui_rich_md/rich_md.h#Extensions#code]]

## Style and fonts
![[../imgui_rich_md/rich_md.h#Style and fonts]]
![[../imgui_rich_md/rich_md.h#Style and fonts#code]]

## Capabilities
![[../imgui_rich_md/rich_md.h#Capabilities]]
![[../imgui_rich_md/rich_md.h#Capabilities#code]]

## Host services
![[../imgui_rich_md/rich_md_host.h#Host services]]

### Types and defaults
![[../imgui_rich_md/rich_md_host.h#Types and defaults]]
![[../imgui_rich_md/rich_md_host.h#Types and defaults#code]]

### Downloads
![[../imgui_rich_md/rich_md_host.h#Downloads]]
![[../imgui_rich_md/rich_md_host.h#Downloads#code]]

### The services
![[../imgui_rich_md/rich_md_host.h#The services]]
![[../imgui_rich_md/rich_md_host.h#The services#code]]

## Backends

### LaTeX
![[../imgui_rich_md/backends/latex/rich_md_latex.h#LaTeX backend]]

#### TeX style
![[../imgui_rich_md/backends/latex/rich_md_latex.h#TeX style]]
![[../imgui_rich_md/backends/latex/rich_md_latex.h#TeX style#code]]

#### Initialization and shutdown
![[../imgui_rich_md/backends/latex/rich_md_latex.h#Initialization and shutdown]]
![[../imgui_rich_md/backends/latex/rich_md_latex.h#Initialization and shutdown#code]]

#### Rendering
![[../imgui_rich_md/backends/latex/rich_md_latex.h#Rendering]]
![[../imgui_rich_md/backends/latex/rich_md_latex.h#Rendering#code]]

### Code snippets
![[../imgui_rich_md/backends/code_editor/snippets.h#Code snippets]]

#### Snippet data
![[../imgui_rich_md/backends/code_editor/snippets.h#Snippet data]]
![[../imgui_rich_md/backends/code_editor/snippets.h#Snippet data#code]]

#### Showing snippets
![[../imgui_rich_md/backends/code_editor/snippets.h#Showing snippets]]
![[../imgui_rich_md/backends/code_editor/snippets.h#Showing snippets#code]]

## Legacy names
![[../imgui_rich_md/rich_md.h#Legacy names]]
![[../imgui_rich_md/rich_md.h#Legacy names#code]]
