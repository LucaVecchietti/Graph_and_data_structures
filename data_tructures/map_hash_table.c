#include<stdlib.h>
#include<string.h>
#include "map_hash_table.h"

unsigned long hash_function(const char *str) {
    /*
    This function calculates the hash value of a string using the djb2 algorithm.
    It takes a string as input and returns an unsigned long integer as the hash value.
    */
    unsigned long hash = 5381; // Initialize the hash value to a large prime number
    int c; // Variable to store the current character of the string

    while ((c = *str++)) { // Iterate through each character of the string until we reach the null terminator
        hash = ((hash << 5) + hash) + c; // Update the hash value using the djb2 algorithm
    }

    return hash; // Return the calculated hash value
}

void hash_map_put(HashTable *hash_table, const char *key, void *value){
    /*
    This function inserts a key-value pair into the hash table.
    It takes a pointer to the hash table, a string key, and a void pointer to the value.
    */

    unsigned long hash_value = hash_function(key); // Calculate the hash value of the key

    int bucket_index = hash_value % hash_table->num_buckets; // calculate the index of the bucket where the entry should be interested by the hash value
    Entry *new_entry = (Entry *)malloc(sizeof(Entry)); // Allocate memory for the new entry
    new_entry->key = strdup(key); // Duplicate the key string and assign it to the new entry
    new_entry->value = value; // Assign the value to the new entry
    new_entry->next = hash_table->buckets[bucket_index]; // Set the next pointer of the new entry to the current head of the bucket's linked list (if any)
    hash_table->buckets[bucket_index] = new_entry; // Update the head of the bucket's linked list to point to the new entry

    // Check if the load factor of the hash table exceeds a certain threshold (e.g., 0.75) and rehash if necessary
    if ((float)hash_table->size / hash_table->num_buckets > 0.75) {
        hash_map_rehash(hash_table);
    }
}

void *hash_map_get(HashTable *hash_table, const char *key) {
    /*
    This function retrieves the value associated with a given key from the hash table.
    It takes a pointer to the hash table and a string key as input, and returns a void pointer to the value if the key is found, or NULL if the key is not found.
    */

    unsigned long hash_value = hash_function(key); // Calculate the hash value of the key

    int bucket_index = hash_value % hash_table->num_buckets; // Calculate the index of the bucket where the entry should be located by the hash value
    Entry *current_entry = hash_table->buckets[bucket_index]; // Get the head of the bucket's linked list

    while (current_entry != NULL) { // Iterate through the linked list of entries in the bucket
        if (strcmp(current_entry->key, key) == 0) { // If we find an entry with a matching key
            return current_entry->value; // Return the value associated with the key
        }
        current_entry = current_entry->next; // Move to the next entry in the linked list
    }

    return NULL; // If we reach the end of the linked list without finding a matching key, return NULL
}

HashTable *init_hash_table(int num_buckets) {
    /*
    This function initializes a new hash table with a specified number of buckets.
    It takes the number of buckets as input and returns a pointer to the newly created hash table.
    */

    HashTable *hash_table = (HashTable *)malloc(sizeof(HashTable)); // Allocate memory for the hash table
    hash_table->num_buckets = num_buckets; // Set the number of buckets in the hash table
    hash_table->buckets = (Entry **)calloc(num_buckets, sizeof(Entry *)); // Allocate memory for the array of bucket pointers and initialize them to NULL

    return hash_table; // Return the pointer to the initialized hash table
}

void free_hash_table(HashTable *hash_table) {
    /*
    This function frees the memory allocated for the hash table and its entries.
    It takes a pointer to the hash table as input and does not return anything.
    */

    for (int i = 0; i < hash_table->num_buckets; i++) { // Iterate through each bucket in the hash table
        Entry *current_entry = hash_table->buckets[i]; // Get the head of the bucket's linked list

        while (current_entry != NULL) { // Iterate through the linked list of entries in the bucket
            Entry *temp_entry = current_entry; // Store a temporary pointer to the current entry
            current_entry = current_entry->next; // Move to the next entry in the linked list

            free(temp_entry->key); // Free the memory allocated for the key string
            free(temp_entry); // Free the memory allocated for the entry itself
        }
    }

    free(hash_table->buckets); // Free the memory allocated for the array of bucket pointers
    free(hash_table); // Free the memory allocated for the hash table structure itself
}

void hash_map_reash(HashTable *hash_table) {
    /*
    This function rehashes the hash table by creating a new hash table with a larger number of buckets and re-inserting all the existing entries into the new hash table.
    It takes a pointer to the hash table as input and does not return anything.
    */

    int old_size = hash_table->num_buckets;
    int new_size = old_size * 2;    // Calculate the new number of buckets (doubling the current number)

    Entry **new_buckets = (Entry **)calloc(new_size, sizeof(Entry *)); // Allocate memory for the new array of bucket pointers and initialize them to NULL

    for (int i = 0; i < hash_table->num_buckets; i++) { // Iterate through each bucket in the old hash table
        Entry *current_entry = hash_table->buckets[i]; // Get the head of the bucket's linked list

        while (current_entry != NULL) { // Iterate through the linked list of entries in the bucket
            
            Entry *next = current_entry->next; // Store a pointer to the next entry before re-inserting the current entry into the new hash table

            unsigned long hash_value = hash_function(current_entry->key) % new_size; // Calculate the hash value of the current entry's key

            current_entry->next = new_buckets[hash_value]; // Insert the current entry into the new hash table
            new_buckets[hash_value] = current_entry; // Update the head of the bucket's linked list in the new hash table to point to the current entry

            current_entry = next; // Move to the next entry in the linked list of the old bucket
        }
    }

    free(hash_table->buckets); // Free the memory allocated for the old array of bucket pointers
    hash_table->buckets = new_buckets; // Update the pointer to the new array of bucket pointers in the hash table
    hash_table->num_buckets = new_size; // Update the number of buckets in the hash table to the new size
}