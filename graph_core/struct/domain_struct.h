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