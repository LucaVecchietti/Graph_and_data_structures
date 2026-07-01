#include <filesystem>
#include <sstream>
#include "../logger.h"
#include "../costants.h"
#include "graph_io.h"
#include "../odt/node_odt.h"
#include "../struct/pod_struct.h"

// TU-local logger: wrapped in an anonymous namespace so the symbol has internal
// linkage and does not collide with the `logger` defined at the same scope in
// node_odt.cpp (and any other TU that follows this pattern).
namespace {
    Logger logger("graph_io.log", LogLevel::DEBUG);

    // Zero `size` bytes starting at `offset` in a stream opened in|out binary.
    // Used on delete to leave no dead bytes behind (NodeRecord, RelationNodeList,
    // edge chunks). The freelist bins track offset+size, not the byte content, so a
    // later reuse simply overwrites these zeros.
    void zero_region(std::fstream &f, uint64_t offset, uint64_t size)
    {
        if (size == 0) return;
        std::vector<char> zeros(static_cast<size_t>(size), 0);
        f.seekp(static_cast<std::streamoff>(offset));
        f.write(zeros.data(), static_cast<std::streamoff>(size));
    }

    // Walk an edge linked list from `head_offset` for `count` edges and free each
    // one individually: push its 48-byte slot onto the single-edge `edges` freelist
    // bin, zero its bytes, and bump meta.free_edge_count. Edges are no longer
    // contiguous (O(1) add_edge splices them anywhere), so a chain walk — not a
    // single contiguous chunk — is the only correct way to reclaim them. The next
    // pointer is read BEFORE the slot is zeroed.
    void free_edge_chain(uint64_t head_offset, uint64_t count, MetaRecord &meta)
    {
        if (count == 0) return;
        namespace fs = std::filesystem;

        std::ifstream edges_in(fs::path(DB_PATH) / "edges.dat", std::ios::binary);
        std::fstream edges_io(fs::path(DB_PATH) / "edges.dat", std::ios::binary | std::ios::in | std::ios::out);
        if (!edges_in || !edges_io)
        {
            logger.error("free_edge_chain: failed to open edges.dat.");
            throw std::runtime_error("free_edge_chain: failed to open edges.dat.");
        }

        uint64_t off = head_offset;
        for (uint64_t i = 0; i < count; ++i)
        {
            edges_in.clear();
            edges_in.seekg(static_cast<std::streamoff>(off));
            Edge e = read_pod<Edge>(edges_in);
            uint64_t next = e.next_offset;

            write_free_offset(BatchOfEdgesFreeOffset{e.id, off, sizeof(Edge)},
                              freelist_bin_path("edges", sizeof(Edge)));
            zero_region(edges_io, off, sizeof(Edge));
            meta.free_edge_count++;

            off = next;
        }
    }
}

/**
 * Writes a NodeIndex struct to the output stream.
 */
void write_node_index(const uint64_t record_offset, const uint64_t relation_offset, NodeType type_id, std::ofstream &out, const MetaRecord &meta)
{
    if (!out) {
        logger.error("Failed to open nodes index file for writing.");
        throw std::runtime_error("Failed to open nodes index file for writing");
    }

    NodeIndex idx;
    idx.id = meta.next_id;
    idx.offset = record_offset;
    idx.relation_offset = relation_offset;
    idx.type_id = type_id;
    write_pod(idx, out);
}

/**
 * Writes a COMPLEX node payload (ComplexHeader + 2 length-prefixed strings)
 * to nodes.dat and the associated JSON attributes to the sidecar file under
 * JSON_ATTR_PATH. Mirror image of read_complex. See graph_io.h for the layout.
 *
 * The ComplexHeader is built locally from the current sizes of type_label and
 * json_file_path so the on-disk header is always self-consistent with the two
 * strings that follow it.
 */
void write_complex(const ComplexRecord &record, const std::string &json_file_path, std::ostream &dat_out)
{
    // 1. Build the POD header from the current string sizes and write it.
    //    These two uint64_t fields are technically redundant with the length
    //    prefixes emitted by write_string just below, but the layout is what
    //    read_complex / the docs assume — keep it in sync.
    ComplexHeader header;
    header.type_label_size = record.type_label.size();
    header.json_file_path_size = json_file_path.size();
    write_pod(header, dat_out);

    // 2. type_label and json_file_path as length-prefixed strings on the main
    //    stream. Note: it is json_file_path that goes on disk here, not
    //    json_attributes — the JSON payload itself lives in the sidecar file.
    write_string(record.type_label, dat_out);
    write_string(json_file_path, dat_out);

    // 3. Open the sidecar JSON file at the SAME path that was just written
    //    inside the header, and dump the JSON payload into it.
    std::filesystem::path json_path = std::filesystem::path(JSON_ATTR_PATH) / json_file_path;
    std::ofstream json_out(json_path, std::ios::binary | std::ios::trunc);
    if (!json_out)
    {
        logger.error("Failed to open JSON attributes file for writing: " + json_path.string());
        throw std::runtime_error("Failed to open JSON attributes file for writing: " + json_path.string());
    }

    json_out << record.json_attributes;
    json_out.close();
}

/**
 * Reads a COMPLEX node payload (ComplexHeader + 2 length-prefixed strings) from
 * nodes.dat and the associated JSON attributes from the sidecar file under
 * JSON_ATTR_PATH. See the header declaration in graph_io.h for the on-disk
 * layout assumed by this function.
 *
 * Mirrors the symmetry of write_complex: header + type_label + json_file_path
 * on the main stream, then the JSON payload from the sidecar file.
 */
void read_complex(ComplexRecord &out, std::ifstream &dat_in)
{
    // 1. ComplexHeader. The two size fields are redundant with the length
    //    prefixes written by write_string, so we read the header to advance
    //    the stream but rely on read_string for the actual lengths.
    ComplexHeader header = read_pod<ComplexHeader>(dat_in);
    (void)header;

    // 2. type_label and json_file_path — both written via write_string, so they
    //    carry their own uint64_t length prefix.
    out.type_label = read_string(dat_in);
    std::string json_file_path = read_string(dat_in);

    // 3. Open the sidecar JSON file and slurp its contents into json_attributes.
    //    A COMPLEX node whose sidecar is missing is a corrupted database, not
    //    a recoverable state — fail loudly.
    std::filesystem::path json_path = std::filesystem::path(JSON_ATTR_PATH) / json_file_path;
    std::ifstream json_in(json_path, std::ios::binary);
    if (!json_in)
    {
        logger.error("Failed to open JSON attributes file for reading: " + json_path.string());
        throw std::runtime_error("Failed to open JSON attributes file for reading: " + json_path.string());
    }

    std::ostringstream buf;
    buf << json_in.rdbuf();
    out.json_attributes = buf.str();
}

std::vector<RelationEntry> read_relation_node_list(std::ifstream &in)
{
    NodeRelationList header = read_pod<NodeRelationList>(in);
    std::vector<RelationEntry> entries;
    entries.reserve(header.type_count);
    for (uint64_t i = 0; i < header.type_count; ++i)
        entries.push_back(read_relation_line(in));

    // Skip the unused (zero-filled) remainder of the fixed tail so the stream
    // sits at the end of this batch region.
    in.seekg(static_cast<std::streamoff>(header.free_bytes), std::ios::cur);
    return entries;
}

NodeIndex read_node_index(std::ifstream &in)
{
    return read_pod<NodeIndex>(in);
}

/**
 * Reads a node from disk based on its ID.
 * @param id The ID of the node to read.
 * @return A pointer to the reconstructed node.
 */
BaseNode* read_node(uint64_t id)
{
    std::ifstream idx_in(std::filesystem::path(DB_PATH) / "nodes.idx", std::ios::binary);
    if (!idx_in) throw std::runtime_error("Failed to open nodes index file for reading.");
    idx_in.seekg(static_cast<std::streamoff>(id * sizeof(NodeIndex)));
    NodeIndex node_idx = read_node_index(idx_in);

    std::ifstream dat_in(std::filesystem::path(DB_PATH) / "nodes.dat", std::ios::binary);
    if (!dat_in) throw std::runtime_error("Failed to open nodes data file for reading.");

    switch (node_idx.type_id)   // Dispatch based on the NodeType to read the correct type of node
    {
        case NodeType::INT:    return read_typed_node<int>   (node_idx, dat_in);
        case NodeType::FLOAT:  return read_typed_node<float> (node_idx, dat_in);
        case NodeType::DOUBLE: return read_typed_node<double>(node_idx, dat_in);
        case NodeType::CHAR:   return read_typed_node<char>  (node_idx, dat_in);
        case NodeType::BOOL:   return read_typed_node<bool>  (node_idx, dat_in);
        case NodeType::COMPLEX: return read_typed_node<ComplexRecord>(node_idx, dat_in);
        case NodeType::TOMBSTONE:
            throw std::runtime_error("read_node: node id " + std::to_string(id) + " is tombstoned (deleted).");
        default: throw std::runtime_error("Unknown NodeType for node id " + std::to_string(id));
    }
}

// ---- Meta Data I/O ----

void write_meta(const MetaRecord &meta){

    std::ofstream out(std::filesystem::path(DB_PATH) / "meta.dat", std::ios::binary | std::ios::trunc);
    if (!out)
    {
        logger.error("Failed to open meta file for writing.");
        throw std::runtime_error("Failed to open meta file for writing.");
    }

    write_pod<MetaRecord>(meta, out);
}

MetaRecord read_meta(){

    std::ifstream in(std::filesystem::path(DB_PATH) / "meta.dat", std::ios::binary);
    if (!in)
    {
        logger.error("Failed to open meta file for reading.");
        throw std::runtime_error("Failed to open meta file for reading.");
    }

    return read_pod<MetaRecord>(in);
}

/**
 * Writes the JSON attributes metadata to the disk.
 * @param meta The JSON attributes metadata to write. This metadata contains information such as the progressive number to generate unique JSON file names for complex nodes.
 */
void write_json_attributes_meta(const JsonMeta &meta)
{
    std::ofstream out(std::filesystem::path(DB_PATH) / "attributes/attributes_meta.dat", std::ios::binary | std::ios::trunc);
    if (!out)
    {
        logger.error("Failed to open JSON attributes meta file for writing.");
        throw std::runtime_error("Failed to open JSON attributes meta file for writing.");
    }

    write_pod<JsonMeta>(meta, out);
}

/**
 * tHis function reads the JSON attributes metadata from the disk, which contains information 
 * such as the progressive number to generate unique JSON file names for complex nodes.
 * The JSON attributes metadata is essential for managing the JSON files that store the attributes of complex nodes,
 * ensuring that each complex node's attributes are stored in a uniquely identifiable JSON file.
 */
JsonMeta read_json_attributes_meta()
{   
    // Ensure the JSON attributes meta file exists and is not empty before attempting to read it.
    if (!std::filesystem::exists(std::filesystem::path(DB_PATH) / "attributes/attributes_meta.dat"))
    {
        logger.info("Did not find JSON attributes meta file. Creating a new one.");
        JsonMeta meta;
        meta.prog_number = 0; // Initialize the progressive number to 0 for the first complex node
        write_json_attributes_meta(meta);
    }

    // Now read the JSON attributes meta file, which should exist and contain valid data.
    std::ifstream in(std::filesystem::path(DB_PATH) / "attributes/attributes_meta.dat", std::ios::binary);
    //  Check if the file was opened successfully before attempting to read from it.
    if (!in)
    {
        logger.error("Failed to open JSON attributes meta file for reading.");
        throw std::runtime_error("Failed to open JSON attributes meta file for reading.");
    }

    // Check if the file is empty before attempting to read the JsonMeta struct.
    if (in.peek() == std::ifstream::traits_type::eof())
    {
        logger.error("JSON attributes meta file is empty.");
        throw std::runtime_error("JSON attributes meta file is empty.");
    }

    return read_pod<JsonMeta>(in);
}

/**
 * O(1) persist of a brand-new edge. See graph_io.h for the full contract.
 * The relation batch never moves, so nodes.idx is only READ (for relation_offset).
 */
uint64_t persist_new_edge(MetaRecord &meta, uint64_t node_id, const std::string &type,
                          uint64_t to_id, uint64_t edge_id, int64_t weight)
{
    namespace fs = std::filesystem;

    // ---- 1. relation_offset (batch start) from nodes.idx -------------------
    uint64_t relation_offset;
    {
        std::ifstream idx_in(fs::path(DB_PATH) / "nodes.idx", std::ios::binary);
        if (!idx_in) throw std::runtime_error("persist_new_edge: failed to open nodes.idx for reading.");
        idx_in.seekg(static_cast<std::streamoff>(node_id * sizeof(NodeIndex)));
        relation_offset = read_node_index(idx_in).relation_offset;
    }

    // ---- 2. read the batch header + lines, locate the relation line --------
    NodeRelationList header;
    int found = -1;            // line index of `type`, or -1 if a brand-new relation
    uint64_t old_head = 0;     // current chain head of that relation (0 if none)
    uint64_t old_count = 0;
    const uint64_t tail_off = relation_offset + sizeof(NodeRelationList);
    {
        std::ifstream dat_in(fs::path(DB_PATH) / "nodes.dat", std::ios::binary);
        if (!dat_in) throw std::runtime_error("persist_new_edge: failed to open nodes.dat for reading.");
        dat_in.seekg(static_cast<std::streamoff>(relation_offset));
        header = read_pod<NodeRelationList>(dat_in);
        for (uint64_t i = 0; i < header.type_count; ++i)
        {
            RelationEntry e = read_relation_line(dat_in); // sequential after the header
            if (e.name == type)
            {
                found     = static_cast<int>(i);
                old_head  = e.edge_offset;
                old_count = e.edge_count;
                break;
            }
        }
    }

    // ---- 3. allocate the new Edge slot (reuse a freed 48-byte slot, else append) ----
    uint64_t new_off;
    {
        std::optional<BatchOfEdgesFreeOffset> reuse =
            pop_free_offset<BatchOfEdgesFreeOffset>(freelist_bin_path("edges", sizeof(Edge)));

        std::fstream edges(fs::path(DB_PATH) / "edges.dat", std::ios::binary | std::ios::in | std::ios::out);
        if (!edges) throw std::runtime_error("persist_new_edge: failed to open edges.dat for writing.");

        if (reuse)
        {
            new_off = reuse->offset;          // exact 48-byte fit (edge ids not recycled)
            meta.free_edge_count--;
        }
        else
        {
            edges.seekp(0, std::ios::end);
            new_off = static_cast<uint64_t>(edges.tellp());
        }

        // New edge becomes the chain HEAD: prev=0, next=old head (0 for a new relation).
        uint64_t next = (found >= 0) ? old_head : 0;
        Edge e = edge_to_pod(edge_id, node_id, to_id, static_cast<uint64_t>(weight), 0, next);
        edges.seekp(static_cast<std::streamoff>(new_off));
        write_pod(e, edges);

        // 4. relink: the old head's prev now points at the new edge.
        if (found >= 0 && old_head != 0)
        {
            edges.seekp(static_cast<std::streamoff>(old_head + offsetof(Edge, prev_offset)));
            write_offset(new_off, edges);
        }
    }

    // ---- 5. update the relation line (and header, for a new relation) in place ----
    {
        std::fstream dat(fs::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::in | std::ios::out);
        if (!dat) throw std::runtime_error("persist_new_edge: failed to open nodes.dat for in-place update.");

        if (found >= 0)
        {
            // Existing relation: only head offset + count change (first 16 bytes of the line).
            dat.seekp(static_cast<std::streamoff>(tail_off + static_cast<uint64_t>(found) * RELATION_LINE_SIZE));
            write_offset(new_off, dat);
            write_offset(old_count + 1, dat);
        }
        else
        {
            // New relation type: needs a free line in this batch (chaining is WIP).
            if (header.type_count >= RELATION_LINES_PER_BATCH || header.free_bytes < RELATION_LINE_SIZE)
                throw std::runtime_error("persist_new_edge: node " + std::to_string(node_id)
                                         + " relation batch full (" + std::to_string(RELATION_LINES_PER_BATCH)
                                         + " types) — batch chaining not yet implemented.");

            uint64_t slot = header.type_count;
            dat.seekp(static_cast<std::streamoff>(tail_off + slot * RELATION_LINE_SIZE));
            write_relation_line(dat, new_off, 1, type);

            header.type_count += 1;
            header.free_bytes -= RELATION_LINE_SIZE;
            dat.seekp(static_cast<std::streamoff>(relation_offset));
            write_pod(header, dat);
        }
    }

    logger.info("persist_new_edge: node " + std::to_string(node_id) + " --" + type + "--> "
                + std::to_string(to_id) + " edge id " + std::to_string(edge_id)
                + " at edges.dat offset " + std::to_string(new_off)
                + (found >= 0 ? " (existing relation)" : " (new relation line)"));
    return new_off;
}

/**
 * O(1) in-place weight overwrite. See graph_io.h.
 */
void persist_edge_weight(uint64_t edge_offset, int64_t weight)
{
    namespace fs = std::filesystem;
    std::fstream edges(fs::path(DB_PATH) / "edges.dat", std::ios::binary | std::ios::in | std::ios::out);
    if (!edges) throw std::runtime_error("persist_edge_weight: failed to open edges.dat for writing.");
    edges.seekp(static_cast<std::streamoff>(edge_offset + offsetof(Edge, weight)));
    edges.write(reinterpret_cast<const char *>(&weight), sizeof(weight));
}

/**
 * Updates the edges of a node on the disk when a new edge is added to the node in memory.
 * This function delete the current edges batch and rewrites it whitch the new edges added to the node
 * ina  new position on the disk, then it updates the relation list of the node tu poitn to the new position
 * and finnealy add the free offstet to the nodes freelist to be reused.
 * @param node The node whose edges need to be updated.
 * @param meta The metadata containing information about the graph state.
 * @param node_id The ID of the node whose edges need to be updated.
 */
void update_node_edges(BaseNode &node, MetaRecord &meta, uint64_t node_id)
{
    namespace fs = std::filesystem;

    // ---- 1. Read the current relation list of the node ---------------------
    // The OLD relation list region (in nodes.dat) and each OLD per-relation
    // edge chunk (in edges.dat) will be orphaned by this update — we record
    // their offsets/sizes here so step 2 can hand them to the freelist.
    NodeIndex node_idx;
    {
        std::ifstream idx_in(fs::path(DB_PATH) / "nodes.idx", std::ios::binary);
        if (!idx_in)
        {
            logger.error("update_node_edges: failed to open nodes.idx for reading.");
            throw std::runtime_error("update_node_edges: failed to open nodes.idx for reading.");
        }
        idx_in.seekg(static_cast<std::streamoff>(node_id * sizeof(NodeIndex)));
        node_idx = read_node_index(idx_in);
    }

    std::vector<RelationEntry> old_entries;
    uint64_t old_relation_total_size = 0; // sizeof(POD) + batch_size of the OLD list
    {
        std::ifstream dat_in(fs::path(DB_PATH) / "nodes.dat", std::ios::binary);
        if (!dat_in)
        {
            logger.error("update_node_edges: failed to open nodes.dat for reading.");
            throw std::runtime_error("update_node_edges: failed to open nodes.dat for reading.");
        }
        dat_in.seekg(static_cast<std::streamoff>(node_idx.relation_offset));
        NodeRelationList old_header = read_pod<NodeRelationList>(dat_in);
        old_relation_total_size = sizeof(NodeRelationList) + old_header.batch_size;

        // read_relation_node_list expects the stream positioned at the POD
        // start, so rewind before delegating to it for the tail entries.
        dat_in.seekg(static_cast<std::streamoff>(node_idx.relation_offset));
        old_entries = read_relation_node_list(dat_in);
    }

    // ---- 2. Orphan the OLD regions onto the freelist bins -------------------
    // Free each old relation's edges by walking its chain (edges are no longer
    // contiguous, so free_edge_chain pushes each 48-byte slot onto the single-edge
    // `edges` bin, zeroes it, and bumps free_edge_count). Then push the old batch
    // region onto the `rel` bin and zero it.
    for (const auto &entry : old_entries)
        free_edge_chain(entry.edge_offset, entry.edge_count, meta);

    write_free_offset(RelationNodeListFreeOffset{node_idx.relation_offset, old_relation_total_size},
                      freelist_bin_path("rel", old_relation_total_size));
    {
        std::fstream dat_io(fs::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::in | std::ios::out);
        if (dat_io) zero_region(dat_io, node_idx.relation_offset, old_relation_total_size);
    }

    logger.info("update_node_edges: orphaned RelationNodeList at offset "
                + std::to_string(node_idx.relation_offset)
                + " of size " + std::to_string(old_relation_total_size)
                + " bytes -> rel bin; free_edge_count=" + std::to_string(meta.free_edge_count));

    // ---- 3. Write the NEW relation batch and the NEW edge runs -------------
    // The relation batch is reclaimed pop-then-append: every batch is one size
    // class (2213 B), and step 2 just freed this node's old batch onto the `rel`
    // bin, so the pop returns that very region → in-place rewrite, no nodes.dat
    // growth. The edge runs are appended fresh at EOF (the old, possibly-scattered
    // edges were freed per-edge in step 2 and are reused later by persist_new_edge).
    NodeRelationList list = node_to_relation_list(node, node_id);
    uint64_t new_relation_total_size = sizeof(NodeRelationList) + list.batch_size;

    // Single-batch only for now (chaining via next_offset is WIP).
    if (list.type_count > RELATION_LINES_PER_BATCH)
        throw std::runtime_error("update_node_edges: node " + std::to_string(node_id)
                                 + " has more than " + std::to_string(RELATION_LINES_PER_BATCH)
                                 + " relation types (batch chaining not yet implemented).");

    // Decide where the new relation-list region lands: reuse an exact-size freed
    // `rel` region if one exists, else append at EOF (set below from tellp()).
    auto rel_reuse = pop_free_offset<RelationNodeListFreeOffset>(
        freelist_bin_path("rel", new_relation_total_size));

    uint64_t new_relation_offset = 0;
    {
        // in|out (NOT app): seekp lands in-place for reuse and correctly extends
        // the file for append on Windows (the "seekp ignored" issue is app-only).
        std::fstream dat_out(fs::path(DB_PATH) / "nodes.dat",
                             std::ios::binary | std::ios::in | std::ios::out);
        if (!dat_out)
        {
            logger.error("update_node_edges: failed to open nodes.dat for writing.");
            throw std::runtime_error("update_node_edges: failed to open nodes.dat for writing.");
        }
        if (rel_reuse)
        {
            new_relation_offset = rel_reuse->offset; // in-place reuse (exact fit)
            dat_out.seekp(static_cast<std::streamoff>(new_relation_offset));
        }
        else
        {
            dat_out.seekp(0, std::ios::end);         // append at EOF
            new_relation_offset = dat_out.tellp();
        }

        // The reuse fit is exact (every batch is one size class now), so the POD
        // plus the full fixed tail land in exactly new_relation_total_size bytes —
        // no overrun. Use ONE in|out stream for the POD + every line so the put
        // pointer advances contiguously (never seek dat_out between lines).
        write_pod(list, dat_out);

        std::fstream edges_out(fs::path(DB_PATH) / "edges.dat",
                               std::ios::binary | std::ios::in | std::ios::out);
        if (!edges_out)
        {
            logger.error("update_node_edges: failed to open edges.dat for writing.");
            throw std::runtime_error("update_node_edges: failed to open edges.dat for writing.");
        }

        // This is the whole-node rewrite path (used only by delete_node's inbound
        // cleanup now — add_edge takes the O(1) persist_new_edge path). Each
        // relation's edges are re-laid as a fresh CONTIGUOUS doubly-linked run
        // appended at EOF (the old, possibly-scattered chain was already freed in
        // step 2). As we write each edge we REFRESH its RAM EdgeRef.offset, so a
        // later O(1) weight overwrite seeks to the right place after relocation.
        // Each Edge keeps its own EdgeRef.id.
        uint64_t used = 0;
        for (auto &[rel_type, neighbors] : node.neighborgs)
        {
            uint64_t edge_count = static_cast<uint64_t>(neighbors.size());

            edges_out.seekp(0, std::ios::end);
            uint64_t base = static_cast<uint64_t>(edges_out.tellp());

            uint64_t i = 0;
            for (auto &[to_id, ref] : neighbors)
            {
                uint64_t prev = (i == 0)               ? 0 : base + (i - 1) * sizeof(Edge);
                uint64_t next = (i == edge_count - 1)  ? 0 : base + (i + 1) * sizeof(Edge);
                Edge e = edge_to_pod(ref.id, node_id, static_cast<uint64_t>(to_id),
                                     static_cast<uint64_t>(ref.weight), prev, next);
                write_pod(e, edges_out);
                ref.offset = base + i * sizeof(Edge); // refresh RAM offset after relocation
                ++i;
            }

            // Fixed-width line on the nodes.dat stream; its put pointer is right
            // after the POD / previous line — do not seek it between lines.
            write_relation_line(dat_out, edge_count == 0 ? 0 : base, edge_count, rel_type);
            ++used;
        }

        // Zero-fill the rest of the fixed tail so the region is exactly 2213 bytes.
        pad_relation_tail(dat_out, used);
    }

    // ---- 4. Patch NodeIndex.relation_offset in-place -----------------------
    // nodes.idx is fixed-width (25 bytes per entry), so we can seek directly
    // to the relation_offset field of this node's entry and overwrite the
    // 8-byte uint64_t. Open in_out (NOT app) so the seek lands inside the
    // existing file rather than at end-of-file (Windows ignores seekp in app
    // mode). This is the only place where nodes.idx is mutated in-place;
    // every other writer uses pure append.
    {
        std::fstream idx_inout(fs::path(DB_PATH) / "nodes.idx",
                               std::ios::binary | std::ios::in | std::ios::out);
        if (!idx_inout)
        {
            logger.error("update_node_edges: failed to open nodes.idx for in-place update.");
            throw std::runtime_error("update_node_edges: failed to open nodes.idx for in-place update.");
        }
        idx_inout.seekp(static_cast<std::streamoff>(
            node_id * sizeof(NodeIndex) + offsetof(NodeIndex, relation_offset)));
        write_offset(new_relation_offset, idx_inout);
    }

    logger.info("update_node_edges: node " + std::to_string(node_id)
                + " relation_offset patched to " + std::to_string(new_relation_offset));
}

void delete_node_from_disk(uint64_t node_id, MetaRecord &meta)
{
    // 0. Calculate the node index offset for the given node_id and log it for debugging purposes.
    uint64_t node_index_offset = node_id * sizeof(NodeIndex);
    logger.info("delete_node_from_disk: calculated node index offset for node " + std::to_string(node_id) + ": " + std::to_string(node_index_offset));

    // 1. Read the NodeIndex of the node to be deleted to obtain the offsets of its data and relations.
    NodeIndex node_idx;
    {
        std::ifstream idx_in(std::filesystem::path(DB_PATH) / "nodes.idx", std::ios::binary);
        if (!idx_in)
        {
            logger.error("delete_node_from_disk: failed to open nodes.idx for reading.");
            throw std::runtime_error("delete_node_from_disk: failed to open nodes.idx for reading.");
        }
        idx_in.seekg(static_cast<std::streamoff>(node_index_offset));
        node_idx = read_node_index(idx_in);
    }

    logger.debug("delete_node_from_disk: read NodeIndex for node " + std::to_string(node_id) + ": "
                + "record_offset=" + std::to_string(node_idx.offset) + ", "
                + "relation_offset=" + std::to_string(node_idx.relation_offset) + ", "
                + "type_id=" + std::to_string(static_cast<uint8_t>(node_idx.type_id)));

    // 2. Read the RelationNodeList of the node to be deleted to obtain the offsets and sizes of its edge chunks.
    //    We read the POD header first to capture batch_size (the exact on-disk size of the variable-width tail),
    //    then rewind and parse the per-relation entries. The total reclaimable size of the relation-list region
    //    is sizeof(RelationNodeList) + batch_size — same accounting used by update_node_edges.
    std::vector<RelationEntry> relation_entries;
    uint64_t relation_list_total_size = 0;
    {
        std::ifstream dat_in(std::filesystem::path(DB_PATH) / "nodes.dat", std::ios::binary);
        if (!dat_in)
        {
            logger.error("delete_node_from_disk: failed to open nodes.dat for reading.");
            throw std::runtime_error("delete_node_from_disk: failed to open nodes.dat for reading.");
        }
        dat_in.seekg(static_cast<std::streamoff>(node_idx.relation_offset));
        NodeRelationList header = read_pod<NodeRelationList>(dat_in);
        relation_list_total_size = sizeof(NodeRelationList) + header.batch_size;

        // read_relation_node_list expects the stream at the POD start, so rewind before delegating.
        dat_in.seekg(static_cast<std::streamoff>(node_idx.relation_offset));
        relation_entries = read_relation_node_list(dat_in);
    }

    logger.debug("delete_node_from_disk: read " + std::to_string(relation_entries.size()) + " relation entries for node " + std::to_string(node_id));

    // 3. Determine the orphaned NodeRecord size and the freelist bin prefix.
    //    Primitives have a fixed size from the type tag. COMPLEX is variable: its
    //    real size is ComplexHeader + the two strings, read from the header here,
    //    and it lands in the `complex_<size>` bins (per-type size classes). While
    //    we have the header open we also reclaim the JSON sidecar: delete the file
    //    and recycle its prog_number onto the json free list.
    std::string node_bin_prefix = "nodes";
    uint64_t record_size;
    if (node_idx.type_id == NodeType::COMPLEX)
    {
        node_bin_prefix = "complex";
        std::ifstream dat_in(std::filesystem::path(DB_PATH) / "nodes.dat", std::ios::binary);
        if (!dat_in)
        {
            logger.error("delete_node_from_disk: failed to open nodes.dat to read COMPLEX header.");
            throw std::runtime_error("delete_node_from_disk: failed to open nodes.dat to read COMPLEX header.");
        }
        dat_in.seekg(static_cast<std::streamoff>(node_idx.offset));
        ComplexHeader h = read_pod<ComplexHeader>(dat_in);
        std::string type_label = read_string(dat_in);            // advances past type_label
        std::string json_file_path = read_string(dat_in);        // the sidecar filename
        record_size = sizeof(ComplexHeader) + h.type_label_size + h.json_file_path_size;

        // Reclaim the sidecar file and recycle its prog_number (the zero-padded
        // leading digits of the filename) for the next COMPLEX insert.
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(JSON_ATTR_PATH) / json_file_path, ec);
        if (ec)
            logger.error("delete_node_from_disk: failed to remove JSON sidecar " + json_file_path + ": " + ec.message());
        try
        {
            uint64_t prog = std::stoull(json_file_path.substr(0, COMPLEX_PROG_DIGITS));
            write_free_offset<uint64_t>(prog, json_freelist_path());
        }
        catch (const std::exception &e)
        {
            logger.error("delete_node_from_disk: could not recycle prog_number from " + json_file_path + ": " + e.what());
        }
    }
    else
    {
        record_size = node_record_payload_size(node_idx.type_id);
    }

    logger.info("delete_node_from_disk: orphaning NodeRecord at offset "
                + std::to_string(node_idx.offset)
                + " of size " + std::to_string(record_size) + " bytes (nodes.dat)");

    logger.info("delete_node_from_disk: orphaning RelationNodeList at offset "
                + std::to_string(node_idx.relation_offset)
                + " of size " + std::to_string(relation_list_total_size) + " bytes (nodes.dat)");

    // 4. Build the free-offset records for the NodeRecord region and the relation batch.
    //    The node's edges are reclaimed separately by free_edge_chain (step 5b), which
    //    walks each relation's linked list and frees the individual 48-byte slots.
    NodeFreeOffset record_free_offset{node_id, node_idx.offset, record_size};
    RelationNodeListFreeOffset relation_list_free_offset{node_idx.relation_offset, relation_list_total_size};

    logger.info("delete_node_from_disk: adding NodeRecord free offset to freelist: idx=" + std::to_string(record_free_offset.idx) + ", offset=" + std::to_string(record_free_offset.offset) + ", size=" + std::to_string(record_free_offset.size));
    logger.info("delete_node_from_disk: adding RelationNodeList free offset to freelist: offset=" + std::to_string(relation_list_free_offset.offset) + ", size=" + std::to_string(relation_list_free_offset.size));

    // 5a. Push the NodeRecord region + relation batch onto their EXACT-SIZE bins.
    write_free_offset(record_free_offset, freelist_bin_path(node_bin_prefix, record_free_offset.size));
    write_free_offset(relation_list_free_offset, freelist_bin_path("rel", relation_list_free_offset.size));

    // 5b. Free each relation's edge chain (per-edge 48-byte slots, zeroed) — bumps
    //     free_edge_count internally.
    for (const auto &entry : relation_entries)
        free_edge_chain(entry.edge_offset, entry.edge_count, meta);

    // 6. Zero the orphaned NodeRecord + relation batch in nodes.dat (edges already
    //    zeroed by free_edge_chain). The freelist bins track offset+size, not the
    //    bytes — a future reuse overwrites these zeros.
    {
        std::fstream dat(std::filesystem::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::in | std::ios::out);
        if (!dat) throw std::runtime_error("delete_node_from_disk: failed to open nodes.dat for zeroing.");
        zero_region(dat, node_idx.offset, record_size);
        zero_region(dat, node_idx.relation_offset, relation_list_total_size);
    }

    // 7. Tombstone the slot in nodes.idx: the entry stays (the id is reusable via the freelist),
    //    but type_id=TOMBSTONE and the offsets are zeroed. A later reuse (write_node_in_freed_slot)
    //    rewrites the whole NodeIndex in place, clearing the tombstone naturally.
    {
        std::fstream idx(std::filesystem::path(DB_PATH) / "nodes.idx", std::ios::binary | std::ios::in | std::ios::out);
        if (!idx) throw std::runtime_error("delete_node_from_disk: failed to open nodes.idx for tombstoning.");
        idx.seekp(static_cast<std::streamoff>(node_index_offset));
        NodeIndex tomb;
        tomb.id              = node_id;
        tomb.offset          = 0;
        tomb.relation_offset = 0;
        tomb.type_id         = NodeType::TOMBSTONE;
        write_pod(tomb, idx);
    }

    // 8. Update the meta counters. write_meta is left to the caller (Graph::delete_node),
    //    consistent with the add_edge pattern. free_count is a global count of free node
    //    slots across all freelist bins (+1 here, -1 on reuse in Graph::insert).
    meta.node_count--;
    meta.free_count++;
    // free_edge_count was already bumped per freed edge by free_edge_chain (step 5b).

    // COMPLEX is now fully handled above (real record size from the header, JSON sidecar
    // removed + prog_number recycled, `complex_<size>` bins). Inbound-edge cleanup lives in
    // Graph::delete_node, driven by the reverse index (build_inbound_index).
}

std::unordered_map<int, std::unordered_set<int>> build_inbound_index(uint64_t next_id)
{
    namespace fs = std::filesystem;
    std::unordered_map<int, std::unordered_set<int>> in_edges;

    std::ifstream idx_in(fs::path(DB_PATH) / "nodes.idx", std::ios::binary);
    if (!idx_in) return in_edges; // no nodes persisted yet

    std::ifstream dat_in(fs::path(DB_PATH) / "nodes.dat", std::ios::binary);
    std::ifstream edges_in(fs::path(DB_PATH) / "edges.dat", std::ios::binary);

    for (uint64_t id = 0; id < next_id; ++id)
    {
        idx_in.clear();
        idx_in.seekg(static_cast<std::streamoff>(id * sizeof(NodeIndex)));
        NodeIndex ni = read_node_index(idx_in);
        if (!idx_in) break; // short / truncated index — stop defensively

        if (ni.type_id == NodeType::TOMBSTONE) continue; // deleted slot: no live edges

        // Follow only this LIVE node's relation chunks, so zeroed/freed edge
        // regions are never read. from_node == id for every edge it owns.
        dat_in.clear();
        dat_in.seekg(static_cast<std::streamoff>(ni.relation_offset));
        std::vector<RelationEntry> entries = read_relation_node_list(dat_in);

        for (const auto &entry : entries)
        {
            // Walk the edge linked list from its head, hopping by next_offset.
            uint64_t off = entry.edge_offset;
            for (uint64_t i = 0; i < entry.edge_count; ++i)
            {
                edges_in.clear();
                edges_in.seekg(static_cast<std::streamoff>(off));
                Edge e = read_pod<Edge>(edges_in);
                in_edges[static_cast<int>(e.to_node)].insert(static_cast<int>(e.from_node));
                off = e.next_offset;
            }
        }
    }

    logger.info("build_inbound_index: built reverse index over " + std::to_string(in_edges.size()) + " target node(s).");
    return in_edges;
}