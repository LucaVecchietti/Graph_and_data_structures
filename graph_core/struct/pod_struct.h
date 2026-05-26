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

/**
 * Metadata struct POD for the graph, hold the offset of the next free position on the disc and the number of nodes in the graph.
 */

#pragma pack(push, 1)
struct MetaRecord
{
    uint64_t next_id;
    uint64_t node_count;
    uint64_t free_count; // How many offsets are available in freelist.dat
};
#pragma pack(pop)

#pragma pack(push, 1)
struct FreeRecord
{
    uint64_t offset; // free offset in nodes.dat
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