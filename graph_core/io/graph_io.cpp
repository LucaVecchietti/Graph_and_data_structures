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

/**
 * Reads a whole relation-list CHAIN starting at the stream's current get position.
 * See graph_io.h for the contract (including the optional batch_offsets out-param).
 */
std::vector<RelationEntry> read_relation_node_list(std::ifstream &in, std::vector<uint64_t> *batch_offsets)
{
    std::vector<RelationEntry> entries;
    uint64_t batch_off = static_cast<uint64_t>(in.tellg());

    for (uint32_t visited = 0;; ++visited)
    {
        // The format has no checksum: a corrupted next_offset must not spin forever.
        if (visited >= RELATION_MAX_BATCHES)
        {
            logger.error("read_relation_node_list: relation chain exceeds "
                         + std::to_string(RELATION_MAX_BATCHES) + " batches at offset "
                         + std::to_string(batch_off) + " — corrupted next_offset?");
            throw std::runtime_error("read_relation_node_list: relation batch chain too long (corrupted next_offset?).");
        }

        if (batch_offsets) batch_offsets->push_back(batch_off);

        NodeRelationList header = read_pod<NodeRelationList>(in);
        for (uint64_t i = 0; i < header.type_count; ++i)
            entries.push_back(read_relation_line(in));

        if (header.next_offset == 0)
        {
            // Last batch: skip its unused (zero-filled) tail remainder so the stream
            // sits at the end of the chain's last batch region.
            in.seekg(static_cast<std::streamoff>(header.free_bytes), std::ios::cur);
            break;
        }

        // Hop to the next batch of the chain (batches need not be contiguous).
        batch_off = header.next_offset;
        in.clear();
        in.seekg(static_cast<std::streamoff>(batch_off));
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

    // ---- 2. walk the batch chain, locate the relation line -----------------
    // The list is a chain of fixed-width batches (next_offset, head 1->2->3...), so
    // the search hops from batch to batch. It stops early on a hit; otherwise it runs
    // to the LAST batch, which is where a brand-new relation type will be placed
    // (append-only: lines 0..type_count-1 of a batch are used, never a hole).
    int found = -1;               // line index of `type` INSIDE found_batch_off, or -1
    uint64_t found_batch_off = 0; // batch holding that line
    uint64_t old_head = 0;        // current chain head of that relation (0 if none)
    uint64_t old_count = 0;
    NodeRelationList last_header; // header of the last batch of the chain
    uint64_t last_batch_off = relation_offset;
    {
        std::ifstream dat_in(fs::path(DB_PATH) / "nodes.dat", std::ios::binary);
        if (!dat_in) throw std::runtime_error("persist_new_edge: failed to open nodes.dat for reading.");

        uint64_t off = relation_offset;
        for (uint32_t visited = 0;; ++visited)
        {
            if (visited >= RELATION_MAX_BATCHES)
            {
                logger.error("persist_new_edge: node " + std::to_string(node_id)
                             + " relation chain exceeds " + std::to_string(RELATION_MAX_BATCHES)
                             + " batches — corrupted next_offset?");
                throw std::runtime_error("persist_new_edge: relation batch chain too long (corrupted next_offset?).");
            }

            dat_in.clear();
            dat_in.seekg(static_cast<std::streamoff>(off));
            NodeRelationList h = read_pod<NodeRelationList>(dat_in);

            for (uint64_t i = 0; i < h.type_count; ++i)
            {
                RelationEntry e = read_relation_line(dat_in); // sequential after the header
                if (e.name == type)
                {
                    found           = static_cast<int>(i);
                    found_batch_off = off;
                    old_head        = e.edge_offset;
                    old_count       = e.edge_count;
                    break;
                }
            }

            last_header    = h;
            last_batch_off = off;

            if (found >= 0 || h.next_offset == 0) break;
            off = h.next_offset;
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
    bool new_batch = false;
    {
        std::fstream dat(fs::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::in | std::ios::out);
        if (!dat) throw std::runtime_error("persist_new_edge: failed to open nodes.dat for in-place update.");

        if (found >= 0)
        {
            // Existing relation: only head offset + count change (first 16 bytes of the
            // line), inside whichever batch of the chain holds that line.
            dat.seekp(static_cast<std::streamoff>(found_batch_off + sizeof(NodeRelationList)
                                                  + static_cast<uint64_t>(found) * RELATION_LINE_SIZE));
            write_offset(new_off, dat);
            write_offset(old_count + 1, dat);
        }
        else if (last_header.type_count < RELATION_LINES_PER_BATCH)
        {
            // New relation type, and the last batch of the chain still has a free line:
            // fill slot `type_count` and bump the header in place. The batch never moves,
            // so NodeIndex stays untouched.
            uint64_t slot = last_header.type_count;
            dat.seekp(static_cast<std::streamoff>(last_batch_off + sizeof(NodeRelationList)
                                                  + slot * RELATION_LINE_SIZE));
            write_relation_line(dat, new_off, 1, type);

            last_header.type_count += 1;
            last_header.free_bytes -= RELATION_LINE_SIZE;
            dat.seekp(static_cast<std::streamoff>(last_batch_off));
            write_pod(last_header, dat);
        }
        else
        {
            // New relation type and the last batch is FULL: chain a fresh batch. It is
            // allocated on its own (exact-size `rel` bin pop, else append at EOF), gets
            // the next serial in `head`, and is linked in with a single 8-byte in-place
            // write to the previous batch's next_offset. NodeIndex still never moves.
            new_batch = true;
            const uint64_t region = relation_batch_region_size();

            uint64_t new_batch_off;
            auto reuse = pop_free_offset<RelationNodeListFreeOffset>(freelist_bin_path("rel", region));
            if (reuse)
            {
                new_batch_off = reuse->offset;                 // exact fit: every batch is one size class
                dat.seekp(static_cast<std::streamoff>(new_batch_off));
            }
            else
            {
                // in|out (NOT app): seekp to EOF + write correctly extends the file.
                dat.seekp(0, std::ios::end);
                new_batch_off = static_cast<uint64_t>(dat.tellp());
            }

            // Full region in one contiguous run: header + the single line + zero tail.
            write_pod(relation_batch_header(node_id, 1, last_header.head + 1, 0), dat);
            write_relation_line(dat, new_off, 1, type);
            pad_relation_tail(dat, 1);

            // Link it: patch only next_offset of the previously-last batch.
            dat.seekp(static_cast<std::streamoff>(last_batch_off + offsetof(NodeRelationList, next_offset)));
            write_offset(new_batch_off, dat);

            logger.info("persist_new_edge: node " + std::to_string(node_id)
                        + " relation batch full — chained batch head "
                        + std::to_string(last_header.head + 1) + " at nodes.dat offset "
                        + std::to_string(new_batch_off)
                        + (reuse ? " (reused rel bin region)" : " (appended)"));
        }
    }

    logger.info("persist_new_edge: node " + std::to_string(node_id) + " --" + type + "--> "
                + std::to_string(to_id) + " edge id " + std::to_string(edge_id)
                + " at edges.dat offset " + std::to_string(new_off)
                + (found >= 0 ? " (existing relation)"
                              : (new_batch ? " (new relation line, new batch)" : " (new relation line)")));
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
 * in a new position on the disk, then it updates the relation list of the node to point to the new position
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

    // Every batch of the OLD chain is orphaned by this update, so collect their
    // offsets while reading: NodeIndex only records where the chain starts.
    const uint64_t region = relation_batch_region_size();
    std::vector<RelationEntry> old_entries;
    std::vector<uint64_t> old_batch_offsets;
    {
        std::ifstream dat_in(fs::path(DB_PATH) / "nodes.dat", std::ios::binary);
        if (!dat_in)
        {
            logger.error("update_node_edges: failed to open nodes.dat for reading.");
            throw std::runtime_error("update_node_edges: failed to open nodes.dat for reading.");
        }
        dat_in.seekg(static_cast<std::streamoff>(node_idx.relation_offset));
        old_entries = read_relation_node_list(dat_in, &old_batch_offsets);
    }

    // ---- 2. Orphan the OLD regions onto the freelist bins -------------------
    // Free each old relation's edges by walking its chain (edges are no longer
    // contiguous, so free_edge_chain pushes each 48-byte slot onto the single-edge
    // `edges` bin, zeroes it, and bumps free_edge_count). Then push EVERY batch of
    // the old chain onto the `rel` bin (one record per batch — all the same size
    // class) and zero them.
    for (const auto &entry : old_entries)
        free_edge_chain(entry.edge_offset, entry.edge_count, meta);

    for (uint64_t batch_off : old_batch_offsets)
        write_free_offset(RelationNodeListFreeOffset{batch_off, region}, freelist_bin_path("rel", region));
    {
        std::fstream dat_io(fs::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::in | std::ios::out);
        if (dat_io)
            for (uint64_t batch_off : old_batch_offsets)
                zero_region(dat_io, batch_off, region);
    }

    logger.info("update_node_edges: orphaned " + std::to_string(old_batch_offsets.size())
                + " relation batch(es) of " + std::to_string(region) + " bytes starting at offset "
                + std::to_string(node_idx.relation_offset)
                + " -> rel bin; free_edge_count=" + std::to_string(meta.free_edge_count));

    // ---- 3. Write the NEW relation chain and the NEW edge runs -------------
    // The batches are reclaimed pop-then-append: every batch is one size class
    // (2213 B), and step 2 just freed this node's whole old chain onto the `rel`
    // bin, so the pops return those very regions → in-place rewrite, no nodes.dat
    // growth (the chain can only need MORE batches than it had if this call grew
    // the node's relation types, which the shrink-only caller never does). The
    // edge runs are appended fresh at EOF (the old, possibly-scattered edges were
    // freed per-edge in step 2 and are reused later by persist_new_edge).
    const uint64_t n_types   = static_cast<uint64_t>(node.neighborgs.size());
    const uint64_t n_batches = relation_batch_count(n_types);

    std::vector<uint64_t> new_batch_offsets(n_batches, 0);
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

        // 3a. Decide where EVERY batch lands before writing any of them: each
        //     next_offset must point at the following batch, and the batches of one
        //     chain need not be contiguous (each is its own freelist pop). Appended
        //     batches are handed successive EOF+k*region slots — probing EOF again
        //     would return the same offset twice, since nothing is written yet.
        dat_out.seekp(0, std::ios::end);
        uint64_t append_cursor = static_cast<uint64_t>(dat_out.tellp());
        for (uint64_t b = 0; b < n_batches; ++b)
        {
            auto reuse = pop_free_offset<RelationNodeListFreeOffset>(freelist_bin_path("rel", region));
            if (reuse)
            {
                new_batch_offsets[b] = reuse->offset; // in-place reuse (exact fit)
            }
            else
            {
                new_batch_offsets[b] = append_cursor;
                append_cursor += region;
            }
        }
        new_relation_offset = new_batch_offsets[0];

        std::fstream edges_out(fs::path(DB_PATH) / "edges.dat",
                               std::ios::binary | std::ios::in | std::ios::out);
        if (!edges_out)
        {
            logger.error("update_node_edges: failed to open edges.dat for writing.");
            throw std::runtime_error("update_node_edges: failed to open edges.dat for writing.");
        }

        // 3b. This is the whole-node rewrite path (used only by delete_node's inbound
        //     cleanup now — add_edge takes the O(1) persist_new_edge path). Each
        //     relation's edges are re-laid as a fresh CONTIGUOUS doubly-linked run
        //     appended at EOF (the old, possibly-scattered chain was already freed in
        //     step 2). As we write each edge we REFRESH its RAM EdgeRef.offset, so a
        //     later O(1) weight overwrite seeks to the right place after relocation.
        //     Each Edge keeps its own EdgeRef.id.
        //     The reuse fit is exact, so header + full fixed tail land in exactly
        //     `region` bytes — no overrun. dat_out is seeked ONLY at batch boundaries;
        //     inside a batch its put pointer must advance contiguously.
        auto rel_it = node.neighborgs.begin();
        for (uint64_t b = 0; b < n_batches; ++b)
        {
            const uint64_t lines = relation_lines_in_batch(n_types, b);
            const uint64_t next  = (b + 1 < n_batches) ? new_batch_offsets[b + 1] : 0;

            dat_out.seekp(static_cast<std::streamoff>(new_batch_offsets[b]));
            write_pod(relation_batch_header(node_id, lines, b + 1, next), dat_out);

            for (uint64_t i = 0; i < lines; ++i, ++rel_it)
            {
                const std::string &rel_type = rel_it->first;
                auto &neighbors             = rel_it->second;
                uint64_t edge_count         = static_cast<uint64_t>(neighbors.size());

                edges_out.seekp(0, std::ios::end);
                uint64_t base = static_cast<uint64_t>(edges_out.tellp());

                uint64_t k = 0;
                for (auto &[to_id, ref] : neighbors)
                {
                    uint64_t prev = (k == 0)               ? 0 : base + (k - 1) * sizeof(Edge);
                    uint64_t next_e = (k == edge_count - 1) ? 0 : base + (k + 1) * sizeof(Edge);
                    Edge e = edge_to_pod(ref.id, node_id, static_cast<uint64_t>(to_id),
                                         static_cast<uint64_t>(ref.weight), prev, next_e);
                    write_pod(e, edges_out);
                    ref.offset = base + k * sizeof(Edge); // refresh RAM offset after relocation
                    ++k;
                }

                write_relation_line(dat_out, edge_count == 0 ? 0 : base, edge_count, rel_type);
            }

            // Zero-fill the rest of the fixed tail so each region is exactly 2213 bytes.
            pad_relation_tail(dat_out, lines);
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
                + " relation_offset patched to " + std::to_string(new_relation_offset)
                + " (" + std::to_string(n_batches) + " batch(es) for "
                + std::to_string(n_types) + " relation type(s))");
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

    // 2. Read the relation list of the node to be deleted to obtain the offsets and sizes of its edge chunks.
    //    The list is a CHAIN of fixed-width batches, so we also collect the offset of every batch: each one is
    //    an independently-reclaimable region of relation_batch_region_size() bytes (a single `rel` size class),
    //    and NodeIndex only records where the chain starts.
    std::vector<RelationEntry> relation_entries;
    std::vector<uint64_t> relation_batch_offsets;
    const uint64_t relation_batch_region = relation_batch_region_size();
    {
        std::ifstream dat_in(std::filesystem::path(DB_PATH) / "nodes.dat", std::ios::binary);
        if (!dat_in)
        {
            logger.error("delete_node_from_disk: failed to open nodes.dat for reading.");
            throw std::runtime_error("delete_node_from_disk: failed to open nodes.dat for reading.");
        }
        dat_in.seekg(static_cast<std::streamoff>(node_idx.relation_offset));
        relation_entries = read_relation_node_list(dat_in, &relation_batch_offsets);
    }

    logger.debug("delete_node_from_disk: read " + std::to_string(relation_entries.size())
                 + " relation entries for node " + std::to_string(node_id)
                 + " across " + std::to_string(relation_batch_offsets.size()) + " batch(es)");

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

    logger.info("delete_node_from_disk: orphaning " + std::to_string(relation_batch_offsets.size())
                + " relation batch(es) of " + std::to_string(relation_batch_region)
                + " bytes, chain starting at offset " + std::to_string(node_idx.relation_offset) + " (nodes.dat)");

    // 4. Build the free-offset record for the NodeRecord region. The relation batches are
    //    pushed one by one below (every batch of the chain is its own free region of the
    //    same size class). The node's edges are reclaimed separately by free_edge_chain
    //    (step 5b), which walks each relation's linked list and frees the 48-byte slots.
    NodeFreeOffset record_free_offset{node_id, node_idx.offset, record_size};

    logger.info("delete_node_from_disk: adding NodeRecord free offset to freelist: idx=" + std::to_string(record_free_offset.idx) + ", offset=" + std::to_string(record_free_offset.offset) + ", size=" + std::to_string(record_free_offset.size));

    // 5a. Push the NodeRecord region + every relation batch onto their EXACT-SIZE bins.
    write_free_offset(record_free_offset, freelist_bin_path(node_bin_prefix, record_free_offset.size));
    for (uint64_t batch_off : relation_batch_offsets)
    {
        logger.info("delete_node_from_disk: adding relation batch free offset to freelist: offset="
                    + std::to_string(batch_off) + ", size=" + std::to_string(relation_batch_region));
        write_free_offset(RelationNodeListFreeOffset{batch_off, relation_batch_region},
                          freelist_bin_path("rel", relation_batch_region));
    }

    // 5b. Free each relation's edge chain (per-edge 48-byte slots, zeroed) — bumps
    //     free_edge_count internally.
    for (const auto &entry : relation_entries)
        free_edge_chain(entry.edge_offset, entry.edge_count, meta);

    // 6. Zero the orphaned NodeRecord + every relation batch of the chain in nodes.dat
    //    (edges already zeroed by free_edge_chain). The freelist bins track offset+size,
    //    not the bytes — a future reuse overwrites these zeros.
    {
        std::fstream dat(std::filesystem::path(DB_PATH) / "nodes.dat", std::ios::binary | std::ios::in | std::ios::out);
        if (!dat) throw std::runtime_error("delete_node_from_disk: failed to open nodes.dat for zeroing.");
        zero_region(dat, node_idx.offset, record_size);
        for (uint64_t batch_off : relation_batch_offsets)
            zero_region(dat, batch_off, relation_batch_region);
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