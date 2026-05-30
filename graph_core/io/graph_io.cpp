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
    // TODO(BUG-001 follow-up): push these (offset, size) pairs onto a real
    // freelist (db/freelist.dat) so subsequent writes can reuse the holes.
    // The FreeRecord POD and MetaRecord.free_count are already defined for
    // this purpose but the persistence path is not implemented yet. Until
    // then we just log the orphaning so the leak is observable in graph_io.log
    // and the regions accumulate as dead bytes in nodes.dat / edges.dat.
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

        // edge_idx is per-node-local (matches the semantics of write_relation_node_list).
        // Globally unique edge ids are tracked separately as BUG-002 — out of scope here.
        uint64_t edge_idx = 0;
        for (const auto &[rel_type, neighbors] : node.neighborgs)
        {
            edges_out.seekp(0, std::ios::end);
            uint64_t edge_offset = edges_out.tellp();
            for (const auto &[to_id, wp] : neighbors)
            {
                Edge edge = edge_to_pod(edge_idx++, node_id,
                                       static_cast<uint64_t>(to_id),
                                       static_cast<uint64_t>(wp.first));
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