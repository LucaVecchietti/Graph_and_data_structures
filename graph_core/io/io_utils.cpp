#include "io_utils.h"

// ---- I/O Utilities ----

// ---- POD I/O ----

/**
 * Writes a POD struct to an output stream in binary format.
 * The struct must be trivially copyable (POD) to ensure safe binary serialization.
 */
template <typename T>
void write_pod(const T &pod, std::ostream &out)
{
    std::static_assert(std::is_trivially_assignable_v<T>, "Template parameter\ T must be a POD type!");
    if (!out)
        throw std::invalid_argument("Output stream is not valid.");
    out.write(std::reinterpret_cast<const char *>(&pod), sizeof(T));
}

/**
 * Reads a POD struct from an input stream in binary format.
 * The struct must be trivially copyable (POD) to ensure safe binary deserialization
 */
template <typename T>
T read_pod(std::ifstream &in)
{
    std::static_assert(std::is_trivially_assignable_v<T>, "Template parameter\ T must be a POD type!");
    if (!in)
        throw std::invalid_argument("Input stream is not valid.");
    T pod;
    in.read(reinterpret_cast<char *>(&pod), sizeof(T));
    return pod;
}

// ---- Complex I/O ----
// ---- String I/O ----

/**
 * Write a string to an output stream with a preceding length byte.
 */
void write_string(const std::string &str, std::ostream &out)
{
    if (str.length() > std::numeric_limits<uint64_t>::max())
    {
        throw std::overflow_error("String length exceeds maximum storable size.");
    }
    if (!out)
    {
        throw std::invalid_argument("Output stream is not valid.");
    }
    uint64_t length = str.length();
    out.write(reinterpret_cast<const char *>(&length), sizeof(length)); // Write length prefix
    out.write(str.data(), length); // Write string data 
}

/**
 * Read a string from an input stream that is prefixed with its length.
 */
std::string read_string(std::ifstream &in)
{
    if (!in)
    {
        throw std::invalid_argument("Input stream is not valid.");
    }
    uint64_t length;
    in.read(reinterpret_cast<char *>(&length), sizeof(length));
    std::string str(length, '\0');
    in.read(&str[0], length);
    return str;
}

// ---- Offset I/O ----

/**
 * Writes a 64-bit offset to the output stream.
 */
void write_offset(const uint64_t offset, std::ostream &out);

/**
 * Reads a 64-bit offset from the input stream.
 */
uint64_t read_offset(std::ifstream &in);