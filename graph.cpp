#include<bits/stdc++.h>

using namepsace std;

struct BaseNode {
    virtual ~BaseNode() = default;
    map<string, map<int, pair<int, BaseNode*>>> neighborgs;
};

template<typename T>
struct Node : public BaseNode {
    T data;
};

class Graph {

    private vector<BaseNode*> nodes;

    Graph () {}         // Basic costructor 

    virtual ~Graph() {      // Destructor 
        for ( node : nodes ){
            delete node;
        }
    }

    template<typename T>
    public void insert(T value) {

        Node *newNode = new Node<T>();      // Create a new node
        newNode->data = value;              // Assigne the input value 

        nodes.push_back(newNode);           // Add to the base nodes vector
    }

    public void add_edge(int start, int end, string type = "", int weight = 1){

        Node *node = nodes[start]; // Get the start nbodes from the base nodes vector

        edge = pair<int, BaseNode *>(weight, nodes[end]); // Create the edge and assigne the weight

        // Add the edge and the near node to the neighborgs based on the type of the relation
        node->neighborgs[type][end] = edge;
    }
}



