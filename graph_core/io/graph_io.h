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

// ---- Non-template declarations ──────────────────────────────────────

/**
 * Writes a NodeIndex to nodes.idx. record_offset and relation_offset are the
 * positions already written by write_node_record / write_relation_node_list.
 */
void write_node_index(uint64_t record_offset, uint64_t relation_offset, NodeType type_id, std::ofstream &out, const MetaRecord &meta);

/**
 * Writes the on-disk payload of a COMPLEX node and its JSON sidecar file.
 * Mirror image of read_complex. Caller is responsible for positioning dat_out
 * at the desired offset (typically end-of-file in append mode).
 * Writes, in order:
 *   1. ComplexHeader        (POD, 16 bytes — built from record.type_label.size()
 *                            and json_file_path.size())
 *   2. type_label           (length-prefixed string via write_string)
 *   3. json_file_path       (length-prefixed string via write_string)
 * Then opens the sidecar file at JSON_ATTR_PATH / json_file_path (trunc) and
 * writes record.json_attributes into it as raw UTF-8.
 * Throws std::runtime_error if the sidecar file cannot be opened.
 */
void write_complex(const ComplexRecord &record, const std::string &json_file_path, std::ofstream &dat_out);

/**
 * Reads the on-disk payload of a COMPLEX node into a ComplexRecord.
 * The stream must already be positioned at the start of the ComplexHeader
 * (i.e. at NodeIndex.offset). Reads, in order:
 *   1. ComplexHeader        (POD, 16 bytes: type_label_size + json_file_path_size)
 *   2. type_label           (length-prefixed string written via write_string)
 *   3. json_file_path       (length-prefixed string written via write_string)
 * Then opens the sidecar file at JSON_ATTR_PATH / json_file_path and reads its
 * full contents into out.json_attributes (raw UTF-8 text, no framing).
 * Throws std::runtime_error if the sidecar file cannot be opened — a COMPLEX
 * node without its JSON sidecar is a corrupted database, not a valid state.
 */
void read_complex(ComplexRecord &out, std::ifstream &dat_in);

NodeIndex                    read_node_index(std::ifstream &in);
std::vector<RelationEntry>   read_relation_node_list(std::ifstream &in);

void            write_meta(const MetaRecord &meta);
MetaRecord      read_meta();
void            write_json_attributes_meta(const JsonMeta &meta);
JsonMeta        read_json_attributes_meta();

void            update_node_edges(BaseNode &node, const MetaRecord &meta, uint64_t node_id);

void            delete_node_from_disk(uint64_t node_id, const MetaRecord &meta);

// NOTE: write_free_offset / read_free_offset are templates (see below), so they
// work uniformly with NodeFreeOffset, RelationNodeListFreeOffset and
// BatchOfEdgesFreeOffset without a per-type declaration here.

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

    // Used by the initial node insert, when node.neighborgs is normally empty
    // (edges are attached later via add_edge). Each edge is written with its own
    // EdgeRef.id rather than a per-node-local counter.
    for (const auto &[rel_type, neighbors] : node.neighborgs)
    {
        uint64_t edge_offset = edges_out.tellp();
        for (const auto &[to_id, ref] : neighbors)
        {
            Edge edge = edge_to_pod(ref.id, node_id, static_cast<uint64_t>(to_id), static_cast<uint64_t>(ref.weight));
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
 * @param node The Node to be written to disk.
 * @param meta The MetaRecord containing the next available node ID and other metadata.
 */
template <typename T>
void write_node(const Node<T> &node, const MetaRecord &meta)
{
    std::ofstream dat_out(std::filesystem::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::app);
    if (!dat_out) throw std::runtime_error("Failed to open nodes data file for writing.");

    // Always append: position the stream at end-of-file and capture that as the
    // record offset, regardless of node type.
    dat_out.seekp(0, std::ios::end);
    uint64_t record_offset = dat_out.tellp();

    // Compile-time dispatch on the node type. Using if constexpr instead of a
    // runtime switch ensures that the COMPLEX branch (which expects a non-POD
    // ComplexRecord payload and would fail static_assert in write_pod / node_to_record)
    // is never instantiated for primitive T, and vice versa.
    if constexpr (node_type_of_v<T> == NodeType::COMPLEX)
    {
        // COMPLEX: out-of-line JSON payload. complex_node_to_record reads
        // JsonMeta to compose the sidecar filename and stores it in
        // json_file_path; we then hand both to write_complex which writes the
        // header + two length-prefixed strings here and the JSON contents to
        // the sidecar file under JSON_ATTR_PATH.
        std::string json_file_path;
        complex_node_to_record(node, json_file_path);
        write_complex(node.data, json_file_path, dat_out);
    }
    else
    {
        // Primitive types (INT/FLOAT/DOUBLE/CHAR/BOOL): NodeRecord<T> is
        // trivially copyable, so we write it straight as a POD.
        NodeRecord<T> record = node_to_record(node);
        write_pod(record, dat_out);
    }

    uint64_t relation_offset = write_relation_node_list<T>(node, meta.next_id, dat_out);

    std::ofstream idx_out(std::filesystem::path(DB_PATH) / "nodes.idx", std::ios::binary | std::ios::app);
    if (!idx_out) throw std::runtime_error("Failed to open nodes index file for writing.");
    write_node_index(record_offset, relation_offset, node_type_of_v<T>, idx_out, meta);
}

/**
 * Reads a node from disk based on its ID.
 * @param id The ID of the node to read.
 * @return A pointer to the reconstructed node.
 */
BaseNode* read_node(uint64_t id);

/**
 * Reads a NodeRecord of type T from the given input stream.
 * @tparam T The type of the data payload in the NodeRecord (must be a POD type).
 * @param in The input stream to read from.
 * @return The NodeRecord containing the data payload of the node.
 */
template <typename T>
NodeRecord<T> read_node_record(std::ifstream &in)
{
    return read_pod<NodeRecord<T>>(in);
}

/**
 * Reads a node from disk based on its ID, reconstructing both the data payload and the adjacency information.
 * The function first reads the NodeIndex to get the offsets for the NodeRecord and the RelationNodeList, then reads the NodeRecord to get the data payload, 
 * and finally reads the RelationNodeList to reconstruct the adjacency information.
 * @param id The ID of the node to read.
 * @return A pointer to the reconstructed node with its data and neighbors.
 */
template <typename T>
BaseNode* read_typed_node(const NodeIndex &node_idx, std::ifstream &dat_in)
{
    Node<T> *node = new Node<T>();

    // Position the stream at the start of the payload (NodeRecord<T> for primitives,
    // or ComplexHeader for COMPLEX). The payload layout depends on the node type,
    // so we dispatch at compile time on node_type_of_v<T>.
    dat_in.seekg(static_cast<std::streamoff>(node_idx.offset));

    if constexpr (node_type_of_v<T> == NodeType::COMPLEX)
    {
        // COMPLEX: payload is ComplexHeader + 2 length-prefixed strings, and the
        // actual JSON attributes live in a sidecar file. read_complex performs
        // all of these reads and populates node->data in one shot.
        read_complex(node->data, dat_in);
    }
    else
    {
        // Primitive types (INT/FLOAT/DOUBLE/CHAR/BOOL): payload is a fixed-size
        // NodeRecord<T> that can be read in a single binary slurp.
        NodeRecord<T> record = read_node_record<T>(dat_in);
        node->data = record.data;
    }

    // Read the RelationNodeList from the data file using the relation_offset from
    // the NodeIndex, and reconstruct the adjacency information. This block is
    // identical for every node type.
    dat_in.seekg(static_cast<std::streamoff>(node_idx.relation_offset));
    std::vector<RelationEntry> entries = read_relation_node_list(dat_in);

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
                // neighbor ptr is nullptr — must be re-linked after all nodes are loaded.
                // edge.id is preserved so a later add_edge overwrite reuses the same id.
                node->neighborgs[entry.name][static_cast<int>(edge.to_node)] =
                    EdgeRef{edge.id, static_cast<int>(edge.weight), nullptr};
            }
        }
    }

    return node;
}

// ---- Free Offset list management ----

/**
 * Writes a free offset record to the specified freelist file. 
 * This function appends the free offset information to the freelist file, 
 * which can later be used to reuse freed space in nodes.dat, 
 * edges.dat, or relation_lists.dat when new nodes or edges are added.
 * @param free_offset The free offset information to write, containing the offset and size of the freed region.
 * @param freelist_path The path to the freelist file where the free offset should be
 * @throws std::runtime_error if the freelist file cannot be opened for writing.
 */
template <typename T>
void write_free_offset(const T &free_offset, const std::filesystem::path &freelist_path)
{
    // NOTE: this template is header-defined, so it cannot use the TU-local
    // `logger` (anonymous-namespace symbol in graph_io.cpp). Like the other
    // header templates, it signals failure by throwing only.
    std::ofstream out(freelist_path, std::ios::binary | std::ios::app);
    if (!out)
    {
        throw std::runtime_error("Failed to open freelist file for writing: " + freelist_path.string());
    }

    write_pod(free_offset, out);
}

/**
 * This function reads a free offset record from the bottom of the specified freelist file.
 * @param freelist_path The path to the freelist file to read from.
 * @return The free offset record read from the file, containing the offset and size of a freed region that can be reused.
 * @throws std::runtime_error if the freelist file cannot be opened for reading, or if the file is empty (i.e. no free offsets available).
 * @throws std::runtime_error if the file is too small to contain a free offset record (i.e. file corruption or invalid format).
 */
template <typename T>
T read_free_offset(const std::filesystem::path &freelist_path)
{
    // Header-defined template: throw-only on error (no TU-local `logger` here).
    std::ifstream in(freelist_path, std::ios::binary | std::ios::ate);
    if (!in)
    {
        throw std::runtime_error("Failed to open freelist file for reading: " + freelist_path.string());
    }

    if (in.tellg() == 0)
    {
        throw std::runtime_error("Freelist file is empty: " + freelist_path.string());
    }

    if (in.tellg() < static_cast<std::streamoff>(sizeof(T)))
    {
        throw std::runtime_error("Freelist file is too small to contain a free offset record: " + freelist_path.string());
    }

    in.seekg(-static_cast<std::streamoff>(sizeof(T)), std::ios::end);
    if (!in)
    {
        throw std::runtime_error("Failed to seek to the last free offset in freelist file: " + freelist_path.string());
    }

    return read_pod<T>(in);
}
