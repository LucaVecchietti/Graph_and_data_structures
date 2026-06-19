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

    TOMBSTONE = 254, // Logically-deleted slot. The entry in nodes.idx survives (so the id
                     // can be recycled via the freelist), but offset/relation_offset are
                     // zeroed and the on-disk NodeRecord / RelationNodeList / edge chunks
                     // have been zero-filled. read_node on a tombstoned id throws; a later
                     // reuse (write_node_in_freed_slot) overwrites the whole NodeIndex in
                     // place, clearing the tombstone naturally.

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
 *  Relation list struct POD of the Node — the FIXED-WIDTH-BATCH header.
 *
 *  A node's relation list is stored as one or more equal-size BATCHES. Each batch
 *  is this 37-byte POD header followed by a fixed RELATION_BATCH_TAIL-byte (= 2176)
 *  tail of up to RELATION_LINES_PER_BATCH (= 8) fixed-width lines. The whole region
 *  is therefore always sizeof(NodeRelationList) + RELATION_BATCH_TAIL = 2213 bytes,
 *  regardless of how many lines are actually used. Two payoffs:
 *    1. A single relation line can be located by index and rewritten IN PLACE
 *       (O(1) seek-write) — the foundation of O(1) add_edge.
 *    2. Every batch is the same on-disk size, so the relation freelist needs only
 *       ONE size class/bin instead of one per variable tail length.
 *  A node with more than 8 relation types chains a second batch via next_offset
 *  (head 1 -> 2 -> 3 ...). (Batch chaining traversal is WIP — see ROADMAP.)
 */
#pragma pack(push, 1)
struct NodeRelationList
{
    uint64_t node_id;     // Id of the owning node (back-reference, also aids recovery).
    uint64_t type_count;  // Number of relation-type LINES actually used in THIS batch (0..8).
    uint16_t batch_size;  // Reserved tail size in bytes. Constant RELATION_BATCH_TAIL (= 2176):
                          // the tail is always written full-width (unused lines zero-filled), so
                          // the total reclaimable region at the batch offset is
                          // sizeof(NodeRelationList) + batch_size = 2213 bytes — one freelist size class.
    uint16_t free_bytes;  // Free tail bytes = batch_size - type_count * RELATION_LINE_SIZE.
                          // Multiple of RELATION_LINE_SIZE (272); min 0, max 2176.
    uint64_t next_offset; // Offset in nodes.dat of the next batch of this list, or 0 if this is the last.
    uint64_t head;        // Batch serial number: 1 = first batch of the list, 2,3,... = extensions.
    uint8_t  is_deleted;  // 1 if this batch is a freed offset, 0 if live.
    /**
     * After this POD, RELATION_BATCH_TAIL bytes of fixed-width lines follow. The
     * first `type_count` lines are used; the rest are zero-filled. Each line is
     * RELATION_LINE_SIZE (= 272) bytes:
     *
     *   [uint64_t edge_offset][uint64_t edge_count][uint8_t name_length][char name[255]]
     *
     * Fields per line:
     *   - edge_offset   8 bytes, byte offset into edges.dat of the HEAD edge of this
     *                   (node, relation) pair's edge linked list (0 if no edges).
     *   - edge_count    8 bytes, number of edges in that linked list.
     *   - name_length   1 byte, actual length of the relation name (<= 255).
     *   - name          255 bytes, the relation name padded with NULs to full width.
     *
     * Fixed line width means line i lives at tail_offset + i * RELATION_LINE_SIZE,
     * so updating one relation's edge_offset/edge_count is a direct seek-write.
     */
};
#pragma pack(pop)

/**
 *  Base struct POD of the Edge - hold the weight and the offset wich point to the destination node.
 */
#pragma pack(push, 1)
struct Edge
{
    uint64_t id;        // Edge  ID
    int64_t weight;     // Weight of the edge
    uint64_t to_node;   // Destination node idx on nodes.idx file [ to_node → NodeIndex(id == to_node)]
    uint64_t from_node; // Source node idx on nodes.idx file [ from_node → NodeIndex(id == from_node)]
    uint64_t prev_offset; // Offset in edges.dat of the previous edge of the SAME (node, relation)
                          // chain, or 0 if this edge is the head. Edges of one relation form a
                          // doubly-linked list so a new edge is spliced in O(1) (append + relink)
                          // instead of rewriting the whole chunk.
    uint64_t next_offset; // Offset in edges.dat of the next edge of the same chain, or 0 if tail.
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
 * Freelist record for a reclaimed NodeRelationList batch region in nodes.dat (the
 * POD header + its fixed RELATION_BATCH_TAIL tail). Persisted to the single rel
 * size bin. No id is tracked: a relation batch has no standalone id, only its byte
 * region. Every batch is the same size (2213 bytes), so there is one rel size class.
 */
#pragma pack(push, 1)
struct RelationNodeListFreeOffset
{
    uint64_t offset; // free offset in nodes.dat (start of the orphaned NodeRelationList batch)
    uint64_t size;   // size in bytes of the free region = sizeof(NodeRelationList) + batch_size
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