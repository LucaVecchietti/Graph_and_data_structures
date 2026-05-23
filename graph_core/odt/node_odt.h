#pragma once

#include "../struct/domain_struct.h"
#include "../struct/pod_struct.h"
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