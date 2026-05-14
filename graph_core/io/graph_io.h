#pragma once 

#include "../struct/pod_struct.h"
#include "../struct/domain_struct.h"
#include "io_utils.h"

// Graph I/O header — defines functions for saving and loading the graph to/from disk in a binary format.

// ---- I/O Nodes ----

/**
 * Write Node, which consists of a NodeIndex, a NodeRecord, and a RelationNodeList, to the output stream. 
 */
template <typename T>
void write_node(const Node<T> &node, std::ofstream &out);

/**
 * Writes a NodeIndex struct to the output stream.
 */
void write_node_index(const NodeIndex &idx, std::ofstream &out);

template <typename T>
void write_node_record(const NodeRecord<T> &record, std::ofstream &out);

NodeIndex read_node_index(std::ifstream &in);

template <typename T>
NodeRecord<T> read_node_record(std::ifstream &in);

// ---- I/O Edges ----

void write_relation_node_list(const RelationNodeList &list, std::ofstream &out);

RelationNodeList read_relation_node_list(std::ifstream &in);