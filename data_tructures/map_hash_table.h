#ifndef MAP_HASH_TABLE_H
#define MAP_HASH_TABLE_H

typedef struct Entry
{
    char *key;          
    void *value;        
    struct Entry *next; 
} Entry;

typedef struct
{
    Entry **buckets; 
    int num_buckets; 
    int size;        
} HashTable;

//Prototypes of the functions for the hash table
HashTable *init_hash_table(int num_buckets);
void free_hash_table(HashTable *hash_table);
void hash_map_put(HashTable *hash_table, const char *key, void *value);
void *hash_map_get(HashTable *hash_table, const char *key);
void hash_map_remove(HashTable *hash_table, const char *key);

#endif // MAP_HASH_TABLE_H