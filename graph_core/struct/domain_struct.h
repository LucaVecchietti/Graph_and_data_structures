#pragma once

#include <unordered_map>
#include <string>
#include <utility>

/**
 *  RAM optimized struct
 */

/**
 * Base node struct — type-erased, holds the adjacency map.
 * neighborgs: relation_type -> { neighbor_index -> (weight, neighbor_ptr) }
 */
struct BaseNode
{
    virtual ~BaseNode() = default;
    std::unordered_map<std::string, std::unordered_map<int, std::pair<int, BaseNode *>>> neighborgs;
};

/**
 * Typed node — inherits adjacency from BaseNode and adds the actual data payload
 */
template <typename T>
struct Node : public BaseNode
{
    T data;
};

/**
 * RAM-side representation of a COMPLEX node payload.
 * Pairs the runtime type label (e.g. "Athlete", "Item", "Company") with the
 * JSON attributes describing that record. The on-disk counterpart is the
 * ComplexHeader POD in pod_struct.h (header) followed by the two raw strings.
 * Not POD — contains std::string — so it is NOT usable with write_pod /
 * NodeRecord<T>; the COMPLEX serialization path needs its own logic (WIP).
 */
struct ComplexRecord
{
    std::string type_label;
    std::string json_attributes;
};