#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <stdexcept>
#include "logger.h"
#include "costants.h"
#include "struct/functions_policies.h"
#include "struct/domain_struct.h"
#include "struct/pod_struct.h"
#include "io/graph_io.h"

/**
 * Graph data structure is build to store differente type of data in an efficente way and to seva relation.
 */

class Graph
{

private:
    std::unordered_map<int, BaseNode *> nodes; // Owns all nodes — responsible for their lifetime

    MetaRecord meta; // Metadata for disk storage management

    Logger logger = Logger("graph.log", LogLevel::DEBUG); // Logger instance for debugging and info

    void init_meta();
    void load_meta();

public:
    Graph();// Basic constructor

    ~Graph(); // Destructor — frees all heap-allocated nodes

    /**
     * Creates a new typed node, assigns its value and appends it to the node list.
     * The index of the node in the vector acts as its ID.
     * Insert with move — avoid copies of large data structures.
     */
    template <typename T>
    void insert(T &&value) // forward reference
    {
        using ValueType = std::remove_reference_t<T>; // Strip reference in case of a pure type (eg. T=int& --strip-> T = int)

        Node<ValueType> *newNode = new Node<ValueType>(); // Create a new node
        newNode->data = std::forward<T>(value);                // Assign the input value and mantain lvalue\rvalue

        nodes[meta.next_id] = newNode; // Add to the base nodes map with the next available ID as the key

        write_node(*newNode, meta); // Write the new node to disk

        // Update metadata
        meta.node_count++;
        meta.next_id++; // Increment the next available ID

        // Log the insertion of the new node. std::to_string covers the numeric
        // primitives (and char/bool, returning the numeric code), but not the
        // non-POD ComplexRecord payload — for COMPLEX we surface the runtime
        // type_label instead, which is the most informative tag we have without
        // dumping the JSON attributes blob.
        if constexpr (node_type_of_v<ValueType> == NodeType::COMPLEX)
        {
            logger.info("Inserted node with ID " + std::to_string(meta.next_id - 1)
                        + " of complex type \"" + newNode->data.type_label + "\"");
        }
        else
        {
            logger.info("Inserted node with ID " + std::to_string(meta.next_id - 1)
                        + " and value " + std::to_string(newNode->data));
        }

        write_meta(meta);
    }

    /**
     * Adds a directed edge from start to end with an optional type and weight.
     * The edge is stored under the given relation type in the adjacency map.
     */
    void add_edge(int start, int end, std::string type = "", int weight = 1);

    /**
     * Generic graph traversal — behavior determined by Policy at compile time.
     * NodeFn: callback(int nodeIdx)            — fired when a node is first visited
     * EdgeFn: callback(int from, int to, int weight) — fired for every edge explored
     */
    template <typename Policy, typename NodeFn, typename EdgeFn>
    void traverse(int start, const std::string &type, NodeFn &&on_node, EdgeFn &&on_edge)
    {
        std::unordered_set<int> visited;
        typename Policy::Frontier frontier;

        // Marks a node as visited, fires the node callback, pushes to frontier
        auto visit = [&](int idx)
        {
            visited.insert(idx);
            on_node(idx);
            Policy::push(frontier, idx);
        };

        visit(start);

        while (!Policy::empty(frontier))
        {
            int current = Policy::pop(frontier);

            // save some time by looking up the node once instead of per edge
            auto nodeIt = nodes.find(current);
            if (nodeIt == nodes.end())
                continue; // nodo non esiste

            auto it = nodeIt->second->neighborgs.find(type);
            if (it == nodeIt->second->neighborgs.end())
                continue;

            for (auto &[neighborgIdx, edge] : it->second)
            {
                on_edge(current, neighborgIdx, edge.weight);
                if (visited.find(neighborgIdx) == visited.end())
                    visit(neighborgIdx);
            }
        }
    }

    // Convenience wrappers — select policy without exposing it in the call site
    template <typename NodeFn, typename EdgeFn>
    void bfs(int start, const std::string &type, NodeFn &&on_node, EdgeFn &&on_edge)
    {
        traverse<BFSPolicy>(start, type, std::forward<NodeFn>(on_node), std::forward<EdgeFn>(on_edge));
    }

    template <typename NodeFn, typename EdgeFn>
    void dfs(int start, const std::string &type, NodeFn &&on_node, EdgeFn &&on_edge)
    {
        traverse<DFSPolicy>(start, type, std::forward<NodeFn>(on_node), std::forward<EdgeFn>(on_edge));
    }
};