#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <queue>
#include <stack>
#include <stdexcept>
#include <fstream>
#include <cstdint>

using namespace std;

// ---- Costants ----

const uint8_t RELATION_TYPE_MAX_SIZE = 255;

/**
 * DISC optimized struct for I/O | POD
 */

/**
 * Base struct POD of the Node, hold the offsets of his contents
 */
#pragma pack(push, 1)
struct NodeIndex
{
    uint64_t id;                // ID of the node
    uint64_t offset;            // Offset to NodeRecord
    uint64_t relation_offset;   // Offset to RelationNodeList
};
#pragma pack(pop)

/**
 *  Content struct POD of the Node - hold the data 
 *  this Node Record can contain ONLY simple types 
 *  e.g.
 *  T = int YES
 *  T = char YES 
 *  T = float YES
 *  T = bool YES
 * 
 *  T = std::vector<int> NO
 *  T = std::string NO
 *  T = std::map<int, std::vector<char>> NO
 * 
 *  to implemente complex struct we need more complex method to wrote them on the DISC.
 */

#pragma pack(push, 1)
template<typename T>
struct NodeRecord 
{
    T data; // Content of the Node
};
#pragma pack(pop)

/**
 *  Relation list struct POD of the Node - hold the typpes and the relatives offsets of the node
 */

#pragma pack(push, 1)
struct RelationNodeList 
{
    uint64_t type_count;    // Number rlation type the node has
    /**
     * After thi POD write every relation type preceded by the dimention of the string, wich is the name of the type,
     * and followed by the offset, that point tu the position where the edges are stored, and the number of edges.
     * 
     * [After this POD structure wrote relation type as [`uint8_t`][s][t][r][i][n][g][`uint64_t`][`uint64_t`] ] * type_count
     * E.g.
     * [4][r][o][a][d][offset][relation_count]
     * [5][t][r][a][i][n][offset][relation_count]
     * ...
     */
};
#pragma pack(pop)

/**
 *  Base struct POD of the Edge - hold the weight and the offset wich point to the destination node.
 */

struct Edge
{
    uint64_t id;        // Edge  ID
    int64_t weight;     // Weight of the edge
    uint64_t to_node;   // Destination node idx on nodes.idx file [ to_node → NodeIndex(id == to_node)]
};
#pragma pack(pop)

/**
 *  RAM optimized struct 
 */

/**
 * Base node struct — type-erased, holds the adjacency map.
 * neighborgs: relation_type -> { neighbor_index -> (weight, neighbor_ptr) }
 */
struct BaseNode
{
    virtual ~BaseNode() = default;
    map<string, map<int, pair<int, BaseNode *>>> neighborgs;
};

/** 
 * Typed node — inherits adjacency from BaseNode and adds the actual data payload
 */
template <typename T>
struct Node : public BaseNode
{
    T data;
};

/**
 * ---- Traversal Policies ----
 * A policy defines the frontier behavior for graph traversal.
 * Swapping the policy changes the traversal order without touching the algorithm.
 */

 struct BFSPolicy 
 {
    using Frontier = queue<int>;
    static void push    (Frontier& f, int i) { f.push(i); }
    static int  pop     (Frontier& f) { int v = f.front(); f.pop(); return v; }
    static bool empty   (Frontier& f) { return f.empty(); }
 };

 struct DFSPolicy 
 {
    using Frontier = stack<int>;
    static void push    (Frontier& f, int i) { f.push(i); }
    static int  pop     (Frontier &f) { int v = f.top(); f.pop(); return v; }
    static bool empty   (Frontier &f) { return f.empty(); }
 };

/**
 * Graph data structure is build to store differente type of data in an efficente way and to seva relation.
 */

class Graph
{

    private:
        vector<BaseNode *> nodes; // Owns all nodes — responsible for their lifetime

    public:
        Graph() {} // Basic constructor

        virtual ~Graph()
        { // Destructor — frees all heap-allocated nodes
            for (auto node : nodes)
            {
                delete node;
            }
        }

        /**
         * Creates a new typed node, assigns its value and appends it to the node list.
         * The index of the node in the vector acts as its ID.
         * Insert with move — avoid copies of large data structures.
         */
        template <typename T>
        void insert(T&& value)  // forward reference 
        {
            using ValueType = remove_reference_t<T>;    //Strip reference in case of a pure type (eg. T=int& --strip-> T = int)

            Node<ValueType> *newNode = new Node<ValueType>();       // Create a new node
            newNode->data = forward<T>(value);      // Assign the input value and mantain lvalue\rvalue

            nodes.push_back(newNode);   // Add to the base nodes vector
        }

        /**
         * Adds a directed edge from start to end with an optional type and weight.
         * The edge is stored under the given relation type in the adjacency map.
         */
        void add_edge(int start, int end, string type = "", int weight = 1)
        {
            if (type.length() > RELATION_TYPE_MAX_SIZE) {
                throw std::invalid_argument(
                    "The size of the sring '" + type + "' is over the limit of " +
                    to_string(RELATION_TYPE_MAX_SIZE) + " characters by " + to_string(type.length() - RELATION_TYPE_MAX_SIZE) + " characters."
                );
            }

            BaseNode *node = nodes[start];                         // Get the start node from the base nodes vector
            auto edge = pair<int, BaseNode *>(weight, nodes[end]); // Create the edge and assign the weight

            // Add the edge and the near node to the neighborgs based on the type of the relation
            node->neighborgs[type][end] = edge;
        }

        /**
         * Generic graph traversal — behavior determined by Policy at compile time.
         * NodeFn: callback(int nodeIdx)            — fired when a node is first visited
         * EdgeFn: callback(int from, int to, int weight) — fired for every edge explored
         */
        template<typename Policy, typename NodeFn, typename EdgeFn>
        void traverse(int start, const string& type, NodeFn&& on_node, EdgeFn on_edge)
        {
            vector<bool> visited(nodes.size(), false);
            typename Policy::Frontier frontier;

            // Marks a node as visited, fires the node callback, pushes to frontier
            auto visit = [&](int idx)
            {
                visited[idx] = true;
                on_node(idx);
                Policy::push(frontier, idx);
            };

            visit(start);

            while (!Policy::empty(frontier)) {

                int current = Policy::pop(frontier);

                auto it = nodes[current]->neighborgs.find(type);
                if ( it == nodes[current]->neighborgs.end() ) continue;

                for (auto& [neighborgIdx, edge] : it->second) {
                    on_edge(current, neighborgIdx, edge.first); // fired for every edge, visited or not
                    if (!visited[neighborgIdx])
                        visit(neighborgIdx);
                }
            }
        }

        // Convenience wrappers — select policy without exposing it in the call site
        template <typename NodeFn, typename EdgeFn>
        void bfs(int start, const string &type, NodeFn &&on_node, EdgeFn &&on_edge)
        {
            traverse<BFSPolicy>(start, type, forward<NodeFn>(on_node), forward<EdgeFn>(on_edge));
        }

        template <typename NodeFn, typename EdgeFn>
        void dfs(int start, const string &type, NodeFn &&on_node, EdgeFn &&on_edge)
        {
            traverse<DFSPolicy>(start, type, forward<NodeFn>(on_node), forward<EdgeFn>(on_edge));
        }
};

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