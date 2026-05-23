#include "graph.h"
#include "costants.h"
#include "struct/pod_struct.h"
#include "io/graph_io.h"
#include "io/io_utils.h"
#include <filesystem>

Graph::Graph(){
    if (!std::filesystem::exists(std::filesystem::path(DB_PATH) / "meta.dat") || std::filesystem::file_size(std::filesystem::path(DB_PATH) / "meta.dat") == 0)
    {
        init_meta();
    }
    else
    {
        load_meta();
    }
} // Basic constructor

Graph::~Graph(){
    for (auto [idx, node] : nodes)
    {
        delete node;
    }
} // Destructor — frees all heap-allocated nodes

// ---- Private Methods ----

void Graph::init_meta()
{
    // Initialize metadata on disk, such as next available node ID and node count.
    // This is necessary for maintaining the integrity of the graph across sessions.

    MetaRecord meta;
    meta.next_id = 0; // Start with ID 0 for the first node
    meta.node_count = 0; // No nodes initially
    meta.free_count = 0; // No free offsets initially

    write_meta(meta);
}

void Graph::load_meta()
{
    // Load metadata from disk, such as next available node ID and node count.
    // This is necessary for maintaining the integrity of the graph across sessions.

    Graph::meta = read_meta();
}

// ---- Public Methods ----

/**
 * Adds a directed edge from start to end with an optional type and weight.
 * The edge is stored under the given relation type in the adjacency map.
 */
void Graph::add_edge(int start, int end, std::string type, int weight = 1)
{
    if (type.length() > RELATION_TYPE_MAX_SIZE)
    {
        throw std::invalid_argument(
            "The size of the sring '" + type + "' is over the limit of " +
            std::to_string(RELATION_TYPE_MAX_SIZE) + " characters by " + std::to_string(type.length() - RELATION_TYPE_MAX_SIZE) + " characters.");
    }

    if (nodes.find(start) == nodes.end() || nodes.find(end) == nodes.end())
    {
        throw std::out_of_range("Start or end node does not exist.");
    }

    BaseNode *node = nodes[start];                              // Get the start node from the base nodes vector
    auto edge = std::pair<int, BaseNode *>(weight, nodes[end]); // Create the edge and assign the weight

    // Add the edge and the near node to the neighborgs based on the type of the relation
    node->neighborgs[type][end] = edge;
}