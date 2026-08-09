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

    // ===== Phase 5: edge-space compaction (rel/edges bin reuse) ============
    // Exercises the NEW pop-then-append path in update_node_edges (step 3):
    // a repeated weight-overwrite of the SAME (start, end, relation) edge.
    // Each overwrite pushes the node's old relation-list region + 32-byte edge
    // chunk onto the size-segregated rel/edges bins, then pops the exact-size
    // region right back (LIFO) -> true in-place rewrite, ZERO file growth.
    // On the OLD design every overwrite appended a fresh rel region + chunk,
    // so nodes.dat / edges.dat would grow by 20x (rel region + 32 bytes).
    std::cout << "\n=== Phase 5: edge-space compaction (overwrite loop) ===\n";
    {
        Graph g;

        // Reuse two nodes that are live after phases 1-4 (both are ints):
        //   node 0 (value 10) and node 2 (value 30). insert() returns void, so
        //   we use known-live ids instead of capturing fresh ones. node 0 also
        //   already owns "road"/"_load" relations, so the overwrite rewrites a
        //   multi-relation list -> exercises several rel/edges bin push/pops.
        const int a = 0;
        const int b = 2;

        // First write of the edge (a NEW edge), then one overwrite to settle
        // into steady state (the push/pop cycle has run at least once and the
        // relation-list / edge-chunk sizes are now fixed).
        g.add_edge(a, b, "ow", 1);
        g.add_edge(a, b, "ow", 2);

        const fs::path nodes_dat = fs::path(DB_PATH) / "nodes.dat";
        const fs::path edges_dat = fs::path(DB_PATH) / "edges.dat";

        std::uintmax_t nodes_before = fs::file_size(nodes_dat);
        std::uintmax_t edges_before = fs::file_size(edges_dat);

        // 20 weight-overwrites of the same edge. If reuse fires, neither file
        // grows; if it does not, both grow monotonically.
        for (int w = 3; w < 23; ++w)
            g.add_edge(a, b, "ow", w);

        std::uintmax_t nodes_after = fs::file_size(nodes_dat);
        std::uintmax_t edges_after = fs::file_size(edges_dat);

        std::cout << "  nodes.dat: before=" << nodes_before
                  << " after=" << nodes_after
                  << (nodes_after == nodes_before ? "  [EQUAL]" : "  [GREW]") << "\n";
        std::cout << "  edges.dat: before=" << edges_before
                  << " after=" << edges_after
                  << (edges_after == edges_before ? "  [EQUAL]" : "  [GREW]") << "\n";
        std::cout << "  compaction verdict: "
                  << ((nodes_after == nodes_before && edges_after == edges_before)
                          ? "PASS (zero growth across 20 overwrites)"
                          : "FAIL (file grew -> reuse not firing)")
                  << "\n";
    }

    // ===== Phase 6: multi-edge chain on one relation (O(1) add + reload) ===
    // Regression guard for the O(1) add_edge path (persist_new_edge): several
    // edges under the SAME (node, relation) form a doubly-linked chain spliced at
    // the head, and a mid-chain weight overwrite (persist_edge_weight) must survive
    // a reload. The previous phases only ever put one edge per relation, so they
    // never exercised the chain walk on read / the prev_offset relink on write.
    std::cout << "\n=== Phase 6: multi-edge chain (same relation) ===\n";
    {
        Graph g;

        g.insert(100); // id 4
        g.insert(200); // id 5
        g.insert(300); // id 6
        g.insert(400); // id 7

        // node 4 --link--> {5, 6, 7}: a 3-edge chain under one relation.
        g.add_edge(4, 5, "link", 51);
        g.add_edge(4, 6, "link", 61);
        g.add_edge(4, 7, "link", 71);

        // Overwrite a MID-chain weight (the splice put 6 in the middle of the list).
        g.add_edge(4, 6, "link", 999);

        std::cout << "before reload, BFS from 4 on \"link\" (expect 5/w51, 6/w999, 7/w71):\n";
        run_bfs(g, 4, "link");
    }
    {
        Graph g; // reload: rebuild the chain from disk by walking next_offset.

        g.add_edge(4, 5, "_load"); // force node 4 back into RAM (lazy-load via add_edge)

        std::cout << "after reload, BFS from 4 on \"link\" (must match: 5/w51, 6/w999, 7/w71):\n";
        run_bfs(g, 4, "link");
    }

    // ===== Phase 7: relation-batch chaining (> 8 relation types) ===========
    // Regression guard for the relation batch CHAIN. A batch holds at most
    // RELATION_LINES_PER_BATCH (8) fixed-width lines; beyond that persist_new_edge
    // allocates a further batch and links it via next_offset (head 1 -> 2 -> 3...).
    // This phase drives a hub node past two batch boundaries (17 relation types =>
    // 8 + 8 + 1) and checks the three paths that must agree on the chain:
    //   - persist_new_edge      : new line in the last batch / brand-new batch
    //   - read_relation_node_list: walk next_offset on reload (all 17 relations back)
    //   - update_node_edges      : whole-chain rewrite (delete_node inbound cleanup)
    std::cout << "\n=== Phase 7: relation-batch chaining (17 relation types) ===\n";
    const int hub = 8;          // next_id is 8 after phases 1-6
    const int first_target = 9; // targets are ids 9..25
    const int rel_types = 17;

    // Counts the edges a BFS from `hub` finds on each of rel_0..rel_(count-1).
    auto count_hub_edges = [](Graph &g, int start, int count) {
        int seen = 0;
        for (int i = 0; i < count; ++i)
            g.bfs(start, "rel_" + std::to_string(i),
                  [](int) {}, [&](int, int, int) { ++seen; });
        return seen;
    };

    {
        Graph g;

        g.insert(1000);                                   // id 8  — the hub
        for (int i = 0; i < rel_types; ++i)
            g.insert(2000 + i);                           // ids 9..25 — one target per relation

        // 17 distinct relation types on ONE node: the 9th and the 17th each force a
        // brand-new batch, so the chain ends up 3 batches long.
        for (int i = 0; i < rel_types; ++i)
            g.add_edge(hub, first_target + i, "rel_" + std::to_string(i), 100 + i);

        int seen = count_hub_edges(g, hub, rel_types);
        std::cout << "  before reload, edges over 17 relations: " << seen
                  << (seen == rel_types ? "  [PASS]" : "  [FAIL]") << "\n";
    }
    {
        Graph g; // reload: read_relation_node_list must hop next_offset across 3 batches

        g.add_edge(hub, first_target, "_load"); // lazy-load the hub back into RAM

        int seen = count_hub_edges(g, hub, rel_types);
        std::cout << "  after reload,  edges over 17 relations: " << seen
                  << (seen == rel_types ? "  [PASS]" : "  [FAIL]") << "\n";
    }
    {
        // Delete the LAST target (id 25, reachable only via rel_16, the lone line of
        // the 3rd batch). The hub is its only inbound owner, so delete_node drives
        // update_node_edges on the hub: the whole 3-batch chain is freed onto the rel
        // bin and rewritten (16 rel_* + "_load" = 17 types => 3 batches again).
        Graph g;
        g.delete_node(first_target + rel_types - 1); // id 25
    }
    {
        Graph g; // reload after the chain rewrite

        g.add_edge(hub, first_target, "_load"); // lazy-load the hub (existing relation: weight overwrite)

        int seen = count_hub_edges(g, hub, rel_types - 1); // rel_0..rel_15 survive
        int gone = count_hub_edges(g, hub, rel_types) - seen;
        std::cout << "  after chain rewrite, edges over rel_0..rel_15: " << seen
                  << (seen == rel_types - 1 ? "  [PASS]" : "  [FAIL]") << "\n";
        std::cout << "  rel_16 (target deleted) must be empty: " << gone
                  << (gone == 0 ? "  [PASS]" : "  [FAIL]") << "\n";
    }

    system("pause");
    return 0;
}
