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
void write_complex(const ComplexRecord &record, const std::string &json_file_path, std::ofstream &dat_out)
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
    RelationNodeList header = read_pod<RelationNodeList>(in);
    std::vector<RelationEntry> entries;
    entries.reserve(header.type_count);
    for (uint64_t i = 0; i < header.type_count; ++i)
    {
        RelationEntry entry;
        entry.name        = read_string(in);
        entry.edge_offset = read_offset(in);
        entry.edge_count  = read_offset(in);
        entries.push_back(std::move(entry));
    }
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
 * Updates the edges of a node on the disk when a new edge is added to the node in memory.
 * This function delete the current edges batch and rewrites it whitch the new edges added to the node
 * ina  new position on the disk, then it updates the relation list of the node tu poitn to the new position
 * and finnealy add the free offstet to the nodes freelist to be reused.
 * @param node The node whose edges need to be updated.
 * @param meta The metadata containing information about the graph state.
 * @param node_id The ID of the node whose edges need to be updated.
 */
void update_node_edges(BaseNode &node, const MetaRecord &meta, uint64_t node_id)
{
    (void)meta; // Reserved for future use (e.g. updating free_count when the
                // freelist persistence lands; not needed yet).

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
        RelationNodeList old_header = read_pod<RelationNodeList>(dat_in);
        old_relation_total_size = sizeof(RelationNodeList) + old_header.batch_size;

        // read_relation_node_list expects the stream positioned at the POD
        // start, so rewind before delegating to it for the tail entries.
        dat_in.seekg(static_cast<std::streamoff>(node_idx.relation_offset));
        old_entries = read_relation_node_list(dat_in);
    }

    // ---- 2. Orphan the OLD regions ----------------------------------------
    // TODO(freelist follow-up): push these (offset, size) pairs onto the real
    // freelists (db/relation_lists_freelist.dat, db/edges_freelist.dat) via
    // write_free_offset — the same path delete_node_from_disk now uses — so
    // subsequent writes can reuse the holes. For now update_node_edges still
    // only logs the orphaning, so these regions accumulate as dead bytes in
    // nodes.dat / edges.dat until this is wired in.
    logger.info("update_node_edges: orphaning RelationNodeList at offset "
                + std::to_string(node_idx.relation_offset)
                + " of size " + std::to_string(old_relation_total_size)
                + " bytes (nodes.dat)");
    for (const auto &entry : old_entries)
    {
        uint64_t chunk_size = entry.edge_count * sizeof(Edge);
        logger.info("update_node_edges: orphaning edge chunk for relation \""
                    + entry.name + "\" at offset " + std::to_string(entry.edge_offset)
                    + " of size " + std::to_string(chunk_size) + " bytes (edges.dat)");
    }

    // ---- 3. Write the NEW edge chunks and the NEW relation list ------------
    // Per-relation: append all current edges to edges.dat as one contiguous
    // chunk, capture its edge_offset, then emit the matching tail entry in
    // the new relation list region in nodes.dat. The new RelationNodeList
    // POD is written up-front (batch_size is pre-computed via the ODT bridge
    // node_to_relation_list, which sums 24 + name.size() over all relations).
    uint64_t new_relation_offset = 0;
    {
        std::ofstream dat_out(fs::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::app);
        if (!dat_out)
        {
            logger.error("update_node_edges: failed to open nodes.dat for writing.");
            throw std::runtime_error("update_node_edges: failed to open nodes.dat for writing.");
        }
        dat_out.seekp(0, std::ios::end);
        new_relation_offset = dat_out.tellp();

        RelationNodeList list = node_to_relation_list(node);
        write_pod(list, dat_out);

        std::ofstream edges_out(fs::path(DB_PATH) / "edges.dat", std::ios::binary | std::ios::app);
        if (!edges_out)
        {
            logger.error("update_node_edges: failed to open edges.dat for writing.");
            throw std::runtime_error("update_node_edges: failed to open edges.dat for writing.");
        }

        // Each edge carries its own globally-unique id in EdgeRef.id (assigned
        // from MetaRecord.next_edge_id by add_edge and preserved across these
        // full-node rewrites), so we write that id directly into the Edge POD.
        for (const auto &[rel_type, neighbors] : node.neighborgs)
        {
            edges_out.seekp(0, std::ios::end);
            uint64_t edge_offset = edges_out.tellp();
            for (const auto &[to_id, ref] : neighbors)
            {
                Edge edge = edge_to_pod(ref.id, node_id,
                                       static_cast<uint64_t>(to_id),
                                       static_cast<uint64_t>(ref.weight));
                write_pod(edge, edges_out);
            }
            uint64_t edge_count = static_cast<uint64_t>(neighbors.size());
            write_string(rel_type, dat_out);
            write_offset(edge_offset, dat_out);
            write_offset(edge_count, dat_out);
        }
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
        RelationNodeList header = read_pod<RelationNodeList>(dat_in);
        relation_list_total_size = sizeof(RelationNodeList) + header.batch_size;

        // read_relation_node_list expects the stream at the POD start, so rewind before delegating.
        dat_in.seekg(static_cast<std::streamoff>(node_idx.relation_offset));
        relation_entries = read_relation_node_list(dat_in);
    }

    logger.debug("delete_node_from_disk: read " + std::to_string(relation_entries.size()) + " relation entries for node " + std::to_string(node_id));

    // 3. Log the offsets and sizes of the regions to be orphaned by this deletion.
    uint64_t record_size = node_record_payload_size(node_idx.type_id);
    logger.info("delete_node_from_disk: orphaning NodeRecord at offset "
                + std::to_string(node_idx.offset)
                + " of size " + std::to_string(record_size) + " bytes (nodes.dat)");

    logger.info("delete_node_from_disk: orphaning RelationNodeList at offset "
                + std::to_string(node_idx.relation_offset)
                + " of size " + std::to_string(relation_list_total_size) + " bytes (nodes.dat)");

    // 4. Build the free-offset records for the orphaned regions.
    //    All three structs carry { idx?, offset, size } — initialise every field explicitly so the
    //    aggregate initialisation cannot silently zero-fill a trailing member.
    //      - NodeFreeOffset.idx       = node_id        (the now-reusable nodes.idx slot)
    //      - BatchOfEdgesFreeOffset.idx = first edge id of the chunk (read from edges.dat below)
    NodeFreeOffset record_free_offset{node_id, node_idx.offset, record_size};
    RelationNodeListFreeOffset relation_list_free_offset{node_idx.relation_offset, relation_list_total_size};
    std::vector<BatchOfEdgesFreeOffset> edge_chunk_free_offsets;
    edge_chunk_free_offsets.reserve(relation_entries.size());

    {
        std::ifstream edges_in(std::filesystem::path(DB_PATH) / "edges.dat", std::ios::binary);
        if (!edges_in)
        {
            logger.error("delete_node_from_disk: failed to open edges.dat for reading.");
            throw std::runtime_error("delete_node_from_disk: failed to open edges.dat for reading.");
        }
        for (const auto &entry : relation_entries)
        {
            // The chunk's first edge id is the reusable starting id of the batch. A persisted
            // relation always has at least one edge, but guard against an empty chunk anyway.
            uint64_t first_edge_id = 0;
            if (entry.edge_count > 0)
            {
                edges_in.seekg(static_cast<std::streamoff>(entry.edge_offset));
                Edge first = read_pod<Edge>(edges_in);
                first_edge_id = first.id;
            }
            edge_chunk_free_offsets.push_back(
                BatchOfEdgesFreeOffset{first_edge_id, entry.edge_offset, entry.edge_count * sizeof(Edge)});
        }
    }

    logger.info("delete_node_from_disk: adding NodeRecord free offset to freelist: idx=" + std::to_string(record_free_offset.idx) + ", offset=" + std::to_string(record_free_offset.offset) + ", size=" + std::to_string(record_free_offset.size));
    logger.info("delete_node_from_disk: adding RelationNodeList free offset to freelist: offset=" + std::to_string(relation_list_free_offset.offset) + ", size=" + std::to_string(relation_list_free_offset.size));
    for (const auto &edge_free_offset : edge_chunk_free_offsets)
    {
        logger.info("delete_node_from_disk: adding edge chunk free offset to freelist: idx=" + std::to_string(edge_free_offset.idx) + ", offset=" + std::to_string(edge_free_offset.offset) + ", size=" + std::to_string(edge_free_offset.size));
    }

    // 5. Push each free offset onto its EXACT-SIZE bin under db/freelist/.
    //    The bin is selected by the region size, so a later insert/update of the
    //    same size pops an exact fit in O(1) (see freelist_bin_path / pop_free_offset).
    write_free_offset(record_free_offset, freelist_bin_path("nodes", record_free_offset.size));
    write_free_offset(relation_list_free_offset, freelist_bin_path("rel", relation_list_free_offset.size));
    for (const auto &edge_free_offset : edge_chunk_free_offsets)
    {
        write_free_offset(edge_free_offset, freelist_bin_path("edges", edge_free_offset.size));
    }

    // 6. Zero the orphaned regions on disk (no dead bytes / no leak): the NodeRecord and
    //    RelationNodeList in nodes.dat, and every edge chunk in edges.dat. The content is no
    //    longer needed — the freelist bins track offset+size, not the bytes — and a future
    //    reuse will overwrite these zeros.
    {
        std::fstream dat(std::filesystem::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::in | std::ios::out);
        if (!dat) throw std::runtime_error("delete_node_from_disk: failed to open nodes.dat for zeroing.");
        zero_region(dat, node_idx.offset, record_size);
        zero_region(dat, node_idx.relation_offset, relation_list_total_size);
    }
    if (!edge_chunk_free_offsets.empty())
    {
        std::fstream edges(std::filesystem::path(DB_PATH) / "edges.dat", std::ios::binary | std::ios::in | std::ios::out);
        if (!edges) throw std::runtime_error("delete_node_from_disk: failed to open edges.dat for zeroing.");
        for (const auto &e : edge_chunk_free_offsets) zero_region(edges, e.offset, e.size);
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
    meta.free_edge_count += edge_chunk_free_offsets.size(); // one freelist record per orphaned chunk

    // TODO(BUG-016 — remaining steps): inbound edges from OTHER nodes that point at node_id
    // are still left dangling (only the node's OUTBOUND adjacency is reclaimed here), and the
    // variable-width COMPLEX payload is not handled (record_size is header-only for COMPLEX,
    // and its JSON sidecar is not removed).
}