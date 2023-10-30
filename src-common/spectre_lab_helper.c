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
 * time_access
 * Returns the time to access an address
 */
uint64_t time_access(void* addr)
{
    char temp;
    uint64_t start, end;
    asm volatile(
        "dsb sy"                "\n\t"
        "isb"                   "\n\t"
        "mrs %0, pmccntr_el0"   "\n\t"
        "isb"                   "\n\t"
        "ldr %2, [%3]"          "\n\t"
        "isb"                   "\n\t"
        "mrs %1, pmccntr_el0"   "\n\t"
        "isb"                   "\n\t"
        "dsb sy"                "\n\t"
        :"=r"(start), "=r"(end),
         "=r"(temp)
        :"r"(addr)
    );
    return end - start;
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
        //flush_address(&shared_memory[SHD_SPECTRE_LAB_PAGE_SIZE * i]);
    }
}
