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
        build_in_edges(); // reconstruct the in-RAM reverse edge index from disk
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

void Graph::build_in_edges()
{
    // One O(N + E) scan of the persisted graph rebuilds the reverse index. From
    // here it is kept in sync incrementally by add_edge / delete_node, so deletes
    // resolve their inbound edges in O(deg_in) without re-scanning.
    in_edges = build_inbound_index(meta.next_id);
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
    // Resolve whether this is a new edge or a weight overwrite, and (for an
    // overwrite) capture the existing edge's id + disk offset. Both O(1) persist
    // paths below need the disk offset; a new edge gets a fresh id.
    bool is_new_edge = true;
    uint64_t edge_id = meta.next_edge_id;
    uint64_t existing_offset = 0;
    {
        auto rel_it = node->neighborgs.find(type);
        if (rel_it != node->neighborgs.end())
        {
            auto edge_it = rel_it->second.find(end);
            if (edge_it != rel_it->second.end())
            {
                is_new_edge = false;
                edge_id = edge_it->second.id;           // preserve the existing edge's id
                existing_offset = edge_it->second.offset; // its Edge record in edges.dat
            }
        }
    }

    if (is_new_edge)
    {
        // O(1) append + relink: write the new Edge (reusing a freed slot or
        // appending), splice it at the relation's chain head, and update that one
        // fixed-width relation line in place. The relation batch never moves, so
        // NodeIndex is untouched. Persist FIRST so we can store the returned disk
        // offset in the EdgeRef (needed for a future O(1) weight overwrite).
        uint64_t edge_off = persist_new_edge(meta, static_cast<uint64_t>(start), type,
                                             static_cast<uint64_t>(end), edge_id,
                                             static_cast<int64_t>(weight));
        node->neighborgs[type][end] = EdgeRef{edge_id, weight, nodes[end], edge_off};

        in_edges[end].insert(start); // reverse index: `start` now points at `end`
        meta.next_edge_id++;
        meta.edge_count++;
    }
    else
    {
        // O(1) in-place weight overwrite: no allocation, no relink, no growth.
        EdgeRef &ref = node->neighborgs[type][end];
        ref.weight = weight;
        ref.neighbor = nodes[end]; // re-link in case it was nullptr after a disk load
        persist_edge_weight(existing_offset, static_cast<int64_t>(weight));
    }

    // next_edge_id / edge_count advance only for a genuinely new edge; a reuse pop
    // inside persist_new_edge may also change free_edge_count — so rewrite meta.dat
    // unconditionally.
    write_meta(meta);
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

    // --- Reverse index: this node's OUTBOUND edges vanish, so drop node_id from
    //     the inbound set of every node it pointed at. Done while `node` is still
    //     resident, before we free it below. ---
    for (const auto &[rel_type, neighbors] : node->neighborgs)
    {
        for (const auto &[to_id, ref] : neighbors)
        {
            (void)ref;
            auto it = in_edges.find(to_id);
            if (it != in_edges.end()) it->second.erase(node_id);
        }
    }

    // X's own outbound edges are reclaimed by delete_node_from_disk below, but that
    // function only touches the node/free counters — account for the lost live edges
    // here (edge_count was previously never decremented on delete).
    meta.edge_count -= edge_count;

    // --- Inbound cleanup: remove the edges that OTHER nodes aim at node_id. The
    //     reverse index hands us those owners directly (O(deg_in)). For each owner
    //     we erase node_id from its adjacency and re-persist via update_node_edges,
    //     so no dangling neighbour survives a reload. ---
    auto inbound_it = in_edges.find(node_id);
    if (inbound_it != in_edges.end())
    {
        for (int owner_id : inbound_it->second)
        {
            if (owner_id == node_id) continue; // self-loop: reclaimed with node_id's own edges

            BaseNode *owner = nullptr;
            auto oit = nodes.find(owner_id);
            if (oit != nodes.end())
            {
                owner = oit->second;
            }
            else
            {
                try
                {
                    owner = read_node(static_cast<uint64_t>(owner_id));
                }
                catch (const std::exception &e)
                {
                    Graph::logger.error("delete_node: could not load inbound owner " + std::to_string(owner_id) + ": " + e.what());
                    continue;
                }
                nodes[owner_id] = owner;
            }

            // Erase node_id from every relation of the owner; drop relations left empty.
            uint64_t removed = 0;
            for (auto rel_it = owner->neighborgs.begin(); rel_it != owner->neighborgs.end();)
            {
                removed += rel_it->second.erase(node_id);
                if (rel_it->second.empty())
                    rel_it = owner->neighborgs.erase(rel_it);
                else
                    ++rel_it;
            }

            if (removed > 0)
            {
                update_node_edges(*owner, meta, static_cast<uint64_t>(owner_id));
                meta.edge_count -= removed;
                logger.info("delete_node: removed " + std::to_string(removed) + " inbound edge(s) " + std::to_string(owner_id) + " -> " + std::to_string(node_id));
            }
        }
        in_edges.erase(node_id);
    }

    // Remove from the in-memory map and free it — whether it was already resident
    // or was just lazy-loaded above. (The previous version leaked a just-loaded
    // node and left a dangling entry in `nodes` pointing at a deleted record.)
    nodes.erase(node_id);
    delete node;

    // Persist the deletion: orphan + zero the node's regions onto the freelists,
    // tombstone its idx slot, and update the meta counters (mutated in place).
    delete_node_from_disk(static_cast<uint64_t>(node_id), meta);
    write_meta(meta);
}

/**
 * TODO: Implement a method to delete an edge from the graph.
 * 
 * Overload 1: Deletes an edge from start to end with a specific type.
 * @param start The ID of the start node.
 * @param end The ID of the end node.
 * @param type The relation type of the edge (e.g., "road", "train"). Optional, defaults to an empty string. 
 */ 
void Graph::delete_edge(int start, int end, std::string type)
{
    // Implementation for deleting an edge from start to end with a specific type.
    // This method should check if the start and end nodes exist, if the edge exists,
    // and then remove the edge from the graph, updating the necessary data structures.

    //Check if the nodes is already loaded in memory, if not load it from disk
    if (nodes.find(start) == nodes.end())
    {
        if (static_cast<uint64_t>(start) < meta.next_id)
        {
            try
            {
                nodes[start] = read_node(static_cast<uint64_t>(start));
            }
            catch (const std::exception &e)
            {
                Graph::logger.error("Failed to delete edge: could not read node " + std::to_string(start) + ": " + e.what());
                throw std::runtime_error("Failed to read node " + std::to_string(start) + ": " + e.what());
            }
        }
        else
        {
            Graph::logger.error("Failed to delete edge: node " + std::to_string(start) + " does not exist.");
            throw std::out_of_range("Node " + std::to_string(start) + " does not exist.");
        }
    }

    if (nodes.find(end) == nodes.end())
    {
        if (static_cast<uint64_t>(end) < meta.next_id)
        {
            try
            {
                nodes[end] = read_node(static_cast<uint64_t>(end));
            }
            catch (const std::exception &e)
            {
                Graph::logger.error("Failed to delete edge: could not read node " + std::to_string(end) + ": " + e.what());
                throw std::runtime_error("Failed to read node " + std::to_string(end) + ": " + e.what());
            }
        }
        else
        {
            Graph::logger.error("Failed to delete edge: node " + std::to_string(end) + " does not exist.");
            throw std::out_of_range("Node " + std::to_string(end) + " does not exist.");
        }
    }

    BaseNode *node = nodes[start]; // Get the start node from the base nodes vector

    auto rel_it = node->neighborgs.find(type);
    if (rel_it != node->neighborgs.end())
    {
        auto edge_it = rel_it->second.find(end);
        if (edge_it != rel_it->second.end())
        {
            // Remove the edge from the in-memory structure
            uint64_t edge_offset = edge_it->second.offset;
            rel_it->second.erase(edge_it);

            // If the relation type is now empty, remove it from the node's neighborgs
            if (rel_it->second.empty())
            {
                node->neighborgs.erase(rel_it);
            }

            // Update the node's edges on disk
            update_node_edges(*node, meta, static_cast<uint64_t>(start));

            // Reverse index: it is keyed by (source node -> target node), with NO
            // relation granularity, so `start` must stay in in_edges[end] as long as
            // ANY of its relations still points at `end`. Dropping it unconditionally
            // would, after deleting 0 --road--> 1 while 0 --train--> 1 survives, make a
            // later delete_node(1) skip the "train" edge and leave it dangling at a
            // tombstoned slot. O(relation types of start).
            bool still_points_at_end = false;
            for (const auto &[other_rel, other_neighbors] : node->neighborgs)
            {
                (void)other_rel;
                if (other_neighbors.find(end) != other_neighbors.end())
                {
                    still_points_at_end = true;
                    break;
                }
            }
            if (!still_points_at_end)
            {
                auto in_it = in_edges.find(end);
                if (in_it != in_edges.end()) in_it->second.erase(start);
            }

            // Update metadata
            meta.edge_count--;
            write_meta(meta);

            logger.info("Deleted edge from node " + std::to_string(start) + " to node " + std::to_string(end) + " with type '" + type + "'");
        }
        else
        {
            Graph::logger.error("Failed to delete edge: edge from node " + std::to_string(start) + " to node " + std::to_string(end) + " with type '" + type + "' does not exist.");
            throw std::out_of_range("Edge from node " + std::to_string(start) + " to node " + std::to_string(end) + " with type '" + type + "' does not exist.");
        }
    }
    else
    {
        Graph::logger.error("Failed to delete edge: relation type '" + type + "' does not exist for node " + std::to_string(start));
        throw std::out_of_range("Relation type '" + type + "' does not exist for node " + std::to_string(start));
    }
}

/**
 * Overload 2: deletes the edge carrying `edge_id`.
 *
 * Edge ids are global and stable but there is NO id -> location index, so the id is
 * first resolved to (start, end, relation) and then handed to overload 1 — which keeps
 * a single delete path (update_node_edges + reverse index + counters) for both entry
 * points. Resolution mirrors the lazy-load pattern of the other overloads: the in-RAM
 * nodes are searched first, and only if the edge is not resident does it fall back to
 * the O(N+E) disk scan in find_edge_by_id.
 * @param edge_id The id of the edge to delete.
 * @throws std::out_of_range if no live edge carries that id.
 */
void Graph::delete_edge(int edge_id)
{
    // Same shape as the node-id guards in the other overloads: an id >= next_edge_id was
    // never assigned, so there is nothing to look for.
    if (edge_id < 0 || static_cast<uint64_t>(edge_id) >= meta.next_edge_id)
    {
        Graph::logger.error("Failed to delete edge: edge id " + std::to_string(edge_id) + " was never assigned.");
        throw std::out_of_range("Edge id " + std::to_string(edge_id) + " was never assigned.");
    }

    // ---- 1. Resolve the id against the resident nodes -----------------------
    // Copies are captured, not references: the delegate below mutates the very maps
    // being iterated here (and erases the relation entry when it empties).
    int start = -1;
    int end = -1;
    std::string type;
    bool found = false;

    for (const auto &[owner_id, owner] : nodes)
    {
        for (const auto &[rel_type, neighbors] : owner->neighborgs)
        {
            for (const auto &[to_id, ref] : neighbors)
            {
                if (ref.id == static_cast<uint64_t>(edge_id))
                {
                    start = owner_id;
                    end   = to_id;
                    type  = rel_type;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (found) break;
    }

    // ---- 2. Not resident: fall back to the disk scan ------------------------
    if (!found)
    {
        std::optional<EdgeLocation> loc = find_edge_by_id(static_cast<uint64_t>(edge_id), meta.next_id);
        if (!loc)
        {
            Graph::logger.error("Failed to delete edge: no live edge carries id " + std::to_string(edge_id) + ".");
            throw std::out_of_range("Edge id " + std::to_string(edge_id) + " does not exist.");
        }
        start = static_cast<int>(loc->from_node);
        end   = static_cast<int>(loc->to_node);
        type  = loc->relation;
    }

    logger.info("delete_edge: edge id " + std::to_string(edge_id) + " resolved to "
                + std::to_string(start) + " --" + type + "--> " + std::to_string(end)
                + (found ? " (from RAM)" : " (from disk scan)"));

    // ---- 3. One single delete path ------------------------------------------
    delete_edge(start, end, type);
}