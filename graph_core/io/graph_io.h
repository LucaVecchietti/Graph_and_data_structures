#pragma once

#include "../struct/pod_struct.h"
#include "../struct/domain_struct.h"
#include "../struct/type_registry.h"
#include "../odt/node_odt.h"
#include "../odt/edge_odt.h"
#include "../costants.h"
#include "io_utils.h"
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>

// Graph I/O header — defines functions for saving and loading the graph to/from disk in a binary format.

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

// ---- Non-template declarations ──────────────────────────────────────

/**
 * Writes a NodeIndex to nodes.idx. record_offset and relation_offset are the
 * positions already written by write_node_record / write_relation_node_list.
 */
void write_node_index(uint64_t record_offset, uint64_t relation_offset, NodeType type_id, std::ofstream &out, const MetaRecord &meta);

NodeIndex                    read_node_index(std::ifstream &in);
std::vector<RelationEntry>   read_relation_node_list(std::ifstream &in);
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
 * Writes the adjacency list of a Node to nodes.dat (out) and edges to edges.dat.
 * For each relation type: writes [name][edge_offset][edge_count] after the POD header.
 * Returns the byte offset where the relation list was written.
 */
template <typename T>
uint64_t write_relation_node_list(const Node<T> &node, uint64_t node_id, std::ofstream &out)
{
    RelationNodeList list = node_to_relation_list(node);
    uint64_t offset = out.tellp();
    write_pod(list, out);

    std::ofstream edges_out(std::filesystem::path(DB_PATH) / "edges.dat", std::ios::binary | std::ios::app);
    if (!edges_out) throw std::runtime_error("Failed to open edges file for writing.");

    uint64_t edge_idx = 0;
    for (const auto &[rel_type, neighbors] : node.neighborgs)
    {
        uint64_t edge_offset = edges_out.tellp();
        for (const auto &[to_id, wp] : neighbors)
        {
            Edge edge = edge_to_pod(edge_idx++, node_id, static_cast<uint64_t>(to_id), static_cast<uint64_t>(wp.first));
            write_pod(edge, edges_out);
        }
        uint64_t edge_count = static_cast<uint64_t>(neighbors.size());
        write_string(rel_type, out);
        write_offset(edge_offset, out);
        write_offset(edge_count, out);
    }
    return offset;
}

/**
 * Write Node (NodeIndex + NodeRecord + RelationNodeList) to disk.
 * Uses a single open on nodes.dat so that tellp() returns correct offsets
 * for both the record and the relation list (double-open in app mode on
 * Windows returns 0 from tellp() before the first write).
 */
template <typename T>
void write_node(const Node<T> &node, const MetaRecord &meta)
{
    std::ofstream dat_out(std::filesystem::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::app);
    if (!dat_out) throw std::runtime_error("Failed to open nodes data file for writing.");

    dat_out.seekp(0, std::ios::end);
    NodeRecord<T> record = node_to_record(node);
    uint64_t record_offset = dat_out.tellp();
    write_pod(record, dat_out);

    uint64_t relation_offset = write_relation_node_list<T>(node, meta.next_id, dat_out);

    std::ofstream idx_out(std::filesystem::path(DB_PATH) / "nodes.idx", std::ios::binary | std::ios::app);
    if (!idx_out) throw std::runtime_error("Failed to open nodes index file for writing.");
    write_node_index(record_offset, relation_offset, node_type_of_v<T>, idx_out, meta);
}

BaseNode* read_node(uint64_t id);

template <typename T>
NodeRecord<T> read_node_record(std::ifstream &in)
{
    return read_pod<NodeRecord<T>>(in);
}

template <typename T>
BaseNode* read_typed_node(const NodeIndex &node_idx, std::ifstream &dat_in)
{
    dat_in.seekg(static_cast<std::streamoff>(node_idx.offset));
    NodeRecord<T> record = read_node_record<T>(dat_in);

    dat_in.seekg(static_cast<std::streamoff>(node_idx.relation_offset));
    std::vector<RelationEntry> entries = read_relation_node_list(dat_in);

    Node<T> *node = new Node<T>();
    node->data = record.data;

    if (!entries.empty())
    {
        std::ifstream edges_in(std::filesystem::path(DB_PATH) / "edges.dat", std::ios::binary);
        if (!edges_in) throw std::runtime_error("Failed to open edges file for reading.");

        for (const auto &entry : entries)
        {
            edges_in.seekg(static_cast<std::streamoff>(entry.edge_offset));
            for (uint64_t i = 0; i < entry.edge_count; ++i)
            {
                Edge edge = read_pod<Edge>(edges_in);
                // neighbor ptr is nullptr — must be re-linked after all nodes are loaded
                node->neighborgs[entry.name][static_cast<int>(edge.to_node)] = {static_cast<int>(edge.weight), nullptr};
            }
        }
    }

    return node;
}
