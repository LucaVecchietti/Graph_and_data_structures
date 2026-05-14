#include "node_odt.h"

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

    static_assert(std::is_trivially_assignable_v<T>, "Template parameter\ T must be a POD type!"); // Ensure T is a POD type for safe serialization

    NodeRecord<T> record;
    record.data = node.data; // Copy the data payload
    return record;
}

/**
 * Translates a typed Node struct to a RelationNodeList POD struct for serialization.
 * The RelationNodeList contains the adjacency information (relation types and neighbor offsets),
 * while the data payload is stored separately in the NodeRecord.
 */

 RelationNodeList node_to_relation_list(const BaseNode &node)
 {
     RelationNodeList list;
     list.type_count = node.neighborgs.size(); // Count the number of relation types
     return list;
 }

/**
 * Translates a NodeRecord POD struct back to a typed Node struct for use in memory.
 * The adjacency information must be reconstructed separately from the RelationNodeList.
 */

NodeIndex node_to_node_index(uint64_t id, uint64_t record_offset, uint64_t relation_offset)
{
    NodeIndex idx;
    idx.id = id;
    idx.offset = record_offset;
    idx.relation_offset = relation_offset;
    return idx;
}
