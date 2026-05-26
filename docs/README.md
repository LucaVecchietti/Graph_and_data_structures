# pointer_graphs — Documentation Index

> Entry point for all project documentation. Maintained by the `docs-keeper` skill.

| Campo | Valore |
|---|---|
| Tipo | index |
| Lingua | en |
| Ultimo aggiornamento | 2026-05-26 |
| Commit di riferimento | 9e8b589 |
| Mirror | — |

---

## Mappa della documentazione

### Architettura
- [overview.md](architecture/overview.md) — top-level view of the system, components and data flow.

### Moduli
- [graph_core.md](modules/graph_core.md) — C++ core: `Graph`, struct/policy/io/odt layers.
- [data_structures.md](modules/data_structures.md) — C legacy: generic hash table (`map_hash_table`).
- [db.md](modules/db.md) — on-disk binary format (`nodes.dat`, `nodes.idx`, `edges.dat`, `meta.dat`).

### Legacy log
- [design_decisions.md](legacy/design_decisions.md) — architectural decisions and the reasons behind them.
- [api_changes.md](legacy/api_changes.md) — changes to function/struct signatures with before/after.
- [known_bugs.md](legacy/known_bugs.md) — recorded bugs (open, fixed, wontfix).
- [snapshots/](legacy/snapshots/) — pre-refactor code snapshots.

## Convenzioni

All documents follow [STANDARD.md](STANDARD.md). When updating or adding any doc, read the standard first.

The Italian mirror — when populated — lives under [`it/`](it/) with the same tree.
