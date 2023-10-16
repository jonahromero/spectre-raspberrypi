
#ifndef COMMON_SPECTRE
#define COMMON_SPECTRE


#define REPEAT(x) for(int repeat_idx_##x=0; repeat_idx_##x < x; repeat_idx_##x++)
#define NUM_SAMPLES 2000
#define L1_SIZE 32768
#define L2_SIZE 262144
#define L3_SIZE 6291456

// Basic Helpers:
inline char* allocate_huge_page(size_t page_size){
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

inline uint64_t print_buffer(uint64_t* arr, size_t size) {
        printf("[");
        for(size_t i=0;i<size - 1;i++) {
                printf("%lu", arr[i]);
                if(size - 2 != i) printf(",");
        }
        printf("]\n");
}

inline uint64_t average(uint64_t* arr, size_t size)
{
        uint64_t sum = 0;
        for(int i=0; i<size - 1;i++) sum += arr[i];
        return sum / size;
}

// Spectre Specific Code:

typedef struct
{
        uint64_t l1, l2, l3, dram;
} CacheStats;

typedef enum {
        L1, L2, L3, DRAM
} MemoryLevel;

inline CacheStats record_cache_stats()
{
        uint64_t dram_samples[NUM_SAMPLES] = {}, l1_samples[NUM_SAMPLES] = {}, l2_samples[NUM_SAMPLES] = {}, l3_samples[NUM_SAMPLES] = {};
        char* eviction_buffer = malloc(2* L3_SIZE * sizeof(char));
        char* line_buffer = malloc(64 * sizeof(char));
        // l1 accesses
        for(int i = 0; i < NUM_SAMPLES; i++){
                line_buffer[0] = 'a';
                l1_samples[i] = measure_one_block_access_time((uint64_t)(void*)line_buffer);
        }
        // l2 accesses
        for(int i = 0; i < NUM_SAMPLES; i++) {
                line_buffer[0] = 'a';
                REPEAT(10) for(int j=0; j<(2*L1_SIZE)/64; j++) eviction_buffer[j*64] = 'a';
                l2_samples[i] = measure_one_block_access_time((uint64_t)(void*)line_buffer);
        }
        // l3 accesses
        for(int i = 0; i < NUM_SAMPLES; i++){
                clflush(line_buffer);
                line_buffer[0] = 'a';
                REPEAT(3) for(int j=0;j<(2*L2_SIZE)/64;j++) eviction_buffer[j*64] = 'a';
                l3_samples[i] = measure_one_block_access_time((uint64_t)(void*)line_buffer);
        }
        // dram access
        for(int i = 0; i < NUM_SAMPLES; i++) {
                clflush(line_buffer);
                dram_samples[i] = measure_one_block_access_time((uint64_t)(void*)line_buffer);
        }
        free(line_buffer);
        free(eviction_buffer);
        return (CacheStats){
                average(l1_samples, NUM_SAMPLES),
                average(l2_samples, NUM_SAMPLES),
                average(l3_samples, NUM_SAMPLES),
                average(dram_samples, NUM_SAMPLES)
        };
}

inline MemoryLevel determine_memory_level(CacheStats stats, void* address)
{
        uint64_t access_time = time_access(address);
        uint64_t l1_diff =   abs(access_time - stats.l1),
                 l2_diff =   abs(access_time - stats.l2),
                 l3_diff =   abs(access_time - stats.l3),
                 dram_diff = abs(access_time - stats.dram);
        if(l1_diff < l2_diff && l1_diff < l3_diff && l1_diff < dram_diff) return L1;
        else if(l2_diff < l3_diff && l2_diff < dram_diff) return L2;
        else if(l3_diff < dram_diff) return L3;
        else return DRAM;
}

inline void warmup()
{
        const size_t page_size = 1 << 12;
        char* small_page = malloc(page_size * sizeof(char));
        REPEAT(2000) for(int i = 0; i < page_size - 1; i++){
                small_page[i] = small_page[i] * 7 + small_page[i] / 5;
        }
        free(small_page);
}

#endif
