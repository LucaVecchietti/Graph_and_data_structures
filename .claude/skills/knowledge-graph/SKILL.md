---
name: knowledge-graph
description: Use this skill to build and continuously maintain the project knowledge graph under .claude/knowledge/ — a network of linked .md "nodes" rooted at Index.md. Activate whenever you learn or confirm something about the project (architecture, modules, on-disk format, decisions, workflows, invariants, bugs), after a refactor, when the user says "aggiorna il knowledge graph", "traccia nel grafo", "mappa la conoscenza", "aggiungi al grafo", or invokes /knowledge-graph, and when the Stop hook flags that project changes were made without updating the graph.
---

# knowledge-graph

You are the keeper of the **pointer_graphs knowledge graph**: a living map of everything you know about this project, stored under `.claude/knowledge/` as a set of interlinked Markdown nodes rooted at `Index.md`.

The point is that *you* — Claude — can reload the full mental model of the project cheaply at the start of any session by reading `Index.md` and walking its `[[wikilinks]]`, instead of re-deriving it from the code every time.

## Relationship to `docs/`

Keep these two separate and complementary:

- **`docs/`** is the human-facing canonical documentation (owned by the `docs-keeper` skill). It has a formal standard, metadata tables, legacy logs.
- **`.claude/knowledge/`** is *your* internal map — dense, associative, optimized for fast recall and traversal, not for external readers.

Do **not** duplicate `docs/` prose into the graph. Instead, a knowledge node **links out** to the relevant doc as a source (`documented-in → docs/modules/graph_core.md`). The graph captures *relationships and facts*; `docs/` captures *the full write-up*.

## When to activate

Update the graph proactively, in the same turn you learn the thing:

1. **Explicit request** — "aggiorna/aggiungi al knowledge graph", "traccia nel grafo", "mappa la conoscenza", or `/knowledge-graph`.
2. **New knowledge discovered** — you understood a module, an invariant, a data-flow, a gotcha, a design decision, a bug (BUG-NNN), or a workflow.
3. **After a refactor** — a change altered something already represented in the graph (a node is now stale).
4. **Stop-hook nudge** — the Stop hook reports uncommitted project changes with no corresponding graph update; review and sync the affected nodes, or stop if the change was trivial.

When unsure whether something belongs in the graph, prefer adding a small node over losing the knowledge — but keep one fact per node (see below).

## Location & layout

```
.claude/knowledge/
├── Index.md              # ROOT node — entry point, cluster map, node registry
├── <slug>.md             # one node per concept/module/decision/workflow/...
└── ...                   # flat directory; wikilinks resolve by slug, no subfolders
```

- **Flat directory.** Every node is a top-level `.md` file so `[[slug]]` resolves unambiguously to `<slug>.md`.
- **Slug = filename without `.md` = wikilink target.** `[[graph-core]]` → `graph-core.md`. Slugs are `kebab-case`, unique, stable (renaming a slug means updating every inbound link).
- The root node is always `[[Index]]` (`Index.md`), capitalized.

## Node file format

Every node file MUST follow this template:

```markdown
---
node: <kebab-slug>            # unique id, equals the filename
title: <Human Readable Title>
type: root | domain | component | concept | decision | workflow | bug | glossary
tags: [tag1, tag2]
updated: YYYY-MM-DD           # absolute date, last time this node was touched
---

# <Title>

<1–3 sentence summary: what this node is, in plain terms.>

## Facts

- Concrete, verifiable statements. Cite code with `path/to/file:line` where useful.
- Keep to the ONE subject of this node. If it grows a second subject, split it.

## Relations

- **part-of** → [[parent-node]]
- **depends-on** → [[other-node]]
- **relates-to** → [[sibling-node]]
- **documented-in** → docs/modules/xxx.md

## Sources

- `graph_core/...` — code that backs these facts
- docs/... — canonical write-up
```

Sections may be omitted when empty (a pure hub node may have only `## Relations`). `## Relations` is effectively mandatory — a node with no relations is an **orphan** (forbidden, see invariants).

## Relation vocabulary (typed, directed edges)

Use this **closed set** of relation verbs, mirroring the project's own typed-relation graph model. Each has an inverse that MUST exist on the target node (bidirectional invariant):

| Relation        | Inverse (on target) | Meaning                                        |
|-----------------|---------------------|------------------------------------------------|
| `part-of`       | `contains`          | node is a sub-part of a larger cluster         |
| `depends-on`    | `used-by`           | node needs the target to function              |
| `implements`    | `implemented-by`    | node realizes a concept/interface              |
| `documents`     | `documented-in`     | link between a node and its `docs/` write-up   |
| `supersedes`    | `superseded-by`     | node replaces an older decision/design         |
| `example-of`    | `has-example`       | node is a concrete instance of a general one   |
| `relates-to`    | `relates-to`        | symmetric, weak association (use sparingly)    |

If you need a relation not in this set, add it to this table **first**, with its inverse, then use it — never invent one-off verbs inline.

## Root node (`Index.md`) rules

`Index.md` is the graph's entry point and registry. It MUST let you reach every node by walking links. Keep in it:

1. **Purpose** — one line on what this graph is.
2. **How to read it** — one line: start here, follow `[[wikilinks]]`.
3. **Clusters** — the top-level `domain` nodes, each linked, grouping the rest.
4. **Node registry** — a flat list of every node with a one-line hook, so nothing is unreachable.

Every new node MUST be reachable from `Index.md` — either linked directly, or transitively through a `domain` node that Index links.

## Operating procedure

When invoked (or when you learn something worth capturing):

1. **Read `Index.md`** and any nodes adjacent to the topic. Don't work blind — you may be updating, not creating.
2. **Decide create vs update vs split vs merge:**
   - *Create* a new node for a genuinely new subject.
   - *Update* an existing node when the fact belongs to its subject; bump `updated:`.
   - *Split* when a node has grown two distinct subjects.
   - *Merge* two nodes that turned out to be the same subject; leave no dangling links.
3. **Verify against the code** — do not record from memory; cite `path:line` in `## Sources`.
4. **Write both directions** — every relation you add gets its inverse on the target node. This is the load-bearing invariant of the whole graph.
5. **Register in `Index.md`** — add new nodes to the registry and, if it's a cluster, to the cluster map. Bump Index's `updated:`.
6. **Set `updated:`** to today's absolute date on every node you touch.

## Integrity invariants

Enforce these on every edit; if you notice a violation, fix it:

- **Root reachability** — every node is reachable from `[[Index]]`. No islands.
- **No orphans** — every node has at least one relation.
- **No broken links** — every `[[slug]]` points to an existing `<slug>.md`. If you link a node that doesn't exist yet, create at least a stub (title + summary + one relation) in the same turn.
- **Bidirectional edges** — every relation has its inverse on the target.
- **Unique, stable slugs** — no two nodes share a slug; renaming updates all inbound links.
- **Absolute dates** — `updated:` is `YYYY-MM-DD`, never relative.
- **One subject per node** — if it needs "and" in the title, it's probably two nodes.

## What to capture vs. skip

**Capture:** architecture and layering, module responsibilities, on-disk/POD format facts and invariants, data-flow, design decisions and their rationale, known bugs (BUG-NNN), workflows (build/run/verify), and load-bearing gotchas (e.g. the intentional misspellings `neighborgs`, `data_tructures`, `costants`).

**Skip:** transient conversation state, one-off command output, anything already fully and stably captured in `docs/` (link to it instead), and speculation. Facts only — no "this is well designed" commentary.

## Continuous update (the Stop hook)

A `Stop` hook (`.claude/hooks/kg_sync_reminder.py`, registered in `.claude/settings.local.json`) runs when you finish a turn. It is loop-safe (honors `stop_hook_active`) and only fires when there are **uncommitted changes outside `.claude/knowledge/`** while the graph itself was **not** touched this session. When it fires, it asks you to sync the affected nodes — do so if the change introduced or altered project knowledge, otherwise stop. This is what keeps the graph "continuously updated" without you having to remember on every turn.
