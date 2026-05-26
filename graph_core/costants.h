#pragma once
#include <cstdint>
#include <string>

constexpr uint8_t RELATION_TYPE_MAX_SIZE = 255;

constexpr std::string_view DB_PATH = "../db";
constexpr std::string_view META_FILE_PATH = "../db/meta.dat";

constexpr std::string_view JSON_ATTR_META_PATH = "../db/attributes/attributes_meta.dat";
constexpr std::string_view JSON_ATTR_PATH = "../db/attributes/";