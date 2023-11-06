
#include "spectre_solution.h"
#include <sys/mman.h>

#define UNSIGNED_ABS_DIFF(a, b) ((a) > (b) ? (a) - (b) : (b) - (a))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define HUGE_PAGE_SIZE (1 << 21)
#define L1_SIZE (64*256*2)
#define L2_SIZE (64*1024*16)
#define ALIGN_FORWARD(x, alignment) (void*)(((uint64_t)(x) + (alignment) - 1) & ~(alignment - 1))

// Helper functions:

char* allocate_2mb_huge_page()
{
    fflush(stdout);
    void* buf = mmap(NULL, HUGE_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);

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

// Spectre Specific Code

char* get_eviction_buffer()
{
    static char* eviction_buffer = NULL;
    if (eviction_buffer == NULL) {
        eviction_buffer = allocate_2mb_huge_page();
    }
    return eviction_buffer;
}

char* get_l2_buffer() {
    return ALIGN_FORWARD(get_eviction_buffer(), L2_SIZE);
}

size_t get_eviction_buffer_size() { return HUGE_PAGE_SIZE; }

void evict_all_cache()
{
    char* l2_cache = get_l2_buffer();
    for (uint64_t set = 0; set < 1024; set++)
    {
        for (uint64_t way = 0; way < 16; way++)
        {
            volatile char* line = (void*)((uint64_t)l2_cache | (way << 16) | (set << 6));
            REPEAT(3) *line = 'a';
        }
    }
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

uint64_t average(uint64_t* arr, size_t size)
{
    uint64_t sum = 0;
    for(int i = 0; i < size - 1; i++)
        sum += arr[i];
    return sum / size;
}

void evict_address(void* addr)
{
    asm volatile("dc civac, %0"::"r"(addr));
}

CacheStats generate_cache_stats(size_t samples)
{
    printf("Eviction Buffer: %p\n", get_eviction_buffer());
    //assert_can_read_cycle_count();
    CacheStats retval = (CacheStats) {
        .num_samples = samples,
        .l1_samples = malloc(sizeof(uint64_t) * samples),
        .l2_samples = malloc(sizeof(uint64_t) * samples),
        .dram_samples = malloc(sizeof(uint64_t) * samples)
    };

    char* eviction_buffer = get_eviction_buffer();
    char* line_buffer = malloc(64 * sizeof(char));

    // l1 accesses
    for(int i = 0; i < samples; i++)
    {
        line_buffer[0] = 'a';
        retval.l1_samples[i] = time_access(line_buffer);
    }
    // l2 accesses
    for(int i = 0; i < samples; i++)
    {
        line_buffer[0] = 'a';
        for(int j = 0; j < L1_SIZE; j += 64) {
            eviction_buffer[j] = 'a';
        }
        retval.l2_samples[i] = time_access(line_buffer);
    }
    // dram access
    for(int i = 0; i < samples; i++)
    {
        REPEAT(6) evict_all_cache();
        retval.dram_samples[i] = time_access(line_buffer);
    }

    retval.l1 = average(retval.l1_samples, samples);
    retval.l2 = average(retval.l2_samples, samples);
    retval.dram = average(retval.dram_samples, samples);

    free(line_buffer);
    return retval;
}

void destroy_cache_stats(CacheStats stats)
{
    free(stats.l1_samples);
    free(stats.l2_samples);
    free(stats.dram_samples);
}

void print_buffer(uint64_t* arr, size_t size) {
    printf("[");
    for(size_t i = 0; i < size - 1; i++) {
            printf("%lu", arr[i]);
            if(size - 2 != i) printf(",");
    }
    printf("]\n");
}

void print_cache_stats(CacheStats stats)
{
    printf("L1 Size:%lu\n", L1_SIZE);
    printf("L2 Size:%lu\n", L2_SIZE);

    printf("L1 Samples: ");
    print_buffer(stats.l1_samples, stats.num_samples);
    printf("L2 Samples: ");
    print_buffer(stats.l2_samples, stats.num_samples);
    printf("DRAM Samples: ");
    print_buffer(stats.dram_samples, stats.num_samples);

    printf("L1 Average: %u\n", stats.l1);
    printf("L2 Average: %u\n", stats.l2);
    printf("DRAM Average: %u\n", stats.dram);
}

void print_python_eviction_set_graph()
{
    char* target = malloc(4096);
    char* l2_cache = get_l2_buffer();
    const uint64_t NUMBER_OF_EVICTION_SETS = 1024;
    const uint64_t TRIALS = 1000;
    uint64_t results[1024] = {};
    REPEAT(TRIALS)
    for (uint64_t es = 0; es < NUMBER_OF_EVICTION_SETS; es++) {
        evict_address(target);
        asm volatile("dsb sy");
        *target = 'a';
        asm volatile("dsb sy");
        for (uint64_t set = 0; set < es; set++)
        {
            for (uint64_t way = 0; way < 16; way++)
            {
                volatile char* line = (void*)((uint64_t)l2_cache | (way << 16) | (set << 6));
                REPEAT(2) *line = 'a';
            }
        }
        results[es] += time_access(target);
    }
    
    printf("import matplotlib.pyplot as plt\n\n");
    printf("data = [");
    for (uint64_t es = 0; es < NUMBER_OF_EVICTION_SETS; es++) {
        printf("[%d, %lu]", es, results[es] / TRIALS);
        if (es != NUMBER_OF_EVICTION_SETS - 1) {
            printf(",\n");
        }
    } 
    printf("]\n\n");
    printf("# Create a plot\n");
    printf("x, y = zip(*data)\n\n");
    printf("# Create a plot\n");
    printf("plt.plot(x, y)  # 'o-' is for dotted line with circle markers\n\n");
    printf("# Set the title and labels\n");
    printf("plt.title('Data Plot')\n");
    printf("plt.xlabel('Eviction Sets Used')\n");
    printf("plt.ylabel('Latencies')\n\n");
    printf("# Save the plot to a file\n");
    printf("plt.savefig('plot.png')\n\n");
    printf("# Show the plot if desired\n");
    printf("# plt.show()\n");
    free(target);
}