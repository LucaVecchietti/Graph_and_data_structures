#pragma once

#include <cstdint>

// ---- Costants ----

const uint8_t RELATION_TYPE_MAX_SIZE = 255;

/**
 * DISC optimized struct for I/O | POD
 */

/**
 * Base struct POD of the Node, hold the offsets of his contents
 */
#pragma pack(push, 1)
struct NodeIndex
{
    uint64_t id;              // ID of the node
    uint64_t offset;          // Offset to NodeRecord
    uint64_t relation_offset; // Offset to RelationNodeList
};
#pragma pack(pop)

/**
 *  Content struct POD of the Node - hold the data
 *  this Node Record can contain ONLY simple types
 *  e.g.
 *  T = int YES
 *  T = char YES
 *  T = float YES
 *  T = bool YES
 *
 *  T = std::vector<int> NO
 *  T = std::string NO
 *  T = std::map<int, std::vector<char>> NO
 *
 *  to implemente complex struct we need more complex method to wrote them on the DISC.
 */

#pragma pack(push, 1)
template <typename T>
struct NodeRecord
{
    T data; // Content of the Node
};
#pragma pack(pop)

/**
 *  Relation list struct POD of the Node - hold the typpes and the relatives offsets of the node
 */

#pragma pack(push, 1)
struct RelationNodeList
{
    uint64_t type_count; // Number rlation type the node has
    /**
     * After thi POD write every relation type preceded by the dimention of the string, wich is the name of the type,
     * and followed by the offset, that point tu the position where the edges are stored, and the number of edges.
     *
     * [After this POD structure wrote relation type as [`uint8_t`][s][t][r][i][n][g][`uint64_t`][`uint64_t`] ] * type_count
     * E.g.
     * [4][r][o][a][d][offset][relation_count]
     * [5][t][r][a][i][n][offset][relation_count]
     * ...
     */
};
#pragma pack(pop)

/**
 *  Base struct POD of the Edge - hold the weight and the offset wich point to the destination node.
 */
#pragma pack(push, 1)
struct Edge
{
    uint64_t id;      // Edge  ID
    int64_t weight;   // Weight of the edge
    uint64_t to_node; // Destination node idx on nodes.idx file [ to_node → NodeIndex(id == to_node)]
    uint64_t from_node; // Source node idx on nodes.idx file [ from_node → NodeIndex(id == from_node)]
};
#pragma pack(pop)