#pragma once

#include <fstream>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <type_traits>

// ---- I/O Utilities ----

// ---- POD I/O ----

/**
 * Writes a POD struct to an output stream in binary format.
 * The struct must be trivially copyable (POD) to ensure safe binary serialization.
 */
template <typename T>
void write_pod(const T &pod, std::ostream &out){
    std::static_assert(std::is_trivially_assignable_v<T>, "Template parameter\ T must be a POD type!");
    if (!out) throw std::invalid_argument("Output stream is not valid.");
    out.write(std::reinterpret_cast<const char*>(&pod), sizeof(T));
}

/**
 * Reads a POD struct from an input stream in binary format.
 * The struct must be trivially copyable (POD) to ensure safe binary deserialization
 */
template <typename T>
T read_pod(std::ifstream &in){
    std::static_assert(std::is_trivially_assignable_v<T>, "Template parameter\ T must be a POD type!");
    if (!in) throw std::invalid_argument("Input stream is not valid.");
    T pod;
    in.read(reinterpret_cast<char*>(&pod), sizeof(T));
    return pod;
}

// ---- Complex I/O ----
// ---- String I/O ----

/**
 * Write a string to an output stream with a preceding length byte.
 */
void write_string(const std::string &str, std::ostream &out);

/**
 * Read a string from an input stream that is prefixed with its length.
 */
std::string read_string(std::ifstream &in);

// ---- Offset I/O ----

/**
 * Writes a 64-bit offset to the output stream.
 */
void write_offset(const uint64_t offset, std::ostream &out);

/**
 * Reads a 64-bit offset from the input stream.
 */
uint64_t read_offset(std::ifstream &in);