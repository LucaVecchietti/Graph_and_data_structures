#pragma once

#include <unordered_map>
#include <string>
#include <utility>
#include <cstdint>

/**
 *  RAM optimized struct
 */

struct BaseNode; // forward declaration: EdgeRef points back to a neighbor node.

/**
 * RAM-side description of a single outgoing edge.
 *  - id        globally-unique edge id; source of truth is MetaRecord.next_edge_id,
 *              assigned once when the edge is first added and preserved across the
 *              full-node rewrites performed by update_node_edges. On the disk side
 *              it is the Edge POD's `id` field.
 *  - weight    edge weight.
 *  - neighbor  pointer to the destination BaseNode (nullptr until re-linked after a
 *              load from disk; see read_node).
 *  - offset    byte offset of this edge's Edge POD in edges.dat. Lets add_edge
 *              overwrite the weight in place in O(1) (persist_edge_weight) without
 *              walking the chain. Set on load (read_typed_node), on first append
 *              (persist_new_edge), and refreshed when update_node_edges relocates
 *              the node's edges. Meaningless until the edge is persisted.
 */
struct EdgeRef
{
    uint64_t id;
    int weight;
    BaseNode *neighbor;
    uint64_t offset;
};

/**
 * Base node struct — type-erased, holds the adjacency map.
 * neighborgs: relation_type -> { neighbor_index -> EdgeRef(id, weight, neighbor_ptr) }
 */
struct BaseNode
{
    virtual ~BaseNode() = default;
    std::unordered_map<std::string, std::unordered_map<int, EdgeRef>> neighborgs;
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