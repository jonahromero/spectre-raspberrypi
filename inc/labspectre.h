#ifndef SHD_SPECTRE_LAB_H
#define SHD_SPECTRE_LAB_H

// Joseph Ravichandran
// MIT Secure Hardware Design
// Created for Spring 2022, Updated for Spring 2023

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>

/********************************************
 * SHD Spectre Lab Userspace Helper Methods *
 ********************************************/

/*
 * time_access
 * Returns the time to access an address
 */
uint64_t time_access(void *addr);

/*
 * evict_address
 * evicts an address to point of coherency
*/
void evict_address(void* addr);

/*
 * init_shared_memory
 * Intializes a region of shared memory by writing to it,
 * and then flushing it from the cache. This should
 * minimize any TLB related issues.
 *
 * Arguments:
 *  - shared_memory: Pointer to the region of memory to initialize
 *  - len: Length of shared memory region
 *
 * Returns: None
 * Side Effects: Brings shared_memory into the cache, and then flushes it.
 */
void init_shared_memory(char *shared_memory, size_t len);

/*
 * run_attacker
 *
 * Arguments:
 *  - kernel_fd: A file descriptor referring to the lab vulnerable kernel module
 *  - shared_memory: A pointer to a region of memory shared with the server
 */
int run_attacker(int kernel_fd, char *shared_memory);

#endif // SHD_SPECTRE_LAB_H
