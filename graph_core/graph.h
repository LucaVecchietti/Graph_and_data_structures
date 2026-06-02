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

        uint64_t node_id;   // the id actually assigned (recycled or freshly minted)
        bool reused = false;

        // Reuse path: try to pop a freed slot whose record size matches exactly.
        // Only for fixed-size primitive payloads — COMPLEX has a variable on-disk
        // size, so it can never exact-fit a bin and always takes the append path.
        // (The if constexpr also keeps write_node_in_freed_slot<ComplexRecord>
        // from ever being instantiated, which would fail node_to_record's assert.)
        if constexpr (node_type_of_v<ValueType> != NodeType::COMPLEX)
        {
            uint64_t needed = node_record_payload_size(node_type_of_v<ValueType>);
            std::optional<NodeFreeOffset> slot = pop_free_offset<NodeFreeOffset>(freelist_bin_path("nodes", needed));
            if (slot)
            {
                node_id = slot->idx;            // recycle the freed id slot
                nodes[node_id] = newNode;
                write_node_in_freed_slot(*newNode, node_id, slot->offset);
                meta.node_count++;              // next_id is NOT bumped — the id was reused
                reused = true;
            }
        }

        // Append path: no fitting hole (or COMPLEX) → mint a fresh id at the end.
        if (!reused)
        {
            node_id = meta.next_id;
            nodes[node_id] = newNode;
            write_node(*newNode, meta);         // append; write_node uses meta.next_id as the id
            meta.node_count++;
            meta.next_id++;
        }

        // Log the insertion of the new node. std::to_string covers the numeric
        // primitives (and char/bool, returning the numeric code), but not the
        // non-POD ComplexRecord payload — for COMPLEX we surface the runtime
        // type_label instead, which is the most informative tag we have without
        // dumping the JSON attributes blob.
        if constexpr (node_type_of_v<ValueType> == NodeType::COMPLEX)
        {
            logger.info("Inserted node with ID " + std::to_string(node_id)
                        + " of complex type \"" + newNode->data.type_label + "\"");
        }
        else
        {
            logger.info("Inserted node with ID " + std::to_string(node_id)
                        + (reused ? " (reused slot)" : "") + " and value " + std::to_string(newNode->data));
        }

        write_meta(meta);
    }

    /**
     * Adds a directed edge from start to end with an optional type and weight.
     * The edge is stored under the given relation type in the adjacency map.
     * @param start The ID of the start node.
     * @param end The ID of the end node.
     * @param type The relation type of the edge (e.g., "road", "train"). Optional, defaults to an empty string.
     * @param weight The weight of the edge. Optional, defaults to 1.
     * @throws std::invalid_argument if the relation type exceeds the maximum allowed size.
     * @throws std::out_of_range if either the start or end node does not exist
     */
    void add_edge(int start, int end, std::string type = "", int weight = 1);

    /**
     * Deletes a node and all its associated edges from the graph.
     * @param node_id The id of the node to delete.
     */
    void delete_node(int node_id); 

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