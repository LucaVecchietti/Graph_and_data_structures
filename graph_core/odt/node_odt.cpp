#include "node_odt.h"
#include "../io/graph_io.h"
#include "../struct/pod_struct.h"
#include "../logger.h"
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
     RelationNodeList list;
     list.type_count = node.neighborgs.size(); // Count the number of relation types
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
NodeRecord<ComplexHeader> complex_node_to_record(const Node<ComplexRecord> &node)
{
    JsonMeta meta_json; 

    try {
        meta_json = read_json_attributes_meta();    // Read the JSON attributes metadata to get the expected attributes for the complex type
    }
    catch (const std::exception &e) {
        // Handle the error, e.g., log it and return an empty record or rethrow
        Logger logger("node_odt.log", LogLevel::ERROR);
        logger.error("Failed to read JSON attributes meta: " + std::string(e.what()));
        throw; // Rethrow the exception after logging
    }

    // Construct the ComplexHeader.json_file_path based on the metadata and the type label of the node
    // the base path for JSON attributes files is defined in costants.h as JSON_ATTR_PATH and is used by the graph_io functions to write and read the JSON attributes of the complex nodes.
    std::string json_file_path = meta_json.prog_number + "_" + node.data.type_label + ".json";

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
std::unordered_map<std::string, std::unordered_map<int, std::pair<int, BaseNode *>>> reconstruct_neighbors(const RelationNodeList &rellist)
{
    std::unordered_map<std::string, std::unordered_map<int, std::pair<int, BaseNode *>>> neighbors;
    
    

    return neighbors;
}
