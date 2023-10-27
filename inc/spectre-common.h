
#ifndef COMMON_SPECTRE
#define COMMON_SPECTRE
#include "labspectre.h"

#define REPEAT(x) for(int repeat_idx_##x=0; repeat_idx_##x < x; repeat_idx_##x++)
#define max(a, b) ((a) > (b) ? (a) : (b))
#define NUM_SAMPLES 900

// Basic Helpers:
static char* allocate_huge_page(size_t page_size) {
  void *buf= mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB, -1, 0);

  if (buf == (void*) - 1) {
     perror("mmap() error\n");
     exit(EXIT_FAILURE);
  }
  // The first access to a page triggers overhead associated with
  // page allocation, TLB insertion, etc.
  // Thus, we use a dummy write here to trigger page allocation
  // so later access will not suffer from such overhead.
  *((char *)buf) = 1; // dummy write to trigger page allocation
  for(int i = 0; i < page_size / 64; i++){
        *((char*)buf + i * 64) = 1;
  }
  return (char*)buf;
}

static void print_buffer(uint32_t* arr, size_t size) {
        printf("[");
        for(size_t i = 0; i < size - 1; i++) {
                printf("%u", arr[i]);
                if(size - 2 != i) printf(",");
        }
        printf("]\n");
}

static uint32_t average(uint32_t* arr, size_t size)
{
        uint64_t sum = 0;
        for(int i = 0; i < size - 1; i++)
            sum += arr[i];
        return sum / size;
}

// Spectre Specific Code:

typedef struct
{
        uint32_t l1, l2, dram;
} CacheStats;

typedef enum {
        L1, L2, DRAM
} MemoryLevel;

static size_t calculate_bytes_in_cache(CacheSize size)
{
        return size.line_size * size.sets * size.associativity * 4;
}

static void evict_all_cache()
{
        const size_t L2_SIZE = calculate_bytes_in_cache(get_cache_info(1, 0));
        char* eviction_buffer = malloc(L2_SIZE * 4);
        for (size_t i = 0; i < (L2_SIZE * 3) / 16; i++)
        {
                eviction_buffer[i*16] = 'a';
        }
        free(eviction_buffer);
}

static CacheStats record_cache_stats()
{
        const size_t L1_SIZE = calculate_bytes_in_cache(get_cache_info(0, 0));
        const size_t L2_SIZE = calculate_bytes_in_cache(get_cache_info(1, 0));
        printf("L1 Size:%lu\n", L1_SIZE);
        printf("L2 Size:%lu\n", L2_SIZE);
        // use clidr to determine number of caches that exist
        for (int i = 0; i < 2; i++){
            CacheSize size = get_cache_info(i, 0);
            printf("Data Cache %d:\n", i);
            printf("\tsets: %d\n", size.sets);;
            printf("\tassociativity: %d\n", size.associativity);
            printf("\tline size: %d\n\n", size.line_size);
        }
        fflush(stdout);

        uint32_t dram_samples[NUM_SAMPLES] = {},
                 l1_samples[NUM_SAMPLES] = {},
                 l2_samples[NUM_SAMPLES] = {};
        size_t largest_cache_size = max(L1_SIZE, L2_SIZE);
        char* eviction_buffer = malloc(2* largest_cache_size * sizeof(char));
        char* line_buffer = malloc(16 * sizeof(char));

        // l1 accesses
        for(int i = 0; i < NUM_SAMPLES; i++) {
                //REPEAT(10) for(int j=0; j<(2*L2_SIZE)/16; j++) eviction_buffer[j*16] = 'a';
                line_buffer[0] = 'a';
                l1_samples[i] = time_access(line_buffer);
        }
        // l2 accesses
        for(int i = 0; i < NUM_SAMPLES; i++) {
                line_buffer[0] = 'a';
                REPEAT(10) for(int j=0; j<(2*L1_SIZE)/16; j++) eviction_buffer[j*16] = 'a';
                l2_samples[i] = time_access(line_buffer);
        }
        // dram access
        for(int i = 0; i < NUM_SAMPLES; i++) {
                //flush_address(line_buffer);
                __sync_synchronize();
                //REPEAT(10) for(int j=0; j<(2*L2_SIZE)/16; j++) eviction_buffer[j*16] = 'a';
                dram_samples[i] = time_access(line_buffer);
        }
        print_buffer(l1_samples, NUM_SAMPLES);
        print_buffer(l2_samples, NUM_SAMPLES);
        print_buffer(dram_samples, NUM_SAMPLES);

        free(line_buffer);
        free(eviction_buffer);
        CacheStats stats = (CacheStats){
                average(l1_samples, NUM_SAMPLES),
                average(l2_samples, NUM_SAMPLES),
                average(dram_samples, NUM_SAMPLES)
        };

        printf("L1 Average: %u\n", stats.l1);
        printf("L2 Average: %u\n", stats.l2);
        printf("DRAM Average: %u\n", stats.dram);
        return stats;
}

static MemoryLevel determine_memory_level(CacheStats stats, void* address)
{
        uint32_t access_time = time_access(address);
        //printf("Time to acces:%u\n", access_time);
#define UNSIGNED_ABS_DIFF(a, b) ((a) > (b) ? (a) - (b) : (b) - (a))
        uint32_t l1_diff = UNSIGNED_ABS_DIFF(access_time, stats.l1),
                 l2_diff = UNSIGNED_ABS_DIFF(access_time, stats.l2),
                 dram_diff = UNSIGNED_ABS_DIFF(access_time, stats.dram);
        //printf("l1_diff:%u, l2_diff:%u, dram_diff:%u", l1_diff, l2_diff, dram_diff);
        if(l1_diff < l2_diff && l1_diff < dram_diff) return L1;
        else if(l2_diff < dram_diff) return L2;
        else return DRAM;
}

static void warmup()
{
        const size_t page_size = 1 << 12;
        char* small_page = malloc(page_size * sizeof(char));
        REPEAT(2000) for(int i = 0; i < page_size - 1; i++){
                small_page[i] = small_page[i] * 7 + small_page[i] / 5;
        }
        free(small_page);
}

#endif
