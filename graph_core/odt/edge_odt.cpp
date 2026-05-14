#include "edge_odt.h"

// ---- Edge ODT ----
/**
 * This file implement the ODT (Object Data Transfer) functions to translate the domain stract to POD stract and vice versa,
 * to mantain the efficente of the graph and to be able to save it on the disc.
 */

/**
 * Translate the adjacency information from map to a list of edges for serialization.
 * The edges are stored as a list of Edge POD structs, which contain the weight and the
 * offset to the destination node. The relation type is stored separately in the RelationNodeList.
 */
Edge edge_to_pod(uint64_t idx, uint64_t from, uint64_t to, uint64_t weight)
{
    Edge edge;
    edge.id = idx; // Unique ID for the edge, can be generated based on from/to or a global counter
    edge.weight = weight;
    edge.to_node = to; // Offset to the destination node's record in the nodes.idx file
    edge.from_node = from; // Offset to the source node's record in the nodes.idx file
    return edge;
}