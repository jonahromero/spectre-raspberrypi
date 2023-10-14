#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>

#include "labspectre.h"
#include "labspectreipc.h"

/*
 * mfence
 * Adds a memory fence
 */
static inline void mfence() {
    asm volatile("mfence");
}

/*
 * clflush
 * Flushes an address from the cache for you
 *
 * Arguments:
 *  - addr: A virtual address whose cache line we should flush
 *
 * Returns: None
 * Side Effects: Flushes a cache line from the cache
 */
void clflush(void *addr)
{
   asm volatile("mcr p15, 0, %0, c7, c6, 1"::"r"(addr));
}

/*
 * rdtsc
 * Reads the current timestamp counter
 *
 * Returns: Current value of TSC
 */
uint64_t rdtsc(void)
{
	uint32_t pmuseren, pmcountenset, counter;
	asm volatile("mrc p15, 0, %0, c9, c14, 0":"=r"(pmuseren));
	assert((pmuseren & 0x1) && "Must have perf monitor access");
	asm volatile("mrc p15, 0, %0, c9, c12, 1":"=r"(pmcountenset));
    assert((pmcountenset & 0x80000000) && "Counter must be enabled to count");
	asm volatile("mrc p15, 0, %0, c9, c13, 0":"=r"(counter));
	return counter;
}

/*
 * time_access
 * Returns the time to access an address
 */
uint64_t time_access(void *addr)
{
    volatile unsigned int temp;
    uint64_t time1, time2;
    time1 = rdtsc();
    temp = *(unsigned int *)addr;
    time2 = rdtsc();
    return time2 - time1;
}

/*
 * init_shared_memory
 * Load all the pages of shared memory, initialize them, and flush them from the cache.
 *
 * Arguments:
 *  - shared_memory: Pointer to a shared memory region
 *  - len: Size of the shared memory region to initialize
 *
 * Returns: None
 * Side Effects: Initializes some memory and flushes it from the cache.
 */
void init_shared_memory(char *shared_memory, size_t len) {
    for (int i = 0; i < (len / SHD_SPECTRE_LAB_PAGE_SIZE); i++) {
        shared_memory[SHD_SPECTRE_LAB_PAGE_SIZE * i] = 0x41;
        clflush(&shared_memory[SHD_SPECTRE_LAB_PAGE_SIZE * i]);
    }
}
