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
 */
NodeRecord<ComplexHeader> complex_node_to_record(const Node<ComplexRecord> &node, std::string &json_file_path);

/**
 * Translates a typed Node struct to a RelationNodeList POD struct for serialization.
 * The RelationNodeList contains the adjacency information (relation types and neighbor offsets),
 * while the data payload is stored separately in the NodeRecord.
 */

RelationNodeList node_to_relation_list(const BaseNode &node);

/**
 * Translates a NodeRecord POD struct back to a typed Node struct for use in memory.
 * The adjacency information must be reconstructed separately from the RelationNodeList.
 */

NodeIndex node_to_node_index(uint64_t id, uint64_t record_offset, uint64_t relation_offset);

/**
 * 
 */
std::unordered_map<std::string, std::unordered_map<int, std::pair<int, BaseNode *>>> reconstruct_neighbors(const RelationNodeList &rellist);

/**
 * 
 */
template <typename T>
BaseNode node_form_pod(const NodeIndex &idx, const NodeRecord<T> &record, const RelationNodeList &rellist){
    Node<T> node;
    node.data = record.data;

    node.neihborgs = reconstruct_neighbors(rellist);

    return node;
}