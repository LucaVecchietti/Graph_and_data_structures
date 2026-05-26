# Documentation Standard

> Source of truth for templates, sections, naming, and conventions used across `docs/`.

| Campo | Valore |
|---|---|
| Tipo | index |
| Lingua | en |
| Ultimo aggiornamento | 2026-05-26 |
| Commit di riferimento | 9e8b589 |
| Mirror | — |

---

## 1. Common header (top of every doc)

Every `.md` under `docs/` starts with this block. The title is a level-1 heading; the one-line description follows; then the metadata table; then a horizontal rule.

```markdown
# {Title}

> {One-line description of what this document contains}

| Campo | Valore |
|---|---|
| Tipo | module \| architecture \| legacy-decisions \| legacy-api \| legacy-bugs \| snapshot \| index |
| Lingua | en \| it |
| Ultimo aggiornamento | YYYY-MM-DD |
| Commit di riferimento | {short hash} \| N/A |
| Mirror | docs/it/.../X.md \| docs/.../X.md \| — |

---
```

Rules:
- `Ultimo aggiornamento`: **absolute date**, never relative.
- `Commit di riferimento`: short SHA of the commit that the doc reflects. `N/A` if not applicable.
- `Mirror`: path to the parallel-language version, or `—` if no mirror exists.

## 2. Sections per document type

### 2.1 `module` — files in `docs/modules/`

```
1. Overview                       — one or two sentences on the module's role
2. Struttura                      — annotated tree of files/subfolders in the module
3. Design                         — design choices, with links to legacy/design_decisions.md
4. Tipi e strutture dati          — every struct/enum/typedef: purpose, fields, invariants
5. Funzioni / interfacce esposte  — signature, params, return, side effects
6. Diagrammi                      — ASCII diagrams of internal relations / data flow
7. Dipendenze                     — IN (who uses me) / OUT (what I use)
8. Voci legacy collegate          — links to legacy/* entries
9. Riferimenti                    — file:line pointers into the source
```

### 2.2 `architecture` — files in `docs/architecture/`

```
1. Overview
2. Diagramma                      — top-level ASCII diagram
3. Componenti                     — short description + link to docs/modules/X.md
4. Flussi di dati                 — how data moves between components
5. Dipendenze cross-modulo
6. Voci legacy collegate
```

### 2.3 `legacy-*` — aggregator files in `docs/legacy/`

```
1. Indice                         — list of entries with anchors
2. Entries                        — most recent first, each with the structure in §3
```

### 2.4 `snapshot` — files in `docs/legacy/snapshots/`

```
1. Contesto                       — why this snapshot was taken
2. Stato del codice al {date}     — extract or description
3. Cosa è cambiato dopo           — pointer to the api_changes / design_decisions entry that supersedes it
```

### 2.5 `index` — `docs/README.md`

```
1. Mappa della documentazione     — links organized by folder
2. Convenzioni                    — pointer to docs/STANDARD.md
```

## 3. Internal structure of legacy log entries

### 3.1 Entry in `design_decisions.md`

```markdown
### YYYY-MM-DD — {Title}

- **Stato:** active | superseded by [link] | deprecated
- **Contesto:** {what problem prompted the decision}
- **Decisione:** {what was decided}
- **Alternative considerate:** {short list + why discarded}
- **Conseguenze:** {trade-offs, constraints imposed}
- **Riferimenti:** {file:line, related modules}
```

### 3.2 Entry in `api_changes.md`

```markdown
### YYYY-MM-DD — {Symbol changed}

- **Motivazione:** {why}
- **Before:**
  ```c
  {previous signature or behavior}
  ```
- **After:**
  ```c
  {current signature or behavior}
  ```
- **Note di migrazione:** {what callers must do}
- **Riferimenti:** {commit hash, file:line}
```

### 3.3 Entry in `known_bugs.md`

```markdown
### YYYY-MM-DD — BUG-NNN: {title}

- **Stato:** open | fixed | wontfix
- **Sintomo:** {observable behavior}
- **Root cause:** {actual cause}
- **Fix:** {what was done, file:line} | "n/a (open)"
- **Regression guard:** {test/check that prevents recurrence, or "none"}
```

## 4. Naming conventions

| Path | Pattern |
|---|---|
| Module doc | `docs/modules/{snake_case}.md` |
| Architecture doc | `docs/architecture/{topic}.md` |
| Legacy aggregator | `docs/legacy/{category}.md` |
| Snapshot | `docs/legacy/snapshots/{YYYY-MM-DD}_{topic}.md` |
| Italian mirror | same path under `docs/it/...` |

Bug IDs: `BUG-NNN`, sequential, three digits, never reused.

## 5. Diagrams

ASCII only. Box-drawing characters allowed (`─ │ ┌ ┐ └ ┘ ├ ┤ ┬ ┴ ┼`). Keep them compact and aligned in a monospace font.

## 6. Cross-linking

- From a module doc to a design decision: link by markdown anchor — `[see decision](../legacy/design_decisions.md#YYYY-MM-DD--title-slug)`.
- From a legacy entry back to the relevant module(s): always include the path under "Riferimenti".

## 7. Language

- Primary documents: **English**.
- Italian mirrors live under `docs/it/...`, same structure. Created **only on explicit request**.
- The metadata `Mirror` field on each side points to the other.
