#pragma once 

#include "../struct/pod_struct.h"
#include "../struct/domain_struct.h"

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
Edge edge_to_pod(uint64_t idx, uint64_t from, uint64_t to, uint64_t weight);