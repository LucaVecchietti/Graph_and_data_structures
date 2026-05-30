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
void write_complex(const ComplexRecord &complex_record, std::ofstream &out);

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

void             write_meta(const MetaRecord &meta);
MetaRecord       read_meta();
void             write_json_attributes_meta(const JsonMeta &meta);
JsonMeta         read_json_attributes_meta();

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
 * @param node The Node to be written to disk.
 * @param meta The MetaRecord containing the next available node ID and other metadata.
 */
template <typename T>
void write_node(const Node<T> &node, const MetaRecord &meta)
{
    std::ofstream dat_out(std::filesystem::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::app);
    if (!dat_out) throw std::runtime_error("Failed to open nodes data file for writing.");

    NodeRecord<T> record;   // Convert the Node to a NodeRecord POD struct for serialization. Initialize the record with the data of the node.
    uint64_t record_offset; // Offset where the NodeRecord is written on the disc.

    switch (node_type_of_v<T>)  // Use the type tag to determine how to write the node record and relation list, and to set the type_id in the NodeIndex.
    {
        case NodeType::INT:
        case NodeType::FLOAT:
        case NodeType::CHAR:
        case NodeType::DOUBLE:
        case NodeType::BOOL:

            // For primitive types, we can directly write the NodeRecord to disk and get the record offset.
            dat_out.seekp(0, std::ios::end);
            record = node_to_record(node);
            record_offset = dat_out.tellp();
            write_pod(record, dat_out);

            break;

        case NodeType::COMPLEX:
            // For complex types, we need to write the ComplexRecord, which includes the type label and the JSON string of attributes.
            std::string json_file_path; // The file path where the JSON attributes of the complex node will be stored. This path is constructed based on the metadata and the type label of the node.
            record = complex_node_to_record(node, jso_file_path);
            dat_out.seekp(0, std::ios::end);
            record_offset = dat_out.tellp();
            write_complex(record, Node<ComplexRecord> node, json_file_path, dat_out);   // This function will handle the writing of the ComplexHeader and the associated JSON attributes to disk.
            break;

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
                // neighbor ptr is nullptr — must be re-linked after all nodes are loaded
                node->neighborgs[entry.name][static_cast<int>(edge.to_node)] = {static_cast<int>(edge.weight), nullptr};
            }
        }
    }

    return node;
}
