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

    meta.edge_count = 0;
    meta.next_edge_id = 0;
    meta.free_edge_count = 0;

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

    BaseNode *node = nodes[start]; // Get the start node from the base nodes vector

    logger.info("Adding edge from node " + std::to_string(start) + " to node " + std::to_string(end) + " with type '" + type + "' and weight " + std::to_string(weight));

    // Decide the edge id and whether this is a brand-new edge or an overwrite of
    // an existing (start, type, end) triple. A new edge consumes a fresh id from
    // meta.next_edge_id; an existing one keeps the id it already has — we only
    // overwrite its weight. Resolved BEFORE the mutation below, which would
    // otherwise create the entry.
    bool is_new_edge = true;
    uint64_t edge_id = meta.next_edge_id;
    {
        auto rel_it = node->neighborgs.find(type);
        if (rel_it != node->neighborgs.end())
        {
            auto edge_it = rel_it->second.find(end);
            if (edge_it != rel_it->second.end())
            {
                is_new_edge = false;
                edge_id = edge_it->second.id; // preserve the existing edge's id
            }
        }
    }

    // Mutate the in-RAM adjacency map FIRST, then persist. update_node_edges
    // reads node->neighborgs to produce the new on-disk state, so the new edge
    // must be visible to it. If the persist throws, the in-RAM state is ahead
    // of disk for the rest of the process lifetime — but the process tipically
    // dies on the exception anyway, and a restart re-reads the (old) state
    // from disk so no permanent inconsistency.
    node->neighborgs[type][end] = EdgeRef{edge_id, weight, nodes[end]};

    // Edge persistence: rewrite the relation list + edge chunks at fresh
    // offsets in nodes.dat / edges.dat and patch NodeIndex.relation_offset
    // in-place. Each edge is written with its own EdgeRef.id (no longer a
    // per-node-local counter). The OLD regions become orphaned bytes (see TODO
    // inside the function — freelist persistence is the planned reclaim mechanism).
    update_node_edges(*node, meta, start);

    // Persistence of the node's edges always happens above (even when the edge
    // already existed — we just rewrote its save). The meta counters, however,
    // only advance for a genuinely new edge: next_edge_id is the monotonic id
    // source (now wired into Edge.id), edge_count tracks live edges. meta.dat is
    // truncated + rewritten so the counters survive a restart.
    if (is_new_edge)
    {
        meta.next_edge_id++;
        meta.edge_count++;
        write_meta(meta);
    }
}

/**
 * Deletes a node and all its associated edges from the graph.
 * @param node_id The id of the node to delete.
 */
void Graph::delete_node(int node_id)
{
    // Ensure the node is resident in RAM. If it is not, lazy-load it from disk
    // (same pattern as add_edge): an id >= meta.next_id was never assigned.
    if (nodes.find(node_id) == nodes.end())
    {
        if (static_cast<uint64_t>(node_id) < meta.next_id)
        {
            try
            {
                nodes[node_id] = read_node(static_cast<uint64_t>(node_id));
            }
            catch (const std::exception &e)
            {
                Graph::logger.error("Failed to delete node: could not read node " + std::to_string(node_id) + ": " + e.what());
                throw std::runtime_error("Failed to read node " + std::to_string(node_id) + ": " + e.what());
            }
        }
        else
        {
            Graph::logger.error("Failed to delete node: node " + std::to_string(node_id) + " does not exist.");
            throw std::out_of_range("Node " + std::to_string(node_id) + " does not exist.");
        }
    }

    BaseNode *node = nodes[node_id];

    // Log how many edges are being dropped (sum of the neighbor maps over all relation types).
    size_t edge_count = 0;
    for (const auto &[rel_type, neighbors] : node->neighborgs)
    {
        edge_count += neighbors.size();
    }
    logger.info("Deleting node " + std::to_string(node_id) + " and its " + std::to_string(edge_count) + " edges.");

    // Remove from the in-memory map and free it — whether it was already resident
    // or was just lazy-loaded above. (The previous version leaked a just-loaded
    // node and left a dangling entry in `nodes` pointing at a deleted record.)
    nodes.erase(node_id);
    delete node;

    // Persist the deletion: orphan the node's regions onto the freelists.
    delete_node_from_disk(static_cast<uint64_t>(node_id), meta);

    // TODO(next step): once delete_node_from_disk updates the meta counters
    // (node_count--, free_count++ / free_edge_count++), re-persist meta here with
    // write_meta(meta), and decide nodes.idx tombstoning + inbound-edge cleanup.
}