#pragma once

#include <cstdint>

/**
 *  NodeType is an enum class that defines the supported data types for node content.
 *  It is used as a type tag in the NodeIndex struct to indicate how to interpret the data stored in the NodeRecord.
 *  The current supported types are:
 * - INT: represents an integer value (NodeRecord<int>)
 * - FLOAT: represents a floating-point value (NodeRecord<float>)
 * - DOUBLE: represents a double-precision floating-point value (NodeRecord<double>)
 * - CHAR: represents a character value (NodeRecord<char>)
 * - BOOL: represents a boolean value (NodeRecord<bool>)
 * This enum allows the graph to support nodes with different types of content while maintaining a consistent on-disk format.
 */
enum class NodeType : uint8_t
{
    INT    = 0,
    FLOAT  = 1,
    DOUBLE = 2,
    CHAR   = 3,
    BOOL   = 4,

    COMPLEX = 255 // Reserved for Record that require a type label (e.g. std::string type = "Athlete" | "Item" | "Company" etc.) with different attributes.
    /**
     * To support different type of record we can use the COMPLEX type and write the type label as a string that rapresent the type of the record .
     * The the different attributes of the record can be stored as a JSON string after the hader of teh record. 
     * The complex type is identical to a classical DB Record, but it require a more complex logic to write and read the record on the DISC.
     * - NOTE: WIP.
     */
};

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
    NodeType type_id;         // Type tag of the stored data
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
    uint64_t type_count;  // Number of distinct relation types this node has.
    uint64_t batch_size;  // Size in bytes of the variable-width tail that follows
                          // this POD (NOT including the 16 bytes of the POD itself).
                          // Two uses:
                          //   1. Read path: after reading the POD, read batch_size
                          //      more bytes in one shot to grab the whole tail
                          //      without per-entry seeks.
                          //   2. Freelist: when this RelationNodeList region is
                          //      orphaned (e.g. by an in-place update from add_edge),
                          //      the total reclaimable size at `relation_offset` is
                          //      sizeof(RelationNodeList) + batch_size.
    /**
     * After this POD, `type_count` entries are laid out contiguously, each:
     *
     *   [uint64_t name_length][name bytes][uint64_t edge_offset][uint64_t edge_count]
     *
     * Fields per entry:
     *   - name_length   8 bytes, length prefix written by write_string.
     *   - name          name_length bytes, the relation type name (no NUL terminator).
     *   - edge_offset   8 bytes, byte offset into edges.dat where the edges of this
     *                   relation start.
     *   - edge_count    8 bytes, number of consecutive Edge POD records belonging
     *                   to this (node, relation) pair starting at edge_offset.
     *
     * Per-entry size on disk = 24 + name_length bytes. The whole tail is
     * `batch_size` bytes, i.e. sum of (24 + name_length) over all entries.
     *
     * Example tail with two relations "road" (3 edges) and "train" (5 edges):
     *   [8: name_length=4][r][o][a][d][8: edge_offset=...][8: edge_count=3]
     *   [8: name_length=5][t][r][a][i][n][8: edge_offset=...][8: edge_count=5]
     *
     * Note: the application-level cap on name_length is RELATION_TYPE_MAX_SIZE
     * (= 255, enforced by add_edge via costants.h), but the on-disk length prefix
     * is `uint64_t`, not `uint8_t` — the format itself permits up to 2^64-1.
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

/**
 * Metadata struct POD for the graph, hold the offset of the next free position on the disc and the number of nodes in the graph.
 */
#pragma pack(push, 1)
struct MetaRecord
{
    // --- Node bookkeeping ---
    uint64_t next_id;    // Next node id to assign on insert.
    uint64_t node_count; // How many live nodes exist.
    uint64_t free_count; // How many node offsets are available in the node freelist.

    // --- Edge bookkeeping ---
    uint64_t edge_count;      // How many live edges exist.
    uint64_t next_edge_id;    // Next edge id to assign when adding an edge.
    uint64_t free_edge_count; // How many edge offsets are available in the edge freelist.
};
#pragma pack(pop)

/**
 * ComplexHeader is a POD struct uesd for nodes of type COMPLEX, witch require a type label and a JSON string to store the attributes of the record.
 * The type_label is a string that rapresent the type of the record (e.g. "Athlete", "Item", "Company" etc.) 
 * and the json_file_path is a string that contains the path to the JSON file that stores the attributes of the record in JSON format.
 */
#pragma pack(push, 1)
struct ComplexHeader
{
    uint64_t type_label_size; // Size of the type label string
    uint64_t json_file_path_size; // Size of the JSON file path string
    // Followed by [type_label][json_file_path]
};
#pragma pack(pop)

/**
 * This struct is used to store the metadata of the JSON attributes of the complex nodes, 
 * such as a progressive number to generate unique JSON file names for complex nodes.
 * The JSON attributes of the complex nodes are stored in separate JSON files on the disk, 
 * and the ComplexHeader struct contains the path to the JSON file that contains the attributes of the record in JSON format.
 */
#pragma pack(push, 1)
struct JsonMeta
{
    uint64_t prog_number; // progressive number to generate unique JSON file names for complex nodes
};
#pragma pack(pop)

/**
 * Freelist record for a reclaimed NodeRecord region in nodes.dat, plus the
 * now-reusable id slot in nodes.idx. Persisted (appended) to db/nodes_freelist.dat.
 */
#pragma pack(push, 1)
struct NodeFreeOffset
{
    uint64_t idx;    // reusable node id (its fixed-width slot in nodes.idx)
    uint64_t offset; // free offset in nodes.dat (start of the orphaned NodeRecord)
    uint64_t size;   // size in bytes of the free region in nodes.dat
};
#pragma pack(pop)

/**
 * Freelist record for a reclaimed RelationNodeList region in nodes.dat (the POD
 * header + its variable-width tail). Persisted to db/relation_lists_freelist.dat.
 * No id is tracked: a relation list has no standalone id, only its byte region.
 */
#pragma pack(push, 1)
struct RelationNodeListFreeOffset
{
    uint64_t offset; // free offset in nodes.dat (start of the orphaned RelationNodeList)
    uint64_t size;   // size in bytes of the free region = sizeof(RelationNodeList) + batch_size
};
#pragma pack(pop)

/**
 * Freelist record for a reclaimed contiguous chunk of Edge records in edges.dat
 * (one chunk = all edges of a single (node, relation) pair). Persisted to
 * db/edges_freelist.dat. Tracks the whole batch, not a single edge.
 */
#pragma pack(push, 1)
struct BatchOfEdgesFreeOffset
{
    uint64_t idx;    // id of the first edge of the batch (reusable edge id starting point)
    uint64_t offset; // free offset in edges.dat (start of the orphaned chunk)
    uint64_t size;   // size in bytes of the free region = edge_count * sizeof(Edge)
};
#pragma pack(pop)