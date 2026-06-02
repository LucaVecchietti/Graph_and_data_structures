#include "type_registry.h"

#include <cstddef>
#include <stdexcept>
#include <string>

// ---- Type Registry — out-of-line definitions ----
//
// The compile-time C++ type → NodeType mapping (node_type_of / node_type_of_v)
// lives entirely in type_registry.h: it is template-only and must be visible to
// every TU that instantiates it. Only the non-template helper below is defined
// here so it has a single definition across the program (avoids the ODR /
// "multiple definition" linker error a header-level definition would cause).

/**
 * Returns the size of the on-disk data payload for a given NodeType.
 * @param type_id The NodeType for which to calculate the payload size.
 * @return The size in bytes of the NodeRecord payload for the given NodeType.
 */
size_t node_record_payload_size(NodeType type_id)
{
    switch (type_id)
    {
        case NodeType::INT:     return sizeof(int);
        case NodeType::FLOAT:   return sizeof(float);
        case NodeType::DOUBLE:  return sizeof(double);
        case NodeType::CHAR:    return sizeof(char);
        case NodeType::BOOL:    return sizeof(bool);
        case NodeType::COMPLEX: return sizeof(ComplexHeader); // header only — see note in type_registry.h
        default: throw std::runtime_error("Unknown NodeType: " + std::to_string(static_cast<uint8_t>(type_id)));
    }
}
