#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>

#include <linux/kernel.h>
#include <sys/syscall.h>
#include <linux/unistd.h>
#include <errno.h>

#include "labspectre.h"
#include "labspectreipc.h"

/*
 * mfence
 * Adds a memory fence
 */
static inline void mfence() {
    uint32_t undefined=0;
    //asm volatile("mcr p15, 0, %0, c7, c10, 4"::"r"(undefined));
    //asm volatile("mcr p15, 0, %0, c7, c10, 5"::"r"(undefined));
    asm volatile("DMB");
}

/*
 * pm_enable_cycle_count
 * Enables the arm performance monitor
 *
 * Returns: 0 if succesfully completed
*/
void pm_enable_cycle_count() {
    long rc = syscall(451);
    if (rc != 0) {
        fprintf(stderr, "Unable to enable cycle count. ");
        if (errno == ENOSYS) {
            fprintf(stderr, "System call number does not exist.");
        }
        fprintf(stderr, "\n");
    }
    else {
        printf("Enabled performance monitor cycle count.\n");
    }
}

/*
 * pm_quick_cycle_count
 * Reads the PM cycle count from user space
 *
 * Returns: cycle count
*/
uint32_t pm_quick_cycle_count()
{
    uint32_t counter;
    asm volatile("mrc p15, 0, %0, c9, c13, 0":"=r"(counter));
    return counter;
}

/*
 * pm_test_cycle_count
 * Debug function that tests whether or not we can read performance counter
 * from user space.
 *
 * Returns: cycle count
*/
static bool pm_test_cycle_count() {
    uint32_t pmuseren, pmcountenset, counter;
    asm volatile("mrc p15, 0, %0, c9, c14, 0":"=r"(pmuseren));
    if(pmuseren & 0x1) {
        fprintf(stderr, "PM Cycle Count is not enabled for user");
        return false;
    }
    asm volatile("mrc p15, 0, %0, c9, c12, 1":"=r"(pmcountenset));
    if (pmcountenset & 0x80000000) {
        fprintf(stderr, "Counter must be enabled to count");
        return false;
    }
    asm volatile("mrc p15, 0, %0, c9, c13, 0":"=r"(counter));
    return true;
}

/*
 * flush_address
 * Flushes an address from the cache for you
 * utilizing syscall: sys_flush_address
 *
 * Arguments:
 *  - addr: A virtual address whose cache line we should flush
 *
 * Returns: None
 * Side Effects: Flushes a cache line from the cache
 */
void flush_address(void* ptr)
{
    long rc = syscall(453, ptr);
    assert(rc == 0 && "Check sys_flush_address is implemented");
}

void flush_address_range(void* addr, size_t size) {
    long rc = syscall(454, addr, size);
    assert(rc == 0 && "Check sys_flush_address_range is implemented");
}

/* get_cache_info + CacheSize
 * Allows user to query the cache dimensions
 *
 * Arguments:
 *  - cache_id: a cache id 1-7
 *  - is_data_cache: whether or not the specified cache
 *    is a data or an instruction cache
 * 
 * Returns: A struct containing various size information
*/
CacheSize get_cache_info(size_t cache_id, int is_data_cache) {
    // Reference CCSIDR on ARM processors for more detailed description:
    // bits 0-2: line size
    // bits 12-3: associativity
    // bits 27-13: number of sets
    uint32_t cache_size;
    long rc = syscall(455, cache_id, is_data_cache, &cache_size);
    if (rc != 0) {
        fprintf(stderr, "Unable to retrieve cache information");
        if (errno == ENOSYS) {
            fprintf(stderr, "System call number does not exist.");
        }
        fprintf(stderr, "\n");
    }
    return (CacheSize) {
        .line_size     = 1 << (((cache_size >> 0) & ((uint32_t)(0x1 << 3) - 1)) + 2),
        .associativity =      ((cache_size >> 3) & ((uint32_t)(0x1 << 10) - 1)) + 1,
        .sets          =      ((cache_size >> 13) & ((uint32_t)(0x1 << 15) - 1)) + 1,
    };
}

/*
 * time_access
 * Returns the time to access an address
 */
uint32_t time_access(void *addr)
{
    uint32_t time;
    long rc = syscall(456, addr, &time);
    assert(rc != -1 && "no syscall");
    return time;
    /*
    volatile unsigned int temp;
    uint32_t time1, time2;
    time1 = pm_quick_cycle_count();
    mfence();
    temp = *(unsigned int *)addr;
    mfence();
    time2 = pm_quick_cycle_count();
    return time2 - time1;
    */
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
        flush_address(&shared_memory[SHD_SPECTRE_LAB_PAGE_SIZE * i]);
    }
}
