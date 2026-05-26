#pragma once

#include "pod_struct.h"
#include "domain_struct.h"

/**
 * type_registry.h defines the compile-time mapping from C++ types to NodeType tags used for on-disk storage.
 * This allows the graph to support nodes with different types of content while maintaining a consistent on-disk format.
 * To add support for a new type, add a new enumerator to the NodeType enum in pod_struct.h and a corresponding specialization of the node_type_of struct template in this file.
 * For example, to add support for std::string, you would:
 * 1. Add STRING = 5 to the NodeType enum in pod_struct.h.
 * 2. Add the following specialization to this file:
 *    template <> struct node_type_of<std::string> { static constexpr NodeType value = NodeType::STRING; };
 * This design allows the graph to be easily extended to support new types without modifying the core logic of reading and writing nodes,
 * as the type information is encapsulated in the NodeType tags and the node_type_of mapping.
 */

// Compile-time mapping: C++ type → NodeType tag stored on disk.
// Adding a new type: add a NodeType enumerator and a specialization below.

template <typename T>
struct node_type_of; // intentionally undefined — triggers a clear error for unsupported types

template <> struct node_type_of<int>    { static constexpr NodeType value = NodeType::INT;    };
template <> struct node_type_of<float>  { static constexpr NodeType value = NodeType::FLOAT;  };
template <> struct node_type_of<double> { static constexpr NodeType value = NodeType::DOUBLE; };
template <> struct node_type_of<char>   { static constexpr NodeType value = NodeType::CHAR;   };
template <> struct node_type_of<bool>   { static constexpr NodeType value = NodeType::BOOL;   };

// COMPLEX: runtime-typed record (type_label + JSON attributes). ComplexRecord
// is NOT POD, so the existing write_pod/NodeRecord<T> path does not cover it —
// the COMPLEX read/write logic must be implemented separately (WIP).
template <> struct node_type_of<ComplexRecord> { static constexpr NodeType value = NodeType::COMPLEX; };

template <typename T>
constexpr NodeType node_type_of_v = node_type_of<T>::value;
