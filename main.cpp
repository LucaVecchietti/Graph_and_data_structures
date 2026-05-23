#include "graph_core/graph.h"
#include <iostream>
#include <windows.h>

int main()
{
    Graph g;

    g.insert(10);
    g.insert(20);
    g.insert(30);

    g.add_edge(0, 1, "road");
    g.add_edge(1, 2, "road");

    g.bfs(0, "road",
        [](int idx)                 { std::cout << "node " << idx << "\n"; },
        [](int from, int to, int w) { std::cout << "edge " << from << "->" << to << " w=" << w << "\n"; }
    );

    system("pause");

    return 0;
}
