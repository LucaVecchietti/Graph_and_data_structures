#include<bits/stdc++.h>

using namespace std;

struct BaseNode {
    virtual ~BaseNode() = default;
    map<string, map<int, pair<int, BaseNode*>>> neighborgs;
};

template<typename T>
struct Node : public BaseNode {
    T data;
};

class Graph {

    private:
        vector<BaseNode*> nodes;

    public:
        Graph () {}         // Basic costructor

        virtual ~Graph() {      // Destructor
            for (auto node : nodes) {
                delete node;
            }
        }

        template<typename T>
        void insert(T value) {
            Node<T> *newNode = new Node<T>();       // Create a new node
            newNode->data = value;                  // Assigne the input value
            nodes.push_back(newNode);               // Add to the base nodes vector
        }

        void add_edge(int start, int end, string type = "", int weight = 1) {
            BaseNode *node = nodes[start];                          // Get the start nbodes from the base nodes vector
            pair<int, BaseNode*> edge = {weight, nodes[end]};      // Create the edge and assigne the weight

            // Add the edge and the near node to the neighborgs based on the type of the relation
            node->neighborgs[type][end] = edge;
        }
};
