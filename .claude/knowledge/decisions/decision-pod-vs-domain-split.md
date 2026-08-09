---
id: decision-pod-vs-domain-split
title: Keep POD and domain structs separate
type: decision
tags: [architecture]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Keep POD and domain structs separate
> Two parallel struct hierarchies - one tuned for RAM, one for disk - with an explicit translation layer between them.

## Context

The graph has to exist in two shapes at once: alive in RAM (pointers, hash maps, fast
non-linear access) and boxed on disk (fixed layout, known byte for byte). A single
representation would have to pay in one of the two worlds - pointers are meaningless on disk,
and packed structs with offsets slow down RAM access.

## Options considered

- **One serializable struct for both** - would force offsets/handles in RAM, or `pack(1)` on
  the domain model.
- **A serialization library** (FlatBuffers, Cap'n Proto) - rejected to avoid external
  dependencies in an exploratory project.
- **Two parallel hierarchies with an explicit translator** - chosen.

## Decision

Keep two families. The domain family (`BaseNode`, `Node<T>`, `EdgeRef`) is tuned for RAM; the
POD family (`NodeRecord<T>`, `NodeIndex`, `NodeRelationList`, `Edge`, `MetaRecord`) is tuned
for disk with `#pragma pack(push,1)` and fixed-width `uint64_t` fields. A dedicated layer,
`odt/`, is the only code allowed to translate between them.

## Consequences

- More code: an explicit ODT layer that would not otherwise exist.
- Most new features touch three files - domain, POD, ODT.
- Any POD change breaks the on-disk format, permanently and without a migration path, since
  the format carries no version field.

## Links

- specifies [[graph-core-architecture]] - this is why there are four layers
- specifies [[pod-layout]] - the disk-side hierarchy it created
- relates to [[odt-layer]] - the translator this decision requires
