# User Activity

## Append New Relation

We need to appen new relation ti improove new edge insertion.
This means thet we have to modify Relation list structure and free list sistem. 



### New Struct NodeRelationList

 - **node_id**: The node id.
 - **batch_size**: Already exist buty we neeed a standar size (of 4069 bytes) to implement freelists and batch extensions.
 - **free_bytes**: The quantity of free bytes in the batch.
 - **next_offset**: The offset of the next extention of the currents list of relation.
 - **head**: The position of the batch in the sequence of the batches of the list (1 -> 2 -> 3 ...).
 - **is_deleted**: Is a boolean value (int8_t), so it can be 1 if the list is deleted and 0 if is not.

Every `NodeRelationList` will be only 2205 bytes, so we can use only a free list (`rel_<node_id>_4096.dat`) and every batch non completly full can be used utill ti will be.

the struct will be:
```cpp
struct NodeRelationList
{
    uint64_t node_id;       // Id of the node
    uint64_t type_count;    // Number of relation types
    uint16_t batch_size;    // Dimension of the batch
    uint16_t free_bytes;    // Number of free bytes (multiple of 272, min 0, max 2176)
    uint64_t head;          // The serial number of the header (1 if is the first page/batch the 2 -> 3 -> 4 ...)
    uint8_t is_deleted;     // Is a boolean value that say if a batch is deleted;
}
```

After every **Header** will be maximum of **8 lines** of **272 bytes**. Every lines will be composed by: 

```
[uint64_t edge_offset][uint64_t edge_count][uint8_t name_length][string [255] name]

// edge_offset: position of the edges of a particula type.
// edge_count: number of edges of the current type that.
// name_length: name of the type of relation.
// name: The neme of the type of relation.
```

### New Methhod to manage Edges

We will improve at the same way the `Edge` struct:

```cpp
struct Edge
{
    uint64_t id;            // Edge  ID
    int64_t weight;         // Weight of the edge
    uint64_t to_node;       // Destination node idx on nodes.idx file [ to_node → NodeIndex(id == to_node)]
    uint64_t from_node;     // Source node idx on nodes.idx file [ from_node → NodeIndex(id == from_node)]
    uint64_t prev_offset;   // Previouse edge offset of the same node.
    uint64_t next_offset;   // Next edge offset of the same node.
};
```

Edges will not been write in sequence anymore, but they will be write always in append to the file of the edges or where is the first free offset pointed by the free list, like a linked list. 
This method che perform **insert** in **O(1)** insted of **O(n)** like the previouse one.

This will require a change of the struct of freelists and meta data.