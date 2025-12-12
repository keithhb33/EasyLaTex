# EasyLaTex — Write LaTeX Without the Noise (Indent-Based → LaTeX)

EasyLaTex is a tiny compiler (written in C) that turns an indentation-based language (`.itex`) into standard `.tex` that compiles with `pdflatex`.

It’s designed for people who **want real LaTeX power** but don’t want to fight LaTeX’s most annoying ergonomics:
- `\begin{...}` / `\end{...}` everywhere
- boilerplate preambles in every file
- “math mode vs equation mode” syntax weirdness
- clutter that makes drafts harder to read and edit

EasyLaTex keeps LaTeX as the target and as the “escape hatch”, but gives you a cleaner source format.

---

## What EasyLaTex is

**EasyLaTex is an indentation-based front-end for LaTeX**: you write structured `.itex`, EasyLaTex emits plain `.tex`.

---

## EasyLaTex vs Normal LaTeX (Side-by-Side)

### 1) Environments: `begin/end` vs Indentation

**Normal LaTeX**
```tex
\begin{center}
This is centered.
\end{center}
```

**EasyLaTex**
```text
center:
    This is centered.
```

The structure is *visually obvious* in EasyLaTex, and you never have to hunt for matching `\end{...}`.

---

### 2) Sections: `\section{...}` vs `section: ...`

**Normal LaTeX**
```tex
\section{Introduction}
This is the intro.
```

**EasyLaTex**
```text
section: Introduction
This is the intro.
```

Also supports newline style:
```text
section:
    Introduction
```

---

### 3) “No-body commands”: commands that should not be environments

Some LaTeX commands are *not* environments and do *not* have a body:
- `\tableofcontents`
- `\maketitle`
- `\newpage`

**Normal LaTeX**
```tex
\tableofcontents
\maketitle
\newpage
```

**EasyLaTex**
```text
tableofcontents:
maketitle:
newpage:
```

No accidental `\begin{tableofcontents}` / `\end{tableofcontents}` nonsense.

---

### 4) Document metadata: title/author/date without boilerplate

**Normal LaTeX**
```tex
\documentclass{article}
\usepackage{amsmath}
...
\title{My Paper}
\author{Me}
\date{December 12, 2025}

\begin{document}
\maketitle
...
\end{document}
```

**EasyLaTex**
```text
title: My Paper
author: Me
date: December 12, 2025

maketitle:
```

EasyLaTex automatically emits the preamble + `\begin{document}` / `\end{document}`.

---

### 5) Math blocks: LaTeX display math vs EasyLaTex `math:`

LaTeX math is powerful but has multiple “modes” and environment rules.

**Normal LaTeX (one common style)**
```tex
\[
\begin{aligned}
a &= b + c \\
d &= e + f
\end{aligned}
\]
```

**EasyLaTex**
```text
math:
    a &= b + c
    d &= e + f
```

If you want literal raw math lines (no DSL processing), you can mark it:
```text
math:
    latex:
        \int_a^b f(x)\,dx = F(b)-F(a)
        a^2 + b^2 = c^2
```

Inside `math:`, EasyLaTex treats each line as a row and joins them with `\\` in LaTeX.

---

### 6) Raw LaTeX escape hatch

Sometimes you want full LaTeX control—custom environments, weird packages, bibliography blocks, TikZ, anything.

**EasyLaTex**
```text
latex:
    \newpage
    \begin{thebibliography}{9}
    \bibitem{trefethen} Trefethen, \textit{ATAP}, SIAM (2013).
    \end{thebibliography}
```

That block is output **exactly as-is**.

So you never lose power: EasyLaTex is “clean by default” but “full LaTeX when needed”.

---

## Why indentation is better than begin/end

In real documents (especially math papers), LaTeX environments nest heavily:

- theorem → proof → aligned → cases → itemize
- figures and tables and captions and references
- long documents where you scroll a lot

With `\begin{...}` / `\end{...}`, it becomes easy to:
- mismatched environment types
- lose track of nesting visually
- end up with messy diffs in git

Indentation makes structure **immediately obvious**, like Python code.

---

## Core Concepts

### File type
- Source: `*.itex`
- Output: `*.tex`

### Blocks are opened by headers
A “header” looks like:
```text
name:
```
or:
```text
name: inline content
```

EasyLaTex only treats `name:` as special if `name` is **actually known** (a whitelisted LaTeX command/environment, or one of EasyLaTex’s special blocks).

That prevents this from breaking:
```text
The narrative of this paper is:
```
This remains normal text unless \"The\" is a supported LaTeX header (it isn’t).

---

## EasyLaTex Language Overview

### 1) Environments (indentation-based)
```text
center:
    text
```
→ `\begin{center} ... \end{center}`

### 2) Section-like commands (take a title)
```text
section: Hello
subsection:
    Background
```
→ `\section{Hello}` and `\subsection{Background}`

### 3) Braced commands (metadata + many `\cmd{...}`-style commands)
```text
title: My Paper
author:
    First Last
    University
date: December 12, 2025
```
→ `\title{...}` `\author{...}` `\date{...}` with multi-line bodies joined by `\\`

### 4) No-body commands (emit `\cmd` only)
```text
tableofcontents:
maketitle:
newpage:
```

### 5) `math:` blocks (aligned display math)
```text
math:
    a &= b + c
    d &= e + f
```

Features:
- Each line becomes a new aligned row.
- Literal `\n` inside a math line splits rows.
- Blank lines can become extra vertical spacing (implementation-dependent, but commonly `\\[0.6em]`).

### 6) `latex:` blocks (raw passthrough)
```text
latex:
    \newpage
    \textbf{This is raw LaTeX}
```

### 7) `python:` blocks (execute code, inject stdout)
```text
python:
    print("hello")
    print(2+2)
```

Default behavior: stdout wrapped in verbatim.

Raw mode:
```text
python[results=tex]:
    print(r"\textbf{Generated by Python}")
```

---

## Full Example Document

**input.itex**
```text
title: Chebyshev Polynomials — A Short Note
author: Keith (Draft)
date: December 12, 2025

maketitle:
tableofcontents:
newpage:

section: Introduction
Chebyshev polynomials arise from the identity below.

math:
    latex:
        T_n(\cos\theta) = \cos(n\theta)

section: A Simple List
itemize:
    - Minimizes worst-case error (minimax)
    - Excellent interpolation nodes
    - FFT-like fast transforms

section: Raw LaTeX Escape Hatch
latex:
    \newpage
    \begin{thebibliography}{9}
    \bibitem{trefethen} Trefethen, \textit{Approximation Theory and Approximation Practice}, SIAM (2013).
    \end{thebibliography}
```

Compile:
```bash
./easylatex input.itex > output.tex
pdflatex output.tex
```

---

## Build & Run

### Build EasyLaTex
```bash
gcc -O2 -Wall -Wextra -std=c11 easylatex.c -o easylatex
```

### Compile `.itex` → `.tex`
```bash
./easylatex input.itex > output.tex
```

### Compile `.tex` → PDF (clean build dir recommended)
```bash
mkdir -p .easylatex_build
./easylatex input.itex > .easylatex_build/output.tex
pdflatex -output-directory .easylatex_build .easylatex_build/output.tex
```

This keeps `output.aux`, `output.out`, etc. inside `.easylatex_build/`.

## `build.sh` (One-command build: `.itex` → `.tex` → `.pdf`)

This repo includes a convenience script, `build.sh`, that:

1. **Compiles** the EasyLaTex compiler (`easylatex.c`) into a local binary (`./easylatex`)
2. **Converts** your `.itex` input into a `.tex` file inside a build folder
3. **Runs `pdflatex` twice** (standard practice for TOC/refs) to produce a PDF
4. **Deletes sidecar LaTeX build artifacts** (`.aux`, `.out`, `.log`, `.toc`, `.synctex.gz`) so the build folder stays clean

### Usage

```bash
chmod +x build.sh
./build.sh [C_FILE] [IN_FILE] [OUT_BASENAME]
```

All arguments are optional:

- `C_FILE` (default: `easylatex.c`)
- `IN_FILE` (default: `input.itex`)
- `OUT_BASENAME` (default: `output`)

### Examples

Build using defaults:

```bash
./build.sh
```

Build a specific `.itex` file:

```bash
./build.sh easylatex.c mypaper.itex paper
```

### Output location

All outputs go into:

```text
.itex_build/
```

After a successful run you’ll have:

- `.itex_build/output.tex`
- `.itex_build/output.pdf`

(or whatever basename you chose)

### Notes / Requirements

- Requires `gcc` (or compatible C compiler) and `pdflatex` (TeX Live / MacTeX).
- The script uses `-halt-on-error` and `set -euo pipefail`, so it stops immediately on errors.
- `pdflatex` output is redirected to `/dev/null` for cleanliness; remove `>/dev/null` if you want full logs during debugging.


---

## Philosophy: “Clean Source, Real LaTeX Output”

EasyLaTex is not trying to replace LaTeX.
It’s trying to make writing LaTeX feel less like writing markup.

- Write clean structure with indentation.
- Use math blocks and simple commands.
- Drop into raw LaTeX whenever needed.
- Always end up with normal `.tex` you can share, debug, and compile anywhere.

---
