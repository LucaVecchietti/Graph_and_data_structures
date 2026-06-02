#pragma once

#include "../struct/domain_struct.h"
#include "../struct/pod_struct.h"
#include <unordered_map>
#include <type_traits>

// ---- Node ODT ----
/**
 * This file implement the ODT (Object Data Transfer) functions to translate the domain stract to POD stract and vice versa,
 * to mantain the efficente of the graph and to be able to save it on the disc.
 */

// ---- Domain to POD ----

/**
 * Translates a typed Node struct to a NodeRecord POD struct for serialization.
 * The NodeRecord contains only the data payload, while the adjacency information is stored separately in the RelationNodeList.
 * @tparam T The type of the data payload in the Node (must be a POD type).
 * @param node The Node to be translated.
 * @return The NodeRecord containing the data payload of the node for serialization. The NodeRecord is a simple wrapper that contains the data of the node, and it will be written on the disc as a primitive type node.
 * For complex types, the ComplexRecord struct will be used instead, which contains the type label and the JSON string of attributes for serialization. The ComplexRecord is a wrapper that contains the ComplexHeader and the JSON string, and it will be written on the disc as a complex type node.
 * The function uses a static_assert to ensure that the template parameter T is a trivially copyable type, which is a requirement for it to be safely serialized as a POD struct. If T is not a POD type, a compile-time error will be generated with the message "Template parameter T must be a POD type!".
 */
template <typename T>
NodeRecord<T> node_to_record(const Node<T> &node)
{
    static_assert(std::is_trivially_copyable_v<T>, "Template parameter T must be a POD type!");
    NodeRecord<T> record;
    record.data = node.data;
    return record;
}

/**
 * This function is used to translate a complex node to a ComplexRecord struct that can be written on the disc as a complex type node.
 * The ComplexRecord struct contains the type label and the json string that contains the attributes of the record in JSON format.
 * The complex type is identical to a classical DB Record, but it require a more complex logic to write and read the record on the DISC, so
 * we need to create a specific function to translate the complex node to a ComplexHeader struct that can be written on the disc using the 
 * NodeRecord struct as a container and writing the type label after the Header and the JSON string on a file. 
 * @param node The complex node to be translated.
 * @param json_file_path The file path where the JSON attributes of the complex node will be stored. This path is constructed based on the metadata and the type label of the node.
 * @return The ComplexRecord struct containing the type label and the JSON string of attributes for serialization. The ComplexRecord is a wrapper that contains the ComplexHeader and the JSON string, and it will be written on the disc as a complex type node.
 */
NodeRecord<ComplexHeader> complex_node_to_record(const Node<ComplexRecord> &node, std::string &json_file_path);

/**
 * Translates a typed Node struct to a RelationNodeList POD struct for serialization.
 * The RelationNodeList contains the adjacency information (relation types and neighbor offsets),
 * while the data payload is stored separately in the NodeRecord.
 * @param node The Node to be translated.
 * @return The RelationNodeList containing the adjacency information of the node for serialization.
 */
RelationNodeList node_to_relation_list(const BaseNode &node);

/**
 * Translates a NodeRecord POD struct back to a typed Node struct for use in memory.
 * The adjacency information must be reconstructed separately from the RelationNodeList.
 * @param id The ID of the node.
 * @param record_offset The byte offset where the NodeRecord is stored on disk.
 * @param relation_offset The byte offset where the RelationNodeList is stored on disk.
 * @return The reconstructed Node with the data from the record.
 */
NodeIndex node_to_node_index(uint64_t id, uint64_t record_offset, uint64_t relation_offset);

// ---- POD to Domain ----

/**
 * RelationEntry represents a single relation type and its associated edges for a node.
 * It is used when writing and reading the RelationNodeList to/from disk.
 * The name is the relation type (e.g., "road", "train"), edge_offset is the byte offset 
 * in edges.dat where the edges of this relation type are stored, and edge_count
 * is the number of edges of this relation type for the node. 
 */
struct RelationEntry
{
    std::string name;
    uint64_t    edge_offset;
    uint64_t    edge_count;
};

/**
 * Reconstructs the neighbor list for a node based on its relation list.
 * @param rellist The relation list containing adjacency information.
 * @return A map of relation types to their associated neighbors.
 */
std::unordered_map<std::string, std::unordered_map<int, EdgeRef>> reconstruct_neighbors(const RelationNodeList &rellist);

/**
 * Creates a node from its POD representation.
 * @param idx The node index.
 * @param record The node record.
 * @param rellist The relation list.
 * @return The reconstructed node.
 */
template <typename T>
BaseNode node_form_pod(const NodeIndex &idx, const NodeRecord<T> &record, const RelationNodeList &rellist){
    Node<T> node;
    node.data = record.data;

    node.neihborgs = reconstruct_neighbors(rellist);

    return node;
}