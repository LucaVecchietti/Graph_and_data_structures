#pragma once
#include <cstdint>
#include <string>

constexpr uint8_t RELATION_TYPE_MAX_SIZE = 255;

// ---- Fixed-width relation-batch layout (NodeRelationList) ----
// A relation list is stored as one or more fixed-size BATCHES. Each batch is a
// NodeRelationList POD header followed by a fixed RELATION_BATCH_TAIL-byte tail
// of up to RELATION_LINES_PER_BATCH fixed-width lines. Fixed width lets a single
// relation line be updated in place (O(1) seek-write) and makes every batch the
// same on-disk size, so the relation freelist needs only ONE size class/bin.
//
// Line layout (RELATION_LINE_SIZE bytes):
//   [uint64_t edge_offset][uint64_t edge_count][uint8_t name_length][char name[RELATION_NAME_MAX]]
constexpr uint8_t  RELATION_NAME_MAX        = 255;                 // fixed name field width per line
constexpr uint16_t RELATION_LINE_SIZE       = 8 + 8 + 1 + RELATION_NAME_MAX; // = 272
constexpr uint8_t  RELATION_LINES_PER_BATCH = 8;                  // max relation lines per batch
constexpr uint16_t RELATION_BATCH_TAIL      = RELATION_LINE_SIZE * RELATION_LINES_PER_BATCH; // = 2176

// Fixed width (in digits) of the zero-padded prog_number prefix in a COMPLEX
// node's sidecar filename (e.g. "00000000000000000005_Athlete.json"). 20 covers
// the full uint64 range, so a COMPLEX record's on-disk size depends ONLY on its
// type_label length — making the exact-size freelist bins behave as per-type
// size classes (a freed slot is reusable by any same-type / same-length record).
constexpr uint8_t COMPLEX_PROG_DIGITS = 20;

constexpr std::string_view DB_PATH = "../db";
constexpr std::string_view META_FILE_PATH = "../db/meta.dat";

constexpr std::string_view JSON_ATTR_META_PATH = "../db/attributes/attributes_meta.dat";
constexpr std::string_view JSON_ATTR_PATH = "../db/attributes/";