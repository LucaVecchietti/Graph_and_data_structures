#include <bits/stdc++.h>

using namespace std;
using namespace std;

// Base node struct — type-erased, holds the adjacency map.
// neighborgs: relation_type -> { neighbor_index -> (weight, neighbor_ptr) }
struct BaseNode
{
    virtual ~BaseNode() = default;
    map<string, map<int, pair<int, BaseNode *>>> neighborgs;
};

// Typed node — inherits adjacency from BaseNode and adds the actual data payload
template <typename T>
struct Node : public BaseNode
{
    T data;
};

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

    // Creates a new typed node, assigns its value and appends it to the node list.
    // The index of the node in the vector acts as its ID.
    template <typename T>
    void insert(T value)
    {

        Node<T> *newNode = new Node<T>(); // Create a new node
        newNode->data = value;            // Assign the input value

        nodes.push_back(newNode); // Add to the base nodes vector
    }

    // Adds a directed edge from start to end with an optional type and weight.
    // The edge is stored under the given relation type in the adjacency map.
    void add_edge(int start, int end, string type = "", int weight = 1)
    {

        BaseNode *node = nodes[start];                         // Get the start node from the base nodes vector
        auto edge = pair<int, BaseNode *>(weight, nodes[end]); // Create the edge and assign the weight

        // Add the edge and the near node to the neighborgs based on the type of the relation
        node->neighborgs[type][end] = edge;
    }
};

int main () {
    
}