#include "graph_io.h"
#include "../odt/node_odt.h"

/**
 * Write Node, which consists of a NodeIndex, a NodeRecord, and a RelationNodeList, to the output stream.
 */

template <typename T>
void write_node(const Node<T> &node, std::ofstream &out)
{
    uint64_t record_offset = write_node_record<T>(node, out); // Write NodeRecord to disk and get its offset
    uint64_t relation_offset = write_relation_node_list<T>(node, out); // Write

    write_node_index<T>(node, out); // Create NodeIndex with offsets
}

/**
 * 
 */

template <typename T>
uint64_t write_node_record(const Node<T> &node, std::ofstream &out)
{
    NodeRecord<T> record = node_to_record(node); // Convert to POD struct
    uint64_t offset = out.tellp(); // Get current offset for the record
    write_pod(record, out); // Write the NodeRecord to disk
    return offset; // Return the offset where the record was written
}

/**
 * 
 */

template <typename T>
uint64_t write_relation_node_list(const Node<T> &node, std::ofstream &out)
{
    RelationNodeList list = node_to_relation_list(node); // Convert to POD struct
    uint64_t offset = out.tellp(); // Get current offset for the relation list
    write_pod(list, out); // Write the RelationNodeList to disk
    return offset; // Return the offset where the relation list was written
}

/**
 * Writes a NodeIndex struct to the output stream.
 */
void write_node_index(const Node<T> &node, std::ofstream &out)
{
    NodeIndex idx;
    idx.id = node.data; // Assuming the node's data can serve as its ID, otherwise generate a unique ID
    idx.offset = out.tellp(); // Get current offset for the NodeIndex
    write_pod(idx, out); // Write the NodeIndex to disk
}