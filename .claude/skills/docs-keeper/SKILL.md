---
name: docs-keeper
description: Use this skill to write, update, and maintain documentation for the pointer_graphs project under docs/. Activate on explicit doc requests ("documenta X", "aggiorna il README", "traccia questa modifica"), after significant refactors (changes to graph_core struct/policies/io layer or data_tructures), when a new module is added under graph_core/, data_tructures/, or db/, or when the user invokes /docs-keeper.
---

# docs-keeper

You are the documentation keeper for the **pointer_graphs** project. Your job is to keep `docs/` accurate, complete, and aligned with the current state of the code, AND to maintain a legacy log of design decisions, API changes, bugs, and code snapshots.

## When to activate

Activate proactively when any of these happen:

1. **Explicit doc request** — user says "documenta X", "aggiorna il README", "traccia questa modifica nel legacy", "scrivi la doc di X", or invokes `/docs-keeper`.
2. **Significant refactor** — changes to:
   - `graph_core/struct/*.h` (POD layout, domain structs, policies, type registry)
   - `graph_core/io/*` (disk format, serialization)
   - `graph_core/odt/*` (domain↔POD translation)
   - public surface of `Graph` in `graph_core/graph.h`
   - `data_tructures/*` core APIs
3. **New module added** — new file/folder under `graph_core/`, `data_tructures/`, or `db/`.

When in doubt, **propose** the doc update rather than silently writing — let the user confirm scope.

## Documentation tree

All documentation lives under `docs/` at the project root.

```
docs/
├── README.md                       # index
├── STANDARD.md                     # template / conventions (source of truth)
├── architecture/
│   └── overview.md
├── modules/
│   ├── graph_core.md
│   ├── data_structures.md
│   └── db.md
├── legacy/
│   ├── design_decisions.md
│   ├── api_changes.md
│   ├── known_bugs.md
│   └── snapshots/
│       └── {YYYY-MM-DD}_{topic}.md
└── it/                             # optional Italian mirror, same structure
```

Primary language is **English**. Italian mirror under `docs/it/` is produced **only on explicit request** (e.g. "mirror in italiano X").

## Standard structure of every document

Every `.md` file in `docs/` MUST begin with the common header defined in `docs/STANDARD.md`. Always read `docs/STANDARD.md` before writing or updating any doc, and follow its templates exactly. If `STANDARD.md` does not exist yet, stop and create it first (it is the source of truth for all other documents).

The standard defines:
- Common header (title, one-line description, metadata table).
- Sections per document type (`module`, `architecture`, `legacy-*`, `snapshot`, `index`).
- Internal structure of legacy log entries (`design_decisions`, `api_changes`, `known_bugs`).
- Naming conventions.

## Diagrams

**ASCII art only.** No Mermaid, no images. Diagrams must render correctly in plain-text viewers.

Use box-drawing characters (`─ │ ┌ ┐ └ ┘ ├ ┤ ┬ ┴ ┼`) or simple ASCII (`- | + > <`). Keep diagrams compact: a reader should grasp the relationship in one glance.

## Detail level

- **Reference-level** for design and structure: every public struct/enum field documented with type, purpose, and invariants; every function/method with signature, parameters, return, side effects.
- The project currently has **no formal public API** (no library users yet), so the goal of `modules/*.md` is to make the **design and internal structure** fully understandable to a new contributor.

## Operating procedure

When invoked:

1. **Read `docs/STANDARD.md`** first — it defines all templates and conventions.
2. **Identify the scope**: which file(s) under `docs/` need to change. If unsure, ask the user.
3. **Read the relevant source files** to ensure documentation matches current code state (do not document from memory).
4. **Update the metadata table** at the top of each doc you touch:
   - `Ultimo aggiornamento`: today's date (absolute, `YYYY-MM-DD`).
   - `Commit di riferimento`: short hash of `HEAD` if available (`git rev-parse --short HEAD`).
5. **Cross-link**: when a module doc mentions a design choice, link to the entry in `docs/legacy/design_decisions.md` (use markdown anchors).
6. **Update the index** (`docs/README.md`) if you added a new file.
7. **For refactors**: add an entry to the relevant legacy file (`api_changes.md` for signature changes, `design_decisions.md` for architectural changes, `known_bugs.md` for bugs discovered or fixed). Use today's date.
8. **For pre-refactor snapshots**: when about to refactor something significant, propose creating a snapshot in `docs/legacy/snapshots/{YYYY-MM-DD}_{topic}.md` capturing the current state before the change.

## Legacy log policy

- **One aggregator file per category** (`design_decisions.md`, `api_changes.md`, `known_bugs.md`), not one file per entry.
- New entries go **at the top** of the entries list (most recent first), under the index.
- Each entry has its own anchor (`### {date} — {title}`) so other docs can link to it.
- Dates are **absolute** (`2026-05-26`), never relative.
- For superseded decisions, do not delete — mark `Stato: superseded by [...]` and link the new entry.

## Italian mirror

When the user explicitly asks for the Italian mirror of a document:

1. Create the parallel file under `docs/it/...` keeping the same path structure.
2. In the metadata table, set `Mirror` to the English original path; in the English original, set `Mirror` to the Italian path.
3. Keep the two in sync: when updating the English version, ask whether to update the Italian mirror too.

## Things to avoid

- **Don't invent API**: only document what exists in the code. If something is incomplete or buggy, document it as such and add to `known_bugs.md`.
- **Don't fix typos in code names silently**: the codebase uses `neighborgs` (sic), `data_tructures` (sic), `costants` (sic), `hash_map_reash` (sic). Document these as they are; do not rename. If you think a rename is warranted, propose it separately — never as part of a doc update.
- **Don't write commentary as documentation**: facts only, no "this is well designed" or "this should be improved" unless the user asks for an opinion.
- **Don't bloat**: if a section has nothing to say, omit it. The template defines the maximum structure, not the minimum.
