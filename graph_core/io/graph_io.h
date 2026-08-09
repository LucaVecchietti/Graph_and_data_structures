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
#include <cstring>
#include <optional>
#include <unordered_map>
#include <unordered_set>

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
void write_complex(const ComplexRecord &record, const std::string &json_file_path, std::ostream &dat_out);

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

/**
 * Reads a node's WHOLE relation list — the chain of fixed-width batches starting at
 * the stream's current get position (typically NodeIndex.relation_offset) — and
 * returns the relation entries of every batch, in chain order.
 *
 * Each batch contributes its first `type_count` lines; the walk then hops to
 * `next_offset` until it hits 0 (the last batch), bounded by RELATION_MAX_BATCHES so
 * a corrupted offset throws instead of looping forever. On return the get position
 * sits at the end of the LAST batch region of the chain.
 *
 * @param batch_offsets Optional out-param: receives the nodes.dat offset of every
 *        batch visited, in chain order. Callers that must RECLAIM the list (delete /
 *        whole-node rewrite) need this — one freelist record per batch region — since
 *        NodeIndex only records where the chain starts.
 */
std::vector<RelationEntry>   read_relation_node_list(std::ifstream &in,
                                                     std::vector<uint64_t> *batch_offsets = nullptr);

void            write_meta(const MetaRecord &meta);
MetaRecord      read_meta();
void            write_json_attributes_meta(const JsonMeta &meta);
JsonMeta        read_json_attributes_meta();

void            update_node_edges(BaseNode &node, MetaRecord &meta, uint64_t node_id);

/**
 * O(1) persist of a brand-new edge (start --type--> to_id). The node's relation
 * batches never move, so NodeIndex is untouched. Steps:
 *   1. read relation_offset from nodes.idx, then walk the batch chain to find the
 *      line for `type` (O(number of batches), i.e. O(relation types / 8));
 *   2. allocate the new Edge slot (pop the single-edge `edges` freelist bin, else
 *      append at EOF of edges.dat);
 *   3. write the Edge with prev=0, next=current chain head;
 *   4. patch the OLD head's prev_offset in place (if the relation already existed);
 *   5. update that relation's fixed-width line in place (edge_offset = new head,
 *      edge_count + 1) — or, for a brand-new relation type, write a fresh line at
 *      slot `type_count` of the LAST batch and bump type_count / shrink free_bytes
 *      in its header.
 * If the last batch is full (8 lines), a fresh batch is allocated (exact-size `rel`
 * bin pop, else append), written with the new line, and linked in by patching only
 * the previous batch's next_offset — so the 8-relation-types cap no longer applies
 * and nodes.idx still never changes.
 * @return the byte offset of the newly-written Edge in edges.dat (store it in EdgeRef.offset).
 */
uint64_t        persist_new_edge(MetaRecord &meta, uint64_t node_id, const std::string &type,
                                 uint64_t to_id, uint64_t edge_id, int64_t weight);

/**
 * O(1) in-place overwrite of an existing edge's weight: seek to
 * edge_offset + offsetof(Edge, weight) in edges.dat and write 8 bytes. No
 * allocation, no relinking, no file growth.
 */
void            persist_edge_weight(uint64_t edge_offset, int64_t weight);

void            delete_node_from_disk(uint64_t node_id, MetaRecord &meta);

/**
 * Where an edge lives, in the terms Graph addresses edges with: the two node ids plus
 * the relation whose chain owns it. Returned by find_edge_by_id.
 */
struct EdgeLocation
{
    uint64_t    from_node;
    uint64_t    to_node;
    std::string relation;
};

/**
 * Resolves a global edge id to its (from_node, to_node, relation).
 *
 * There is no id -> offset index on disk, so this scans every live node exactly like
 * build_inbound_index: for each non-tombstoned nodes.idx slot it walks the node's
 * relation batch CHAIN and each relation's edge chain until it hits Edge.id == edge_id.
 * Only live nodes' chains are followed, so zeroed/freed regions are never mistaken for
 * live edges — a freed 48-byte slot reads back as id 0, which collides with the real
 * edge id 0, which is why a flat scan of edges.dat would not be sound. O(N + E).
 *
 * Edge ids are globally unique and never recycled, so the first match is the only one.
 * @return the location, or std::nullopt if no live edge carries that id.
 */
std::optional<EdgeLocation> find_edge_by_id(uint64_t edge_id, uint64_t next_id);

/**
 * Builds the inbound (reverse) edge index by scanning every live node on disk:
 * for each live node's outbound edge (from_node → to_node), records
 * to_node → { from_node, ... }. Tombstoned slots are skipped and only live
 * nodes' relation chunks are followed, so zeroed/freed edge regions are never
 * mistaken for live edges. O(N + E). Used by Graph at load to answer
 * "who points at X?" in O(deg_in) when deleting X.
 */
std::unordered_map<int, std::unordered_set<int>> build_inbound_index(uint64_t next_id);

// NOTE: write_free_offset / read_free_offset are templates (see below), so they
// work uniformly with NodeFreeOffset, RelationNodeListFreeOffset and
// BatchOfEdgesFreeOffset without a per-type declaration here.

// ---- Fixed-width relation-batch helpers ─────────────────────────────
//
// A relation batch tail is RELATION_BATCH_TAIL bytes of fixed-width lines (see
// pod_struct.h / costants.h). These helpers keep the on-disk line layout in one
// place so the read and write paths cannot drift.
//
// A node's relation list is a CHAIN of batches: NodeIndex.relation_offset points at
// the first one, each header's next_offset at the following one (0 = last), and `head`
// numbers them 1, 2, 3... Batches of one chain need NOT be contiguous in nodes.dat —
// each is allocated on its own (freelist pop or append), which is what keeps adding a
// relation type an in-place write that never moves the rest of the list.

/**
 * On-disk size of ONE batch region: the POD header plus its full fixed tail. Constant
 * (2213 bytes), which is why the `rel` freelist collapses to a single size class.
 */
inline constexpr uint64_t relation_batch_region_size()
{
    return sizeof(NodeRelationList) + RELATION_BATCH_TAIL;
}

/**
 * How many batches a node with `type_count` relation types needs: ceil(n / 8), and
 * always at least ONE — a node with no relations still owns an empty batch, so
 * NodeIndex.relation_offset always points at a readable header.
 */
inline uint64_t relation_batch_count(uint64_t type_count)
{
    if (type_count == 0) return 1;
    return (type_count + RELATION_LINES_PER_BATCH - 1) / RELATION_LINES_PER_BATCH;
}

/**
 * Lines used by batch index `b` (0-based) of a chain holding `type_count` types:
 * RELATION_LINES_PER_BATCH for every batch but the last, the remainder for the last.
 */
inline uint64_t relation_lines_in_batch(uint64_t type_count, uint64_t b)
{
    uint64_t written = b * RELATION_LINES_PER_BATCH;
    if (written >= type_count) return 0;
    uint64_t left = type_count - written;
    return left < RELATION_LINES_PER_BATCH ? left : RELATION_LINES_PER_BATCH;
}

/**
 * Writes one fixed-width relation line (RELATION_LINE_SIZE bytes) at the stream's
 * current put position: [edge_offset][edge_count][name_length][name padded to 255].
 */
inline void write_relation_line(std::ostream &out, uint64_t edge_offset, uint64_t edge_count, const std::string &name)
{
    write_offset(edge_offset, out);
    write_offset(edge_count, out);
    uint8_t name_len = static_cast<uint8_t>(name.size());
    out.write(reinterpret_cast<const char *>(&name_len), 1);
    char namebuf[RELATION_NAME_MAX] = {0};            // zero-padded fixed-width name field
    std::memcpy(namebuf, name.data(), name.size());
    out.write(namebuf, RELATION_NAME_MAX);
}

/**
 * Reads one fixed-width relation line written by write_relation_line into a
 * RelationEntry (name trimmed to its real length).
 */
inline RelationEntry read_relation_line(std::ifstream &in)
{
    RelationEntry e;
    e.edge_offset = read_offset(in);
    e.edge_count  = read_offset(in);
    uint8_t name_len = 0;
    in.read(reinterpret_cast<char *>(&name_len), 1);
    char namebuf[RELATION_NAME_MAX];
    in.read(namebuf, RELATION_NAME_MAX);
    e.name.assign(namebuf, name_len);
    return e;
}

/**
 * Zero-fills the remainder of a batch tail after `used_lines` used lines, so the
 * region is always exactly sizeof(NodeRelationList) + RELATION_BATCH_TAIL bytes
 * (one freelist size class; unused lines read back as zeros).
 */
inline void pad_relation_tail(std::ostream &out, uint64_t used_lines)
{
    uint64_t pad = RELATION_BATCH_TAIL - used_lines * RELATION_LINE_SIZE;
    if (pad == 0) return;
    std::vector<char> zeros(static_cast<size_t>(pad), 0);
    out.write(zeros.data(), static_cast<std::streamsize>(pad));
}

/**
 * Writes a single (node, relation) edge set as a doubly-linked, contiguous Edge
 * run. The stream put pointer MUST already sit at `base` (the head edge offset);
 * the run occupies neighbors.size() * sizeof(Edge) bytes from there. prev/next
 * offsets chain the edges so a future insert can splice in O(1). No-op if empty.
 */
inline void write_edge_chain_at(std::ostream &edges_out, uint64_t base, uint64_t from_node,
                                const std::unordered_map<int, EdgeRef> &neighbors)
{
    uint64_t n = static_cast<uint64_t>(neighbors.size());
    uint64_t i = 0;
    for (const auto &[to_id, ref] : neighbors)
    {
        uint64_t prev = (i == 0)     ? 0 : base + (i - 1) * sizeof(Edge);
        uint64_t next = (i == n - 1) ? 0 : base + (i + 1) * sizeof(Edge);
        Edge edge = edge_to_pod(ref.id, from_node, static_cast<uint64_t>(to_id),
                                static_cast<uint64_t>(ref.weight), prev, next);
        write_pod(edge, edges_out);
        ++i;
    }
}

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
 * For each relation type: writes [edge_offset][edge_count][name] after the POD header.
 * Returns the byte offset of the FIRST batch (what goes into NodeIndex.relation_offset).
 *
 * More than RELATION_LINES_PER_BATCH relation types spill into further batches,
 * chained via next_offset (head 1 -> 2 -> 3 ...). Because this writer appends the
 * whole chain back-to-back at the stream's put position, batch b+1 starts exactly
 * relation_batch_region_size() bytes after batch b — so every next_offset is known
 * up front and no back-patching is needed.
 */
template <typename T>
uint64_t write_relation_node_list(const Node<T> &node, uint64_t node_id, std::ofstream &out)
{
    const uint64_t region    = relation_batch_region_size();
    const uint64_t n_types   = static_cast<uint64_t>(node.neighborgs.size());
    const uint64_t n_batches = relation_batch_count(n_types);

    const uint64_t first_offset = static_cast<uint64_t>(out.tellp());

    std::ofstream edges_out(std::filesystem::path(DB_PATH) / "edges.dat", std::ios::binary | std::ios::app);
    if (!edges_out) throw std::runtime_error("Failed to open edges file for writing.");

    // Used by the initial node insert, when node.neighborgs is normally empty
    // (edges are attached later via add_edge) — then this writes one empty batch.
    // Each relation's edges are written as a doubly-linked contiguous run; the line
    // stores the head edge offset.
    auto rel_it = node.neighborgs.begin();
    for (uint64_t b = 0; b < n_batches; ++b)
    {
        const uint64_t lines = relation_lines_in_batch(n_types, b);
        const uint64_t next  = (b + 1 < n_batches) ? first_offset + (b + 1) * region : 0;

        write_pod(relation_batch_header(node_id, lines, b + 1, next), out);

        for (uint64_t i = 0; i < lines; ++i, ++rel_it)
        {
            const auto &rel_type  = rel_it->first;
            const auto &neighbors = rel_it->second;

            edges_out.seekp(0, std::ios::end);
            uint64_t edge_offset = static_cast<uint64_t>(edges_out.tellp());
            write_edge_chain_at(edges_out, edge_offset, node_id, neighbors);
            write_relation_line(out, neighbors.empty() ? 0 : edge_offset,
                                static_cast<uint64_t>(neighbors.size()), rel_type);
        }

        // Pad the rest of the fixed tail so every batch region is exactly 2213 bytes.
        pad_relation_tail(out, lines);
    }

    return first_offset;
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
 * Writes a freshly-inserted node into a slot reclaimed from the freelist instead
 * of appending. Counterpart of write_node for the reuse path taken by
 * Graph::insert when pop_free_offset returns a fitting NodeFreeOffset.
 *
 * Differences from write_node (append):
 *   - the NodeRecord<T> is written IN PLACE at `record_offset` (the freed
 *     region in nodes.dat) — exact-fit, so it never overruns the hole;
 *   - the NodeIndex is written IN PLACE at the freed id slot `node_id` in
 *     nodes.idx (fixed-width records make the slot directly addressable);
 *   - the (empty, for a fresh node) RelationNodeList is still APPENDED at the
 *     end of nodes.dat — reusing rel/edge bins is a separate step.
 *
 * Never instantiated for COMPLEX: its on-disk size is variable, so Graph::insert
 * guards the reuse path with `if constexpr (... != NodeType::COMPLEX)` and this
 * template (which would fail node_to_record's static_assert) is never reached.
 */
template <typename T>
void write_node_in_freed_slot(const Node<T> &node, uint64_t node_id, uint64_t record_offset)
{
    namespace fs = std::filesystem;

    // 1. NodeRecord<T> in place, at the freed offset.
    {
        std::fstream dat(fs::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::in | std::ios::out);
        if (!dat) throw std::runtime_error("write_node_in_freed_slot: failed to open nodes.dat for in-place write.");
        dat.seekp(static_cast<std::streamoff>(record_offset));
        NodeRecord<T> record = node_to_record(node);
        write_pod(record, dat);
    }

    // 2. Empty RelationNodeList appended at end-of-file (fresh node has no edges).
    uint64_t relation_offset = 0;
    {
        std::ofstream dat_app(fs::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::app);
        if (!dat_app) throw std::runtime_error("write_node_in_freed_slot: failed to open nodes.dat for append.");
        dat_app.seekp(0, std::ios::end);
        relation_offset = write_relation_node_list<T>(node, node_id, dat_app);
    }

    // 3. NodeIndex in place, at the freed id slot in nodes.idx.
    {
        std::fstream idx(fs::path(DB_PATH) / "nodes.idx", std::ios::binary | std::ios::in | std::ios::out);
        if (!idx) throw std::runtime_error("write_node_in_freed_slot: failed to open nodes.idx for in-place write.");
        idx.seekp(static_cast<std::streamoff>(node_id * sizeof(NodeIndex)));
        NodeIndex ni;
        ni.id              = node_id;
        ni.offset          = record_offset;
        ni.relation_offset = relation_offset;
        ni.type_id         = node_type_of_v<T>;
        write_pod(ni, idx);
    }
}

/**
 * COMPLEX counterpart of write_node_in_freed_slot: writes a freshly-inserted
 * COMPLEX node into a reclaimed slot instead of appending. The freed slot is an
 * EXACT fit because COMPLEX records of the same type_label length all have the
 * same size (see complex_record_on_disk_size), so writing the new header + two
 * strings in place never overruns the hole.
 *
 *   1. ComplexHeader + type_label + json_file_path IN PLACE at record_offset, and
 *      the JSON sidecar file written by write_complex. complex_node_to_record
 *      assigns the prog_number (recycled from the json free list, or fresh).
 *   2. The (empty, fresh node) RelationNodeList is APPENDED at end-of-file.
 *   3. The NodeIndex is written IN PLACE at the freed id slot.
 */
inline void write_complex_in_freed_slot(const Node<ComplexRecord> &node, uint64_t node_id, uint64_t record_offset)
{
    namespace fs = std::filesystem;

    // 1. ComplexHeader + two strings IN PLACE at the freed offset (exact-fit slot),
    //    plus the sidecar JSON file.
    {
        std::fstream dat(fs::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::in | std::ios::out);
        if (!dat) throw std::runtime_error("write_complex_in_freed_slot: failed to open nodes.dat for in-place write.");
        dat.seekp(static_cast<std::streamoff>(record_offset));
        std::string json_file_path;
        complex_node_to_record(node, json_file_path); // assigns prog_number + sidecar filename
        write_complex(node.data, json_file_path, dat);
    }

    // 2. Empty RelationNodeList appended at end-of-file (fresh node has no edges).
    uint64_t relation_offset = 0;
    {
        std::ofstream dat_app(fs::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::app);
        if (!dat_app) throw std::runtime_error("write_complex_in_freed_slot: failed to open nodes.dat for append.");
        dat_app.seekp(0, std::ios::end);
        relation_offset = write_relation_node_list<ComplexRecord>(node, node_id, dat_app);
    }

    // 3. NodeIndex in place, at the freed id slot in nodes.idx.
    {
        std::fstream idx(fs::path(DB_PATH) / "nodes.idx", std::ios::binary | std::ios::in | std::ios::out);
        if (!idx) throw std::runtime_error("write_complex_in_freed_slot: failed to open nodes.idx for in-place write.");
        idx.seekp(static_cast<std::streamoff>(node_id * sizeof(NodeIndex)));
        NodeIndex ni;
        ni.id              = node_id;
        ni.offset          = record_offset;
        ni.relation_offset = relation_offset;
        ni.type_id         = NodeType::COMPLEX;
        write_pod(ni, idx);
    }
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
            // Follow the edge linked list from its head (entry.edge_offset),
            // hopping by next_offset. edge_count bounds the walk (the chain may be
            // contiguous now, but next_offset makes the read robust to O(1) splices).
            uint64_t off = entry.edge_offset;
            for (uint64_t i = 0; i < entry.edge_count; ++i)
            {
                uint64_t cur = off; // this edge's own disk offset (kept in EdgeRef.offset)
                edges_in.seekg(static_cast<std::streamoff>(cur));
                Edge edge = read_pod<Edge>(edges_in);
                // neighbor ptr is nullptr — must be re-linked after all nodes are loaded.
                // edge.id is preserved so a later add_edge overwrite reuses the same id;
                // cur lets a weight overwrite seek straight to the record in O(1).
                node->neighborgs[entry.name][static_cast<int>(edge.to_node)] =
                    EdgeRef{edge.id, static_cast<int>(edge.weight), nullptr, cur};
                off = edge.next_offset;
            }
        }
    }

    return node;
}

// ---- Free Offset list management ----
//
// Freelists are SEGREGATED BY EXACT SIZE: there is one bin file per distinct
// free-region size, under db/freelist/. The size is encoded in the filename, so:
//   - push (on delete/orphan)  = append one record to the right bin   → O(1)
//   - pop  (on insert/reuse)   = read the last record + truncate it   → O(1)
// Every record in a bin has the same size, so a pop is always an exact fit:
// no scanning, no file rewrite, no bytes wasted. See freelist_bin_path below.
//   prefix "nodes" → NodeFreeOffset           (size ∈ {1,4,8} for primitives)
//   prefix "rel"   → RelationNodeListFreeOffset
//   prefix "edges" → BatchOfEdgesFreeOffset    (size = edge_count * sizeof(Edge))

/**
 * Builds the path of the freelist bin for a given (prefix, size):
 *   db/freelist/<prefix>_<size>.dat
 */
inline std::filesystem::path freelist_bin_path(const std::string &prefix, uint64_t size)
{
    return std::filesystem::path(DB_PATH) / "freelist" / (prefix + "_" + std::to_string(size) + ".dat");
}

/**
 * On-disk size of a COMPLEX record as a pure function of its type_label length.
 * Because the sidecar filename is zero-padded to a FIXED width (COMPLEX_PROG_DIGITS),
 * json_file_path_size no longer depends on prog_number — so every record of a given
 * type_label length has the exact same size, and the `complex_<size>.dat` freelist
 * bins act as per-type size classes. Layout: ComplexHeader + type_label + json_file_path,
 * where json_file_path = "<20 digits>_<type_label>.json".
 */
inline uint64_t complex_record_on_disk_size(uint64_t type_label_len)
{
    uint64_t json_path_len = COMPLEX_PROG_DIGITS + 1 + type_label_len + 5; // "_" (1) + ".json" (5)
    return sizeof(ComplexHeader) + type_label_len + json_path_len;
}

/**
 * Path of the json free list: a flat LIFO stack of freed prog_numbers (uint64).
 * Each COMPLEX node owns one sidecar FILE (no offsets), so reclaiming it is just
 * deleting the file and recycling its prog_number — pushed here on delete, popped
 * by complex_node_to_record on the next COMPLEX insert.
 */
inline std::filesystem::path json_freelist_path()
{
    return std::filesystem::path(DB_PATH) / "freelist" / "json_prog.dat";
}

/**
 * Appends a free-offset record to its size bin (push). Creates db/freelist/ on
 * first use. Header-defined template, so it throws on failure (no TU-local logger).
 * @param free_offset The free-offset record (its .size selects the bin via the caller).
 * @param freelist_path The bin file path, typically from freelist_bin_path(...).
 */
template <typename T>
void write_free_offset(const T &free_offset, const std::filesystem::path &freelist_path)
{
    std::filesystem::create_directories(freelist_path.parent_path());

    std::ofstream out(freelist_path, std::ios::binary | std::ios::app);
    if (!out)
    {
        throw std::runtime_error("Failed to open freelist file for writing: " + freelist_path.string());
    }

    write_pod(free_offset, out);
}

/**
 * Pops the last record off a size bin (LIFO): reads it, then shrinks the file by
 * one record. Because every record in a bin is the same size, the popped record
 * is always an exact fit for that bin's size class.
 * @param freelist_path The bin file path, typically from freelist_bin_path(...).
 * @return The popped record, or std::nullopt if the bin does not exist / is empty.
 * @throws std::runtime_error if the file exists but cannot be truncated.
 */
template <typename T>
std::optional<T> pop_free_offset(const std::filesystem::path &freelist_path)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    if (!fs::exists(freelist_path)) return std::nullopt;
    uint64_t fsize = static_cast<uint64_t>(fs::file_size(freelist_path, ec));
    if (ec || fsize < sizeof(T)) return std::nullopt; // empty (or corrupt-small) bin → nothing to reuse

    T record;
    {
        std::ifstream in(freelist_path, std::ios::binary);
        if (!in) return std::nullopt;
        in.seekg(static_cast<std::streamoff>(fsize - sizeof(T)));
        record = read_pod<T>(in);
    }

    // Pop: drop the last record by shrinking the file by exactly one record.
    fs::resize_file(freelist_path, fsize - sizeof(T), ec);
    if (ec)
    {
        throw std::runtime_error("Failed to truncate freelist file on pop: " + freelist_path.string());
    }

    return record;
}
