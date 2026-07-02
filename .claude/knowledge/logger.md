---
node: logger
title: Logger
type: component
tags: [logging, diagnostics]
updated: 2026-07-01
---

# Logger

Header-only logger (`logger.h`) used across the project for debug/info/warn/error output.

## Facts

- `enum class LogLevel { DEBUG, INFO, WARN, ERR }`; a `min_level` filters entries below threshold (`logger.h:8-27`).
- Each entry is `timestamp [LEVEL] msg\n`, written to BOTH the log file and `std::cerr`, flushed per line (`logger.h:29-37`).
- `Graph` constructs `Logger("graph.log", LogLevel::DEBUG)` (`graph.h:34`); the I/O translation unit logs to `graph_io.log`.
- Timestamp is local time `%Y-%m-%d %H:%M:%S` (`logger.h:51-57`).

## Relations

- **part-of** → [[persistence-io]]
- **relates-to** → [[graph-core]]

## Sources

- `graph_core/logger.h`, `graph.log`, `graph_io.log`
