#include "io_utils.h"
#include <limits>

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
    out.write(reinterpret_cast<const char *>(&length), sizeof(length));
    out.write(str.data(), length);
}

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

void write_offset(const uint64_t offset, std::ostream &out)
{
    if (!out)
    {
        throw std::invalid_argument("Output stream is not valid.");
    }
    out.write(reinterpret_cast<const char *>(&offset), sizeof(offset));
}

uint64_t read_offset(std::ifstream &in)
{
    if (!in)
    {
        throw std::invalid_argument("Input stream is not valid.");
    }
    uint64_t offset;
    in.read(reinterpret_cast<char *>(&offset), sizeof(offset));
    return offset;
}
