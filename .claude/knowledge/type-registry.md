---
node: type-registry
title: Type Registry (C++ type → NodeType)
type: component
tags: [types, compile-time, registry]
updated: 2026-07-01
---

# Type Registry

Compile-time mapping from a C++ payload type to its on-disk `NodeType` tag.

## Facts

- `struct/type_registry.h` (+ `.cpp`): primary template `node_type_of<T>` is intentionally **undefined** → a clear compile error for unsupported types (`type_registry.h:22-23`).
- Specializations: `int`→INT, `float`→FLOAT, `double`→DOUBLE, `char`→CHAR, `bool`→BOOL, and `ComplexRecord`→COMPLEX (`type_registry.h:25-34`).
- `node_type_of_v<T>` is the value alias used by `insert`/`write_node` for `if constexpr` dispatch.
- `node_record_payload_size(NodeType)` (in `.cpp`) returns the on-disk payload size; for COMPLEX it returns only `sizeof(ComplexHeader)` — the two variable strings are NOT counted (`type_registry.h:41-51`).
- Adding a type = new `NodeType` enumerator + a `node_type_of` specialization.

## Relations

- **part-of** → [[pod-layout]]
- **used-by** → [[persistence-io]]
- **relates-to** → [[complex-nodes]]
- **documented-in** → docs/modules/graph_core.md

## Sources

- `graph_core/struct/type_registry.h`, `graph_core/struct/type_registry.cpp`
