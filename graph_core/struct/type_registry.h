#pragma once

#include "pod_struct.h"

// Compile-time mapping: C++ type → NodeType tag stored on disk.
// Adding a new type: add a NodeType enumerator and a specialization below.

template <typename T>
struct node_type_of; // intentionally undefined — triggers a clear error for unsupported types

template <> struct node_type_of<int>    { static constexpr NodeType value = NodeType::INT;    };
template <> struct node_type_of<float>  { static constexpr NodeType value = NodeType::FLOAT;  };
template <> struct node_type_of<double> { static constexpr NodeType value = NodeType::DOUBLE; };
template <> struct node_type_of<char>   { static constexpr NodeType value = NodeType::CHAR;   };
template <> struct node_type_of<bool>   { static constexpr NodeType value = NodeType::BOOL;   };

template <typename T>
constexpr NodeType node_type_of_v = node_type_of<T>::value;
