# MyOS-Simple Wiki

A local, in-repository documentation wiki for **MyOS-Simple**, the five-stage bare-metal
x86 OS tutorial. Start at **[Home.md](Home.md)** for the full catalog and reading paths.

Every page is verified against the source in this repository and cites it as
`path:line`. There are 31 articles in four sections plus this index.


## Layout

```
wiki/
├── Home.md                 Landing page + complete catalog
├── _Sidebar.md             Navigation (rendered as the sidebar on GitHub Wikis)
├── README.md               This file
├── concepts/               One idea per page — the theory behind the stages (15)
├── stages/                 Guided walkthrough of each of the five stages (5)
├── reference/              Dense lookups: memory map, ports, GDT bits, scancodes,
│                           commands, toolchain, glossary (7)
└── guides/                 How-to: build, debug, troubleshoot, extend (4)
```


## Reading paths

- **The tour.** [Stage 1](stages/stage-1-assembly-boot.md) →
  [2](stages/stage-2-c-protected-mode.md) → [3](stages/stage-3-interactive-shell.md) →
  [4](stages/stage-4-clock-processes-calc.md) → [5](stages/stage-5-release.md),
  following links into the [concepts](concepts/) as questions arise.
- **The deep dive.** Read the [concepts](concepts/) in catalog order from
  [Home.md](Home.md#concepts).
- **The cheat sheet.** Keep [reference/](reference/) open while you hack.


## Using it as a GitHub Wiki

The files use GitHub-Wiki conventions: `Home.md` is the landing page and `_Sidebar.md`
renders as the navigation sidebar. To publish, push the contents of this folder to the
repository's `*.wiki.git`. The links are relative, so the same files also browse cleanly
right here in the main repository.


## Conventions

- `# Title` + a one-line italic tagline open every page.
- Knowledge nuggets appear as `> 💡 **Tidbit:**` and limitations as `> ⚠️ **Caveat:**`.
- Code is quoted from the real source with `path:line` citations.
- Each page ends with a cross-linked **See also** section.
