#include <filesystem>
#include "../logger.h"
#include "../costants.h"
#include "graph_io.h"
#include "../odt/node_odt.h"
#include "../struct/pod_struct.h"

Logger logger = Logger("graph_io.log", LogLevel::DEBUG); // Logger instance for debugging and info

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

BaseNode read_node(const uint64_t &idx)
{
    
    NodeIndex node_idx = read_node_index(idx);
    NodeRecord record = read_node_record(node_idx.offset);
    std::vector<RelationEntry> rel_list = read_relation_node_list(node_idx.relation_offset);

    BaseNode node = node_form_pod();
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