#include "graph.h"
#include "costants.h"
#include "struct/pod_struct.h"
#include "io/graph_io.h"
#include "io/io_utils.h"
#include <filesystem>

Graph::Graph(){
    std::filesystem::create_directories(DB_PATH);

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
    meta.next_id = 0;
    meta.node_count = 0;
    meta.free_count = 0;

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
void Graph::add_edge(int start, int end, std::string type, int weight)
{
    if (type.length() > RELATION_TYPE_MAX_SIZE)
    {
        Graph::logger.error("Failed to add edge: relation type '" + type + "' exceeds maximum allowed size of " + std::to_string(RELATION_TYPE_MAX_SIZE) + " characters.");
        throw std::invalid_argument(
            "The size of the sring '" + type + "' is over the limit of " +
            std::to_string(RELATION_TYPE_MAX_SIZE) + " characters by " + std::to_string(type.length() - RELATION_TYPE_MAX_SIZE) + " characters.");
    }

    if (nodes.find(start) == nodes.end())
    {   
        if (static_cast<uint64_t>(start) < meta.next_id)
        {
            try{
                nodes[start] = read_node(static_cast<uint64_t>(start));
            }
            catch (const std::exception &e)
            {
                Graph::logger.error("Failed to add edge: could not read node " + std::to_string(start) + ": " + e.what());
                throw std::runtime_error("Failed to read node " + std::to_string(start) + ": " + e.what());
            }
        }
        else
        {
            Graph::logger.error("Failed to add edge: node " + std::to_string(start) + " does not exist.");
            throw std::out_of_range("Node " + std::to_string(start) + " does not exist.");
        }
    }

    if (nodes.find(end) == nodes.end())
    {
        if (static_cast<uint64_t>(end) < meta.next_id)
        {
            try{
                nodes[end] = read_node(static_cast<uint64_t>(end));
            }
            catch (const std::exception &e)
            {
                Graph::logger.error("Failed to add edge: could not read node " + std::to_string(end) + ": " + e.what());
                throw std::runtime_error("Failed to read node " + std::to_string(end) + ": " + e.what());
            }
        }
        else
        {
            Graph::logger.error("Failed to add edge: node " + std::to_string(end) + " does not exist.");
            throw std::out_of_range("Node " + std::to_string(end) + " does not exist.");
        }
    }

    BaseNode *node = nodes[start];                              // Get the start node from the base nodes vector
    auto edge = std::pair<int, BaseNode *>(weight, nodes[end]); // Create the edge and assign the weight

    logger.info("Adding edge from node " + std::to_string(start) + " to node " + std::to_string(end) + " with type '" + type + "' and weight " + std::to_string(weight));

    // Add the edge and the near node to the neighborgs based on the type of the relation
    node->neighborgs[type][end] = edge;
}