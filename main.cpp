#include "graph_core/graph.h"

#include <iostream>
#include <filesystem>
#include <windows.h>

namespace fs = std::filesystem;

// Convenience wrapper around g.bfs for the two-phase smoke test below.
// on_node logs each visited node id; on_edge logs each (from, to, weight) triple.
static void run_bfs(Graph &g, int start, const std::string &relation)
{
    g.bfs(start, relation,
        [](int idx)                 { std::cout << "  node " << idx << "\n"; },
        [](int from, int to, int w) { std::cout << "  edge " << from << " -> " << to << " w=" << w << "\n"; }
    );
}

int main()
{
    // Every run starts from a clean db/. The on-disk format has no magic /
    // no version word, so leftover files from a different POD layout would
    // silently corrupt the read path. See CLAUDE.md ("Working-directory trap").
    fs::remove_all(fs::path(DB_PATH));
    fs::create_directories(fs::path(DB_PATH) / "attributes");  // sidecar dir for COMPLEX nodes

    // ===== Phase 1: write nodes and edges from scratch =====================
    // Exercises:
    //   - Graph::insert<int>             (primitive payload, fast path)
    //   - Graph::insert<ComplexRecord>   (COMPLEX payload + JSON sidecar)
    //   - Graph::add_edge                (now persists via update_node_edges:
    //                                     rewrites relation list + edge chunks,
    //                                     patches NodeIndex.relation_offset)
    //   - Graph::bfs                     (in-RAM traversal of the just-written graph)
    std::cout << "=== Phase 1: fresh writes ===\n";
    {
        Graph g; // init_meta on an empty db/

        g.insert(10);   // id 0
        g.insert(20);   // id 1
        g.insert(30);   // id 2
        g.insert(ComplexRecord{ "Athlete", R"({"name":"Bolt","age":35})" });  // id 3

        g.add_edge(0, 1, "road");
        g.add_edge(1, 2, "road");
        g.add_edge(2, 3, "knows");   // int payload -> COMPLEX payload

        std::cout << "BFS from 0 on \"road\":\n";
        run_bfs(g, 0, "road");
        std::cout << "BFS from 2 on \"knows\":\n";
        run_bfs(g, 2, "knows");
    } // Graph destructor: nothing else written to disk — persisted state already there.

    // ===== Phase 2: reload from disk and verify ============================
    // Exercises:
    //   - Graph::Graph() loading meta.dat                  (load_meta path)
    //   - read_node for primitive nodes (id 0, 1, 2)       (read_typed_node else branch)
    //   - read_node for the COMPLEX node (id 3)            (read_typed_node if constexpr +
    //                                                       read_complex + sidecar JSON)
    //   - Persisted "road" / "knows" edges visible after restart
    //     (regression test for BUG-001).
    std::cout << "\n=== Phase 2: reload + verify ===\n";
    {
        Graph g; // load_meta — reads next_id=4 from db/meta.dat.

        // Graph::traverse does NOT lazy-load: it silently skips ids that
        // are not already in the in-RAM `nodes` map. Since there is no
        // public read_node API on Graph, the simplest trick to pull each
        // persisted node back into RAM is to call add_edge: it lazy-loads
        // start/end via read_node when they are absent. A dedicated
        // "_load" relation type keeps the relations being verified clean.
        // Side effect: the "_load" edges themselves get persisted — desired
        // noise for a smoke test, not a correctness problem.
        g.add_edge(0, 1, "_load");
        g.add_edge(1, 2, "_load");
        g.add_edge(2, 3, "_load");

        std::cout << "BFS from 0 on \"road\" after reload:\n";
        run_bfs(g, 0, "road");
        std::cout << "BFS from 2 on \"knows\" after reload:\n";
        run_bfs(g, 2, "knows");
    }

    system("pause");
    return 0;
}
