#include <filesystem>
#include "../costants.h"
#include "graph_io.h"
#include "../odt/node_odt.h"
#include "../struct/pod_struct.h"

/**
 * Write Node, which consists of a NodeIndex, a NodeRecord, and a RelationNodeList, to the output stream.
 */

template <typename T>
void write_node(const Node<T> &node, const MetaRecord &meta)
{
    uint64_t record_offset = write_node_record<T>(node); // Write NodeRecord to disk and get its offset
    uint64_t relation_offset = write_relation_node_list<T>(node); // Write

    write_node_index<T>(record_offset, relation_offset, out, meta); // Create NodeIndex with offsets
}

/**
 * 
 */

template <typename T>
uint64_t write_node_record(const Node<T> &node)
{   
    std::ofstream out (std::filesystem::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::app);
    if (!out){
        throw std::runtime_error("Failed to open nodes record file for writing. ");
    }

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
void write_node_index(const uint64_t record_offset, const uint64_t relation_offset, std::ofstream &out, const MetaRecord &meta)
{
    std::ofstream out(std::filesystem::path(DB_PATH) / "nodes.idx", std::ios::binary | std::ios::app);
    if (!out) {
        throw std::runtime_error("Failed to open nodes record file for writing");
    } 

    NodeIndex idx;
    idx.id = meta.next_id; // Assign the next available ID to the NodeIndex
    idx.offset = record_offset; // Assign the offset to the content of the node
    idx.relation_offset = relation_offset;  // Assign the offset to the relation list
}

// ---- Meta Data I/O ----

void write_meta(const MetaRecord &meta){

    std::ofstream out(std::filesystem::path(DB_PATH) / "meta.dat", std::ios::binary | std::ios::trunc);
    if (!out)
    {
        throw std::runtime_error("Failed to open nodes index file for writing.");
    }

    write_pod<MetaRecord>(meta, out);
}

MetaRecord read_meta(){

    std::ifstream in(std::filesystem::path(DB_PATH) / "meta.dat", std::ios::binary);
    if (!in)
    {
        throw std::runtime_error("Failed to open nodes index file for writing.");
    }

    return read_pod<MetaRecord>(in);
}