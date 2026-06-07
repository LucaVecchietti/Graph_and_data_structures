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

    // ===== Phase 3: delete + freelist reuse on insert =====================
    // Exercises:
    //   - Graph::delete_node            (pushes the node's region onto the
    //                                    exact-size bin db/freelist/nodes_4.dat)
    //   - Graph::insert reuse path      (pops that bin, recycles id 1 and the
    //                                    freed 4-byte region instead of appending)
    std::cout << "\n=== Phase 3: delete + reuse ===\n";
    {
        Graph g; // load_meta — next_id is 4 after phases 1-2.

        // Delete node 1 (an int). Its 4-byte record region + id slot go on the freelist.
        g.delete_node(1);

        // Insert a new int. The reuse path should recycle id 1 (next_id stays 4)
        // and overwrite the freed region rather than appending a fresh record.
        // Watch graph.log for "Inserted node with ID 1 (reused slot) and value 777".
        g.insert(777);

        std::cout << "deleted node 1, inserted 777 — see graph.log for id reuse,\n"
                     "and db/freelist/ for the bin files.\n";
    }

    // ===== Phase 4: COMPLEX delete + reuse ================================
    // Exercises:
    //   - Graph::delete_node on a COMPLEX node (id 3): real record size taken
    //     from the on-disk ComplexHeader -> db/freelist/complex_<size>.dat, the
    //     JSON sidecar file removed, and its prog_number recycled onto the json
    //     free list (db/freelist/json_prog.dat).
    //   - Graph::insert<ComplexRecord> reuse path: same type_label "Athlete" ->
    //     same size class -> exact-fit reuse of id 3, recycling prog_number 0 and
    //     rewriting the record + sidecar in place (next_id stays 4).
    std::cout << "\n=== Phase 4: COMPLEX delete + reuse ===\n";
    {
        Graph g; // next_id is still 4 after phases 1-3.

        g.delete_node(3); // Athlete -> complex bin + recycled prog_number + removed sidecar

        // Re-insert an Athlete: same size class -> pops the complex bin and the
        // recycled prog_number 0, so the same sidecar filename is reborn with new JSON.
        g.insert(ComplexRecord{ "Athlete", R"({"name":"Gatlin","age":42})" });

        std::cout << "deleted COMPLEX node 3, re-inserted an Athlete — see graph.log for\n"
                     "id/prog reuse, db/freelist/complex_*.dat and db/attributes/.\n";
    }

    system("pause");
    return 0;
}
