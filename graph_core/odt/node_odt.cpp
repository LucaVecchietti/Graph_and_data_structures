#include "node_odt.h"
#include "../io/graph_io.h"
#include "../struct/pod_struct.h"
#include "../logger.h"

// TU-local logger: wrapped in an anonymous namespace so the symbol has internal
// linkage and does not collide with the `logger` defined at the same scope in
// graph_io.cpp (and any other TU that follows this pattern).
namespace {
    Logger logger("node_odt.log", LogLevel::DEBUG);
}

// ---- Node ODT ----
/**
 * This file implement the ODT (Object Data Transfer) functions to translate the domain stract to POD stract and vice versa,
 * to mantain the efficente of the graph and to be able to save it on the disc.
 */

// ---- Domain to POD ----

/**
 * Translates a typed Node struct to a NodeRecord POD struct for serialization.
 * The NodeRecord contains only the data payload, while the adjacency information is stored separately in the RelationNodeList.
 */

/**
 * Translates a typed Node struct to a RelationNodeList POD struct for serialization.
 * The RelationNodeList contains the adjacency information (relation types and neighbor offsets),
 * while the data payload is stored separately in the NodeRecord.
 */

 RelationNodeList node_to_relation_list(const BaseNode &node)
 {
     // Pre-compute the size of the variable-width tail that will be written
     // immediately after this POD by the I/O layer. Per-entry layout is
     //   [uint64_t name_length][name bytes][uint64_t edge_offset][uint64_t edge_count]
     // so each entry contributes 24 + name_length bytes.
     uint64_t batch_size = 0;
     for (const auto &[rel_type, neighbors] : node.neighborgs)
     {
         (void)neighbors;
         batch_size += 3 * sizeof(uint64_t) + rel_type.size();
     }

     RelationNodeList list;
     list.type_count = node.neighborgs.size(); // Count the number of relation types
     list.batch_size = batch_size;             // Size in bytes of the tail (see pod_struct.h)
     return list;
 }

/**
 * Translates a NodeRecord POD struct back to a typed Node struct for use in memory.
 * The adjacency information must be reconstructed separately from the RelationNodeList.
 */

NodeIndex node_to_node_index(uint64_t id, uint64_t record_offset, uint64_t relation_offset)
{
    NodeIndex idx;
    idx.id = id;
    idx.offset = record_offset;
    idx.relation_offset = relation_offset;
    return idx;
}

/**
 * This function is used to translate a complex node to a ComplexRecord struct that can be written on the disc as a complex type node.
 * The ComplexRecord struct contains the type label and the json string that contains the attributes of the record in JSON format.
 * The complex type is identical to a classical DB Record, but it require a more complex logic to write and read the record on the DISC, so
 * we need to create a specific function to translate the complex node to a ComplexHeader struct that can be written on the disc using the 
 * NodeRecord struct as a container and writing the type label after the Header and the JSON string on a file. 
 */
NodeRecord<ComplexHeader> complex_node_to_record(const Node<ComplexRecord> &node, std::string &json_file_path)
{
    JsonMeta meta_json; 

    try {
        meta_json = read_json_attributes_meta();    // Read the JSON attributes metadata to get the expected attributes for the complex type
    }
    catch (const std::exception &e) {
        // Handle the error, e.g., log it and return an empty record or rethrow
        logger.error("Failed to read JSON attributes meta: " + std::string(e.what()));
        throw; // Rethrow the exception after logging
    }

    // Decide THIS node's prog_number. Prefer a recycled one from the json free list
    // (a prog_number freed by a previous COMPLEX delete) so numbers stay dense and the
    // counter does not grow unbounded. Only when none is available do we consume — and
    // persist — a fresh value from the monotonic counter (BUG-014: previously the
    // counter was read but never advanced/persisted, so every sidecar collided on 0).
    // Reserve-before-persist: if the persist throws, the number is "consumed" but no
    // two records can ever share it.
    uint64_t prog;
    std::optional<uint64_t> recycled = pop_free_offset<uint64_t>(json_freelist_path());
    if (recycled)
    {
        prog = *recycled;
    }
    else
    {
        prog = meta_json.prog_number;
        meta_json.prog_number = prog + 1;
        write_json_attributes_meta(meta_json);
    }

    // Zero-pad prog_number to COMPLEX_PROG_DIGITS so the sidecar filename — and
    // therefore json_file_path_size, and therefore the whole on-disk record size —
    // has a FIXED width regardless of how many digits prog has. This makes a
    // record's size a pure function of type_label length, so the exact-size
    // freelist bins double as per-type size classes (see costants.h).
    // num.size() <= 20 for any uint64, so the pad count is never negative.
    std::string num = std::to_string(prog);
    std::string padded = std::string(COMPLEX_PROG_DIGITS - num.size(), '0') + num;
    json_file_path = padded + "_" + node.data.type_label + ".json";

    // Construct the ComplexHeader with the type label and JSON attributes
    ComplexHeader header;
    header.type_label_size = node.data.type_label.size();
    header.json_file_path_size = json_file_path.size(); 

    NodeRecord<ComplexHeader> record;
    record.data = header;

    return record;
}

/**
 * 
 */
std::unordered_map<std::string, std::unordered_map<int, EdgeRef>> reconstruct_neighbors(const RelationNodeList &rellist)
{
    std::unordered_map<std::string, std::unordered_map<int, EdgeRef>> neighbors;
    
    

    return neighbors;
}
