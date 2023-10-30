
#ifndef SPECTRE_SOLUTION
#define SPECTRE_SOLUTION
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "labspectre.h"
#define REPEAT(x) for(int repeat_idx_##x=0; repeat_idx_##x < x; repeat_idx_##x++)

typedef struct
{
    uint32_t l1, l2, dram;
} CacheStats;

typedef enum {
    L1, L2, DRAM
} MemoryLevel;

void warmup();
void evict_all_cache();
const char* memory_level_to_str(MemoryLevel level);
CacheStats record_cache_stats();
MemoryLevel determine_memory_level(CacheStats stats, void* address);

#endif
