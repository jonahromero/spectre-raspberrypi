
#include "spectre_solution.h"
#include <sys/mman.h>

#define UNSIGNED_ABS_DIFF(a, b) ((a) > (b) ? (a) - (b) : (b) - (a))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define NUM_SAMPLES 900
#define HUGE_PAGE_SIZE (1 << 21)
#define IS_ALIGNED(ptr, alignment) (((uint64_t)(ptr) & ((uint64_t)(alignment) - 1)) == 0)

#define L1_SIZE (64*256*2)
#define L2_SIZE (64*1024*16)

// Helper functions:

char* allocate_huge_page()
{
    fflush(stdout);
    void* buf = mmap(NULL, (2 * 1024 * 1024), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);

    if (buf == (void*) - 1) {
        perror("mmap() error\n");
        exit(EXIT_FAILURE);
    }
    // The first access to a page triggers overhead associated with
    // page allocation, TLB insertion, etc.
    // Thus, we use a dummy write here to trigger page allocation
    // so later access will not suffer from such overhead.
    *((char *)buf) = 1; // dummy write to trigger page allocation
    for(int i = 0; i < HUGE_PAGE_SIZE; i += 64){
            *((char*)buf + i) = 1;
    }
    return (char*)buf;
}

const char* memory_level_to_str(MemoryLevel level)
{
    switch (level)
    {
        case L1: return "L1";
        case L2: return "L2";
        case DRAM: return "DRAM";
        default: return "Unknown";
    }
}

void print_buffer(uint32_t* arr, size_t size) {
    printf("[");
    for(size_t i = 0; i < size - 1; i++) {
            printf("%u", arr[i]);
            if(size - 2 != i) printf(",");
    }
    printf("]\n");
}

uint32_t average(uint32_t* arr, size_t size)
{
    uint64_t sum = 0;
    for(int i = 0; i < size - 1; i++)
        sum += arr[i];
    return sum / size;
}

// Spectre Specific Code

char* get_eviction_buffer()
{
    static char* eviction_buffer = NULL;
    if (eviction_buffer == NULL) {
        eviction_buffer = allocate_huge_page();
    }
    return eviction_buffer;
}

size_t get_eviction_buffer_size() { return HUGE_PAGE_SIZE; }

void evict_all_cache()
{
    char* eviction_buffer = get_eviction_buffer();
    for (size_t i = 0; i < get_eviction_buffer_size(); i += 64)
    {
        eviction_buffer[i] = 'a';
    }
}

void create_eviction_set_graph()
{
    char* eviction_buffer = get_eviction_buffer();
    assert(IS_ALIGNED(eviction_buffer, HUGE_PAGE_SIZE) && "Not huge page aligned.");

}

void assert_can_read_cycle_count()
{
    uint64_t read_reg;
    asm volatile("mrs %0, PMUSERENR_EL0":"=r"(read_reg));
    if (!(read_reg & 1) || !((read_reg >> 2) & 1)) {
        fprintf(stderr, "Status Failed:%#08x\n", read_reg);
        fflush(stderr);
        exit(EXIT_FAILURE);
    }
}

CacheStats record_cache_stats()
{
    assert_can_read_cycle_count();
    printf("L1 Size:%lu\n", L1_SIZE);
    printf("L2 Size:%lu\n", L2_SIZE);

    uint32_t dram_samples[NUM_SAMPLES] = {},
                l1_samples[NUM_SAMPLES] = {},
                l2_samples[NUM_SAMPLES] = {};
    size_t largest_cache_size = max(L1_SIZE, L2_SIZE);
    char* eviction_buffer = malloc(2* largest_cache_size * sizeof(char));
    char* line_buffer = malloc(16 * sizeof(char));

    // dram access
    for(int i = 0; i < NUM_SAMPLES; i++) {
        //flush_address(line_buffer);
        REPEAT(1) evict_all_cache();
        //REPEAT(10) for(int j=0; j<(2*L2_SIZE)/16; j++) eviction_buffer[j*16] = 'a';
        dram_samples[i] = time_access(line_buffer);
    }
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

MemoryLevel determine_memory_level(CacheStats stats, void* address)
{
    uint32_t access_time = time_access(address);
    //printf("Time to acces:%u\n", access_time);
    uint32_t l1_diff = UNSIGNED_ABS_DIFF(access_time, stats.l1),
                l2_diff = UNSIGNED_ABS_DIFF(access_time, stats.l2),
                dram_diff = UNSIGNED_ABS_DIFF(access_time, stats.dram);
    //printf("l1_diff:%u, l2_diff:%u, dram_diff:%u", l1_diff, l2_diff, dram_diff);
    if(l1_diff < l2_diff && l1_diff < dram_diff) return L1;
    else if(l2_diff < dram_diff) return L2;
    else return DRAM;
}

void warmup()
{
    const size_t page_size = 1 << 12;
    char* small_page = malloc(page_size * sizeof(char));
    REPEAT(2000) for(int i = 0; i < page_size - 1; i++){
            small_page[i] = small_page[i] * 7 + small_page[i] / 5;
    }
    free(small_page);
}
