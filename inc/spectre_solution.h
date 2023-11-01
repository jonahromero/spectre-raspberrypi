
#ifndef SPECTRE_SOLUTION
#define SPECTRE_SOLUTION
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "labspectre.h"

// Repeat any statement of block by placing macro infront. i.e REPEAT(2) i++;
#define REPEAT(x) for(int repeat_idx_##x=0; repeat_idx_##x < x; repeat_idx_##x++)

/*
 * Generate Statistics about the cache in order to perform
 * a side-channel attack.
*/
typedef struct
{
    // number of samples we take for each memory domain
    size_t num_samples;
    // dynamically allocated lists containing latencies for accessing an address
    // in a given memory domain. Each element is a seperate time trial
    uint64_t *l1_samples, *l2_samples, *dram_samples;
    // the average latency for l1, l2, and dram
    uint64_t l1, l2, dram;
} CacheStats;

CacheStats generate_cache_stats(size_t samples);
void destroy_cache_stats(CacheStats stats);
void print_cache_stats(CacheStats stats);

/*
 * evict_all_cache
 * evicts all the lines from cache
*/
void evict_all_cache();

/*
 * print_numpy_eviction_set_graph
 * prints python code that when executed produces 
 * a matplotlib graph, relating the number of eviction sets
 * accessed and the latency of an access
*/
void print_python_eviction_set_graph();

#endif
