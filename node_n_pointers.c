#include <stdio.h>
#include <stdlib.h>
#include "map_hash_table.h"

typedef enum {
    INT,
    FLOAT,
    DOUBLE,
    CHAR,
} DataType;

/*
IMPORTANT NODE:
To improve the efficency to access the neighborgs of a node we have tu use a map instead of an array of pointers to the neighborgs,
this way we can access the neighborgs of a node in O(1) time insteaad of O(num_neighborgs) time. 

typedef struct Node {
    DataType type;
    void *data;
    int size;
    HashTable *neighborgs; // Use a hash table to store the neighborgs of the node, where the key is the index of the neighborg node and the value is a pointer to the neighborg node
} Node;

After this change we need to update all the functions that access the neighborgs of a node to use the hash table 
insted of the array of pointers to the neighborgs.

*/

typedef struct Node {
    DataType type;
    void *data;
    int size;
    struct Node **neighborgs;
    int num_neighborgs;
} Node;

typedef struct {
    /*
    This struct is an adjacency matrix rapresentation of a graph, it contains a poiner to a 2D array of integers (adjacency_matrix)
    and the number of nodes in the Graph (num_nodes). The ajacency matrix is a square matrix where the element at row i and column j 
    is 1 if there is an edge from node i to node j, and 0 otherwise.

    This struct cud be used to represent a graph in a more efficient way than the Graph struct, especially for dense graphs, 
    where the number of edges is close to the square of the number of nodes. However, it is less flexible than the Graph struct, 
    as it does not allow to store data in the nodes or to have different types of data in the nodes.

    This struct could be use to finde all the neighbors of a node in O(num_nodes) time, while in the Graph struct it 
    would take O(num_neighborgs) time, where num_neighborgs is the number of neighbors of the node.

    In case some of the edges are mono directional we can finde all the nodes that have a pointer to a specific node 
    by looking at the column corresponding to the node.
    Whith the Graph struct we would need to look at all the nodes and check if they are neighborgs of the node we are looking for, 
    which would take O(num_nodes) time in the worst case.

    If the value of a elemente Ii,j = 1 it means that the node i has a pointer to node j, if the value is 0 it means that there is no pointer from node i to node j. 

    The diagonal of the adjacency matrix (where i = j) can be used to indicate if a node has a pointer to itself, which is a special case of an edge in the graph. 
    */
    int **adjacency_matrix;
    int num_nodes;
} AdjacencyMatrixGraph;

typedef struct {
    Node *nodes;
    int num_nodes;
    AdjacencyMatrixGraph adj_matrix_graph;
} Graph;

Graph* init_graph(){
    /*
    This functionn initialise a new graph and return the pointer to the graph,
    the graph is initialised with no nodes and the number of nodes is set to 0.
    */

    Graph *graph = (Graph *)malloc(sizeof(Graph));
    graph->nodes = NULL;
    graph->num_nodes = 0;
    graph->adj_matrix_graph.adjacency_matrix = NULL;
    graph->adj_matrix_graph.num_nodes = 0;

    return graph;
}

void add_node_to_adjacency_matrix(AdjacencyMatrixGraph *adj_matrix_graph, int new_num_nodes) {
    /*
    This function update the adjacency matrix of the graph to accommodate the new number of nodes.
    It reallocates memory for the adjacency matrix and initializes the new elements to 0.
    */

    int old_num_nodes = adj_matrix_graph->num_nodes; // Store the old number of nodes before updating it

    int **new_adj_matrix = (int **)malloc(new_num_nodes * sizeof(int *)); // Allocate memory for the new adjacency matrix
    for (int i = 0; i < new_num_nodes; i++) {
        new_adj_matrix[i] = (int *)calloc(new_num_nodes, sizeof(int)); // Allocate memory for each row of the new adjacency matrix

        if (i < old_num_nodes) { // Copy the old adjacency matrix to the new one for the existing nodes
            memcpy(new_adj_matrix[i], adj_matrix_graph->adjacency_matrix[i], old_num_nodes * sizeof(int)); // Copy the old adjacency matrix to the new one for the existing nodes

            free(adj_matrix_graph->adjacency_matrix[i]); // Free the old row of the adjacency matrix after copying it to the new one
        }
    }

    // Free the old adjacency matrix
    free(adj_matrix_graph->adjacency_matrix);
    adj_matrix_graph->adjacency_matrix = new_adj_matrix; // Update the pointer to the new adjacency matrix
    adj_matrix_graph->num_nodes = new_num_nodes; // Update the number of nodes in the adjacency matrix graph
}

void add_node(Graph *graph, DataType type, void *input_data, int n_elements) {
    /*
    This function adds a new node to the graph.
    */

    size_t data_size; // Variable to store the size of the data to be stored in the node
    size_t total_data_size; // Variable to store the total size of the data to be stored in the node (data_size * n_elements)

    switch (type) { // Use the type to determinate how to allocate memory for the data
        case INT:
            data_size = sizeof(int);
            break;
        case FLOAT:
            data_size = sizeof(float);
            break;
        case DOUBLE:
            data_size = sizeof(double);
            break;
        case CHAR:
            data_size = sizeof(char);
            break;
        default :
            fprintf(stderr, "Invalid data type\n");
            return;
    }

    total_data_size = data_size * n_elements; // Calculate the total size of the data to be stored in the node

    Node *new_node = (Node *)malloc(sizeof(Node)); // Allocate memory for the new node
    new_node->type = type; // Set the type of the new node
    new_node->data = malloc(total_data_size); // Allocate memory for the data of the new node
    new_node->size = n_elements; // Set the size of the data of the new node (number of elements)
    new_node->neighborgs = NULL; // Initialize the neighbors of the new node to NULL
    new_node->num_neighborgs = 0; // Initialize the number of neighbors of the new node to 0

    // Copy the input data to the new node's data
    memcpy(new_node->data, input_data, total_data_size);

    // Add the new node to the graph
    graph->nodes = (Node *)realloc(graph->nodes, (graph->num_nodes + 1) * sizeof(Node)); // Reallocate memory for the nodes of the graph to accommodate the new node
    graph->nodes[graph->num_nodes] = *new_node; // Add the new node to the graph
    graph->num_nodes++; // Increment the number of nodes in the graph

    
    // Update the adjacency matrix graph to accommodate the new node
    add_node_to_adjacency_matrix(&graph->adj_matrix_graph, graph->num_nodes); // Update the adjacency matrix graph to accommodate the new node
}

void add_edge(Graph *graph, int node1_index, int node2_index) {
    /*
    This function create the edge between two nodes of the graph, by their index in the graph's nnodes array.
    */

    if (node1_index >= graph->num_nodes || node2_index >= graph->num_nodes || node1_index < 0 || node2_index < 0) { // Check if the node indices are valid before accessing the nodes
        fprintf(stderr, "Invalid node index\n");
        return;
    }

    Node *node1 = &graph->nodes[node1_index]; // Get the first node by its index in the graph nodes array
    Node *node2 = &graph->nodes[node2_index]; //Get the second node by its index in the graph nodes array

    // Add node2 to the neighborgs of index 1
    node1->neighborgs = (Node **)realloc(node1->neighborgs, (node1->num_neighborgs + 1) * sizeof(Node *)); // Reallocate memory of the neighborgs of node 1 to accommodate the neighborg node2
    node1->neighborgs[node1->num_neighborgs] = node2; // add node2 to the neighborgs of node 1
    node1->num_neighborgs++; // Increment the number of neighbors of node 1

    // Update the adjacency matrix graph to indicate the edge between node1 and node2
    graph->adj_matrix_graph.adjacency_matrix[node1_index][node2_index] = 1; // Set the element at row node1_index and column node2_index to 1 to indicate the edge between node1 and node2
}

void remove_node(Graph *graph, int node_index) {
    /*
    This function removes a node from the graph, including its connections.
    */

    if (node_index >= graph->num_nodes || node_index < 0) {
        fprintf(stderr, "Invalid node index\n");
        return;
    }

    //Remove the node from the grph nodes array
    free(graph->nodes[node_index].data); // Free the data of the node to be removed
    if(graph->nodes[node_index].neighborgs != NULL) {
        free(graph->nodes[node_index].neighborgs); //Free the neighborgs of the node to be removed if they are not NULL
    }

    AdjacencyMatrixGraph *adj_matrix_graph = &graph->adj_matrix_graph; // Get the adjacency matrix graph from the graph struct to update it

    // Shift the nodes in the graph nodes array to remove the node to be removed
    memmove(
        &graph->nodes[node_index],
        &graph->nodes[node_index + 1],
        (graph->num_nodes - node_index - 1) * sizeof(Node));

    graph->num_nodes--; // Decrement the number of nodes in the graph after removing the node

    // Update the neighborgs of the remaining nodes to remove the pointer to the removed node
    for (int i = 0; i < graph->num_nodes; i++) {
        if (adj_matrix_graph->adjacency_matrix[i][node_index]){
            // If there is an edge from node i to the removed node, we need to remove the pointer to the removed node from the neighborgs of node i
            Node *node_i = &graph->nodes[i]; // Get node i by its index in the graph nodes array

            // Find the index of the removed node in the neighborgs of node i
            int neighborg_index = -1;
            for (int j = 0; j < node_i->num_neighborgs; j++) {
                if (node_i->neighborgs[j] == &graph->nodes[node_index]) {
                    neighborg_index = j;
                    break;
                }
            }

            if (neighborg_index != -1) { // If the removed node is a neighbor of node i, we need to remove it from the neighborgs of node i
                memmove(
                    &node_i->neighborgs[neighborg_index],
                    &node_i->neighborgs[neighborg_index + 1],
                    (node_i->num_neighborgs - neighborg_index - 1) * sizeof(Node *));

                node_i->num_neighborgs--; // Decrement the number of neighbors of node i after removing the pointer to the removed node
                node_i->neighborgs = (Node **)realloc(node_i->neighborgs, node_i->num_neighborgs * sizeof(Node *)); // Reallocate memory for the neighborgs of node i to accommodate the new number of neighbors after removing the pointer to the removed node
            }
        }
    }
 
    // Update the adjacency matrix graph to remove the node and its connection wit other nodes by shifting the rows and columns of the adjacency matrix
    for (int i = node_index; i < graph->num_nodes - 1; i++) { // Shift the rows of the adjacency matrix up to remove the row corresponding to the node to be removed
        free(adj_matrix_graph->adjacency_matrix[i]); // Free the old row of the adjacency matrix before shifting it
        adj_matrix_graph->adjacency_matrix[i] = adj_matrix_graph->adjacency_matrix[i + 1]; // Shift the row up
    }

    free(adj_matrix_graph->adjacency_matrix[graph->num_nodes - 1]); // Free the last row of the adjacency matrix after shifting it

    for (int i = 0; i < graph->num_nodes - 1; i++){ // Shift the columns of the adjacency matrix left to remove the column corresponding to the node to be removed
        memmove(
            &adj_matrix_graph->adjacency_matrix[i][node_index],
            &adj_matrix_graph->adjacency_matrix[i][node_index + 1],
            (graph->num_nodes - 1 - node_index) * sizeof(int) 
        );
    }

    // Free the last column of the adjacency matrix after shifting it
    for (int i = 0; i < graph->num_nodes - 1; i++) {
        adj_matrix_graph->adjacency_matrix[i] = (int *)realloc(adj_matrix_graph->adjacency_matrix[i], (graph->num_nodes - 1) * sizeof(int)); // Reallocate memory for each row of the adjacency matrix to accommodate the new number of nodes after removing the node
    }

    adj_matrix_graph->num_nodes--; // Decrement the number of nodes in the adjacency matrix graph after removing the node

}

void *get_node_value(Node *node, int *data_size, int *data_type) {
    /*
    Every time we add a new DataType we nedd to update this function.
    This function permit to get the value of a node whithout nowing the type of the data stored in the node,
    we can use the data_type output parameter to know the type of the data and the data_size to know the size of the data.

    if the node is null or the type is invalid the function return NULL and set data_size to 0 and datatype to -1.
    */

    if (node == NULL) { *data_size = 0; *data_type = -1; return NULL;} // Check for NULL pointer befor accessing the node

    switch (node->type) { // Use the type field to determine how to interpret the data
        case INT:
            *data_size = node->size;
            *data_type = INT;
            return (int *)node->data;
        case FLOAT:
            *data_size = node->size;
            *data_type = FLOAT;
            return (float *)node->data;
        case DOUBLE:
            *data_size = node->size;
            *data_type = DOUBLE;
            return (double *)node->data;
        case CHAR:
            *data_size = node->size;
            *data_type = CHAR;
            return (char *)node->data;
        default:
            *data_size = 0;
            *data_type = -1; // Invalid type
            return NULL;
    }
}
