#pragma once

#include "../struct/pod_struct.h"
#include "../struct/domain_struct.h"
#include "../struct/type_registry.h"
#include "../odt/node_odt.h"
#include "../costants.h"
#include "io_utils.h"
#include <fstream>
#include <filesystem>

// Graph I/O header — defines functions for saving and loading the graph to/from disk in a binary format.

// ---- Non-template declarations ──────────────────────────────────────

/**
 * Writes a NodeIndex to nodes.idx. record_offset and relation_offset are the
 * positions already written by write_node_record / write_relation_node_list.
 */
void write_node_index(uint64_t record_offset, uint64_t relation_offset, NodeType type_id, std::ofstream &out, const MetaRecord &meta);

NodeIndex        read_node_index(std::ifstream &in);
RelationNodeList read_relation_node_list(std::ifstream &in);
void             write_meta(const MetaRecord &meta);
MetaRecord       read_meta();

// ---- Template definitions ───────────────────────────────────────────

/**
 * Writes the data payload of a Node to nodes.dat.
 * Returns the byte offset where the record was written.
 */
template <typename T>
uint64_t write_node_record(const Node<T> &node)
{
    std::ofstream out(std::filesystem::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::app);
    if (!out) throw std::runtime_error("Failed to open nodes record file for writing.");

    NodeRecord<T> record = node_to_record(node);
    uint64_t offset = out.tellp();
    write_pod(record, out);
    return offset;
}

/**
 * Writes the adjacency list of a Node to the provided stream.
 * Returns the byte offset where the relation list was written.
 */
template <typename T>
uint64_t write_relation_node_list(const Node<T> &node, std::ofstream &out)
{
    RelationNodeList list = node_to_relation_list(node);
    uint64_t offset = out.tellp();
    write_pod(list, out);
    return offset;
}

/**
 * Write Node (NodeIndex + NodeRecord + RelationNodeList) to disk.
 * Opens the necessary files internally.
 */
template <typename T>
void write_node(const Node<T> &node, const MetaRecord &meta)
{
    uint64_t record_offset = write_node_record<T>(node);

    std::ofstream rel_out(std::filesystem::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::app);
    if (!rel_out) throw std::runtime_error("Failed to open nodes record file for writing.");
    uint64_t relation_offset = write_relation_node_list<T>(node, rel_out);

    std::ofstream idx_out(std::filesystem::path(DB_PATH) / "nodes.idx", std::ios::binary | std::ios::app);
    if (!idx_out) throw std::runtime_error("Failed to open nodes index file for writing.");
    write_node_index(record_offset, relation_offset, node_type_of_v<T>, idx_out, meta);
}

template <typename T>
NodeRecord<T> read_node_record(std::ifstream &in);
