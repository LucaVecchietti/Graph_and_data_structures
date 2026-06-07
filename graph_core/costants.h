#pragma once
#include <cstdint>
#include <string>

constexpr uint8_t RELATION_TYPE_MAX_SIZE = 255;

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