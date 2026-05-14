#pragma once

#include <vector>
#include <string>
#include <stdexcept>
#include "costants.h"
#include "struct/functions_policies.h"
#include "struct/domain_struct.h"
#include "struct/pod_struct.h"

/**
 * Graph data structure is build to store differente type of data in an efficente way and to seva relation.
 */

class Graph
{

private:
    std::vector<BaseNode *> nodes; // Owns all nodes — responsible for their lifetime

    MetaRecord meta; // Metadata for disk storage management

    void init_meta();
    void load_meta();

public:
    Graph();// Basic constructor

    virtual ~Graph()
    { // Destructor — frees all heap-allocated nodes
        for (auto node : nodes)
        {
            delete node;
        }
    }

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

        nodes.push_back(newNode); // Add to the base nodes vector
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
    void traverse(int start, const std::string &type, NodeFn &&on_node, EdgeFn on_edge)
    {
        std::vector<bool> visited(nodes.size(), false);
        typename Policy::Frontier frontier;

        // Marks a node as visited, fires the node callback, pushes to frontier
        auto visit = [&](int idx)
        {
            visited[idx] = true;
            on_node(idx);
            Policy::push(frontier, idx);
        };

        visit(start);

        while (!Policy::empty(frontier))
        {

            int current = Policy::pop(frontier);

            auto it = nodes[current]->neighborgs.find(type);
            if (it == nodes[current]->neighborgs.end())
                continue;

            for (auto &[neighborgIdx, edge] : it->second)
            {
                on_edge(current, neighborgIdx, edge.first); // fired for every edge, visited or not
                if (!visited[neighborgIdx])
                    visit(neighborgIdx);
            }
        }
    }

    // Convenience wrappers — select policy without exposing it in the call site
    template <typename NodeFn, typename EdgeFn>
    void bfs(int start, const std::string &type, NodeFn &&on_node, EdgeFn &&on_edge)
    {
        traverse<BFSPolicy>(start, type, forward<NodeFn>(on_node), forward<EdgeFn>(on_edge));
    }

    template <typename NodeFn, typename EdgeFn>
    void dfs(int start, const std::string &type, NodeFn &&on_node, EdgeFn &&on_edge)
    {
        traverse<DFSPolicy>(start, type, forward<NodeFn>(on_node), forward<EdgeFn>(on_edge));
    }
};