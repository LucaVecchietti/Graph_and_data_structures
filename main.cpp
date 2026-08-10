#include "graph_core/graph.h"

#include <iostream>
#include <filesystem>
#include <string>
#include <windows.h>

namespace fs = std::filesystem;

// ---- Tiny assertion harness -----------------------------------------------
// The phases below no longer ask the reader to eyeball BFS output: every phase
// states what it expects, and the run ends with a single verdict + exit code.
static int g_failures = 0;

static void expect(const std::string &label, long long got, long long want)
{
    const bool ok = (got == want);
    if (!ok) ++g_failures;
    std::cout << "  " << (ok ? "[PASS] " : "[FAIL] ") << label
              << " -> got " << got << ", want " << want << "\n";
}

// Result of one traversal: how many distinct nodes were visited and how many
// edges were explored.
struct Walk
{
    int nodes = 0;
    int edges = 0;
    long long weight_sum = 0;
};

static Walk bfs_walk(Graph &g, int start, const std::string &relation)
{
    Walk w;
    g.bfs(start, relation,
          [&](int)                { ++w.nodes; },
          [&](int, int, int wgt)  { ++w.edges; w.weight_sum += wgt; });
    return w;
}

// Verbose variant, for the phases where seeing the shape of the walk helps.
static void print_bfs(Graph &g, int start, const std::string &relation)
{
    g.bfs(start, relation,
        [](int idx)                 { std::cout << "    node " << idx << "\n"; },
        [](int from, int to, int w) { std::cout << "    edge " << from << " -> " << to << " w=" << w << "\n"; }
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
    //   - Graph::add_edge                (O(1) persist via persist_new_edge)
    //   - Graph::bfs                     (in-RAM traversal of the just-written graph)
    // Everything is resident here, so this is the baseline the cold-cache
    // phases below are compared against.
    std::cout << "=== Phase 1: fresh writes (all nodes resident) ===\n";
    {
        Graph g; // init_meta on an empty db/

        g.insert(10);   // id 0
        g.insert(20);   // id 1
        g.insert(30);   // id 2
        g.insert(ComplexRecord{ "Athlete", R"({"name":"Bolt","age":35})" });  // id 3

        g.add_edge(0, 1, "road");
        g.add_edge(1, 2, "road");
        g.add_edge(2, 3, "knows");   // int payload -> COMPLEX payload

        std::cout << "  BFS from 0 on \"road\" (chain 0 -> 1 -> 2):\n";
        print_bfs(g, 0, "road");

        Walk road = bfs_walk(g, 0, "road");
        expect("road: nodes visited", road.nodes, 3);   // 0, 1, 2
        expect("road: edges explored", road.edges, 2);  // 0->1, 1->2

        Walk knows = bfs_walk(g, 2, "knows");
        expect("knows: nodes visited", knows.nodes, 2); // 2, 3
        expect("knows: edges explored", knows.edges, 1);
    } // Graph destructor: nothing else written — persisted state already there.

    // ===== Phase 2: cold reload — lazy load inside traverse ================
    // THE discriminating phase for the lazy-loading work. A brand-new Graph has
    // an EMPTY `nodes` map: only meta.dat and the reverse index are loaded. So a
    // traversal has to materialise nodes from disk as it walks.
    //
    // Previously every phase forced the load by hand with a throwaway
    // `add_edge(x, y, "_load")` call, which polluted the store and hid this gap.
    // That trick is gone: the walks below rely on traverse alone.
    //
    // Two cases, deliberately separated:
    //   (a) DEPTH 1  — 2 --knows--> 3. Only the START node must be materialised.
    //   (b) DEPTH 2  — 0 --road--> 1 --road--> 2. The node discovered at depth 1
    //                  must ALSO be materialised in order to be expanded.
    std::cout << "\n=== Phase 2: cold reload + lazy load in traverse ===\n";
    {
        Graph g; // load_meta only — `nodes` is empty, nothing is resident

        std::cout << "  (a) depth-1 walk, BFS from 2 on \"knows\":\n";
        print_bfs(g, 2, "knows");
        Walk knows = bfs_walk(g, 2, "knows");
        expect("cold depth-1: nodes visited", knows.nodes, 2);  // 2, 3
        expect("cold depth-1: edges explored", knows.edges, 1);
    }
    {
        Graph g; // fresh again, so case (b) starts from a genuinely cold map

        std::cout << "  (b) depth-2 walk, BFS from 0 on \"road\":\n";
        print_bfs(g, 0, "road");
        Walk road = bfs_walk(g, 0, "road");
        expect("cold depth-2: nodes visited", road.nodes, 3);   // 0, 1, 2
        expect("cold depth-2: edges explored", road.edges, 2);  // 0->1, 1->2
    }
    {
        Graph g; // a non-existent start must still be rejected, not silently empty

        bool threw = false;
        try { bfs_walk(g, 999, "road"); }
        catch (const std::out_of_range &) { threw = true; }
        expect("cold walk from a never-assigned id throws", threw ? 1 : 0, 1);
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

        g.delete_node(1);   // its 4-byte record region + id slot go on the freelist
        g.insert(777);      // reuse path: recycles id 1, next_id stays 4

        // Node 1 is gone as a hop: the road chain now ends at 0 -> 1(new, no edges).
        // 0 --road--> 1 was removed by delete_node's inbound cleanup, so the walk
        // from 0 sees only itself.
        Walk road = bfs_walk(g, 0, "road");
        expect("road after delete_node(1): nodes visited", road.nodes, 1);
        expect("road after delete_node(1): edges explored", road.edges, 0);

        std::cout << "  deleted node 1, inserted 777 — see graph.log for id reuse,\n"
                     "  and db/freelist/ for the bin files.\n";
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
        g.insert(ComplexRecord{ "Athlete", R"({"name":"Gatlin","age":42})" });

        // The "knows" edge 2 -> 3 was reclaimed with the old node 3.
        Walk knows = bfs_walk(g, 2, "knows");
        expect("knows after COMPLEX delete: edges explored", knows.edges, 0);

        std::cout << "  deleted COMPLEX node 3, re-inserted an Athlete — see graph.log for\n"
                     "  id/prog reuse, db/freelist/complex_*.dat and db/attributes/.\n";
    }

    // ===== Phase 5: edge-space compaction (rel/edges bin reuse) ============
    // A repeated weight-overwrite of the SAME (start, end, relation) edge must be
    // an in-place 8-byte write (persist_edge_weight): no allocation, no relink, and
    // therefore ZERO growth of nodes.dat / edges.dat.
    std::cout << "\n=== Phase 5: edge-space compaction (overwrite loop) ===\n";
    {
        Graph g;

        const int a = 0;   // value 10, live since phase 1
        const int b = 2;   // value 30, live since phase 1

        // First write of the edge (a NEW edge), then one overwrite to settle
        // into steady state (sizes are fixed from here on).
        g.add_edge(a, b, "ow", 1);
        g.add_edge(a, b, "ow", 2);

        const fs::path nodes_dat = fs::path(DB_PATH) / "nodes.dat";
        const fs::path edges_dat = fs::path(DB_PATH) / "edges.dat";

        std::uintmax_t nodes_before = fs::file_size(nodes_dat);
        std::uintmax_t edges_before = fs::file_size(edges_dat);

        for (int w = 3; w < 23; ++w)
            g.add_edge(a, b, "ow", w);

        std::uintmax_t nodes_after = fs::file_size(nodes_dat);
        std::uintmax_t edges_after = fs::file_size(edges_dat);

        std::cout << "  nodes.dat: before=" << nodes_before << " after=" << nodes_after << "\n";
        std::cout << "  edges.dat: before=" << edges_before << " after=" << edges_after << "\n";
        expect("nodes.dat growth over 20 overwrites", (long long)(nodes_after - nodes_before), 0);
        expect("edges.dat growth over 20 overwrites", (long long)(edges_after - edges_before), 0);

        // The last written weight must be the one we read back.
        Walk ow = bfs_walk(g, a, "ow");
        expect("ow: last weight wins", ow.weight_sum, 22);
    }

    // ===== Phase 6: multi-edge chain on one relation (O(1) add + reload) ===
    // Several edges under the SAME (node, relation) form a doubly-linked chain
    // spliced at the head; a mid-chain weight overwrite (persist_edge_weight) must
    // survive a reload. All three targets are leaves, so the reload walk only needs
    // the START node materialised — depth 1.
    std::cout << "\n=== Phase 6: multi-edge chain (same relation) ===\n";
    {
        Graph g;

        g.insert(100); // id 4
        g.insert(200); // id 5
        g.insert(300); // id 6
        g.insert(400); // id 7

        g.add_edge(4, 5, "link", 51);
        g.add_edge(4, 6, "link", 61);
        g.add_edge(4, 7, "link", 71);
        g.add_edge(4, 6, "link", 999);   // mid-chain overwrite

        std::cout << "  before reload:\n";
        print_bfs(g, 4, "link");
        Walk before = bfs_walk(g, 4, "link");
        expect("chain before reload: edges", before.edges, 3);
        expect("chain before reload: weight sum", before.weight_sum, 51 + 999 + 71);
    }
    {
        Graph g; // cold: the chain is rebuilt from disk by walking next_offset

        std::cout << "  after reload:\n";
        print_bfs(g, 4, "link");
        Walk after = bfs_walk(g, 4, "link");
        expect("chain after reload: nodes", after.nodes, 4);              // 4, 5, 6, 7
        expect("chain after reload: edges", after.edges, 3);
        expect("chain after reload: weight sum", after.weight_sum, 51 + 999 + 71);
    }

    // ===== Phase 7: relation-batch chaining (> 8 relation types) ===========
    // A batch holds at most RELATION_LINES_PER_BATCH (8) fixed-width lines; beyond
    // that persist_new_edge allocates a further batch and links it via next_offset
    // (head 1 -> 2 -> 3...). 17 relation types => 8 + 8 + 1. Checks the three paths
    // that must agree on the chain: persist_new_edge (new line / new batch),
    // read_relation_node_list (walk on reload) and update_node_edges (whole-chain
    // rewrite, driven here by delete_node's inbound cleanup).
    std::cout << "\n=== Phase 7: relation-batch chaining (17 relation types) ===\n";
    const int hub = 8;          // next_id is 8 after phases 1-6
    const int first_target = 9; // targets are ids 9..25
    const int rel_types = 17;

    // Edges the hub still has over rel_0..rel_(count-1). Each relation holds one
    // edge to a leaf, so every walk is depth 1.
    auto count_hub_edges = [](Graph &g, int start, int count) {
        int seen = 0;
        for (int i = 0; i < count; ++i)
            seen += bfs_walk(g, start, "rel_" + std::to_string(i)).edges;
        return seen;
    };

    {
        Graph g;

        g.insert(1000);                                   // id 8  — the hub
        for (int i = 0; i < rel_types; ++i)
            g.insert(2000 + i);                           // ids 9..25 — one target per relation

        // The 9th and the 17th type each force a brand-new batch: 3 batches total.
        for (int i = 0; i < rel_types; ++i)
            g.add_edge(hub, first_target + i, "rel_" + std::to_string(i), 100 + i);

        expect("chaining before reload: edges over 17 relations",
               count_hub_edges(g, hub, rel_types), rel_types);
    }
    {
        Graph g; // cold: read_relation_node_list must hop next_offset across 3 batches

        expect("chaining after reload: edges over 17 relations",
               count_hub_edges(g, hub, rel_types), rel_types);
    }
    {
        // Delete the LAST target (id 25, reachable only via rel_16, the lone line of
        // the 3rd batch). The hub is its only inbound owner, so delete_node drives
        // update_node_edges on the hub: the whole 3-batch chain is freed onto the rel
        // bin and rewritten.
        Graph g;
        g.delete_node(first_target + rel_types - 1); // id 25
    }
    {
        Graph g; // cold reload after the chain rewrite

        expect("after chain rewrite: edges over rel_0..rel_15",
               count_hub_edges(g, hub, rel_types - 1), rel_types - 1);
        expect("after chain rewrite: rel_16 is empty",
               bfs_walk(g, hub, "rel_16").edges, 0);
    }

    // ===== Verdict =========================================================
    std::cout << "\n=== Verdict ===\n"
              << (g_failures == 0 ? "  ALL CHECKS PASSED\n"
                                  : "  " + std::to_string(g_failures) + " CHECK(S) FAILED\n");

    system("pause");
    return g_failures == 0 ? 0 : 1;
}
