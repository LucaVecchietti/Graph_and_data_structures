#include <iostream>
#include "graph.h"

using namespace std;

int main()
{
    Graph g;

    // Insert nodes — storing simple ints as payload
    g.insert(0); // node 0
    g.insert(1); // node 1
    g.insert(2); // node 2
    g.insert(3); // node 3

    // Build a simple directed graph
    //
    //   0 ──(road,5)──→ 1
    //   0 ──(road,3)──→ 2
    //   1 ──(road,2)──→ 3
    //   2 ──(road,4)──→ 3
    //
    g.add_edge(0, 1, "road", 5);
    g.add_edge(0, 2, "road", 3);
    g.add_edge(1, 3, "road", 2);
    g.add_edge(2, 3, "road", 4);

    // ── BFS ────────────────────────────────────────────────────────────────
    cout << "=== BFS ===\n";
    g.bfs(0, "road", [](int idx)
          { cout << "  [node] visited: " << idx << "\n"; }, [](int from, int to, int weight)
          { cout << "  [edge] " << from << " -> " << to
                 << "  (weight=" << weight << ")\n"; });

    // ── DFS ────────────────────────────────────────────────────────────────
    cout << "\n=== DFS ===\n";
    g.dfs(0, "road", [](int idx)
          { cout << "  [node] visited: " << idx << "\n"; }, [](int from, int to, int weight)
          { cout << "  [edge] " << from << " -> " << to
                 << "  (weight=" << weight << ")\n"; });

    return 0;
}