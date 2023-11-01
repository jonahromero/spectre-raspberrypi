/*
 * labspectrekm.c
 * Kernel module for the Spectre lab in the Secure Hardware Design course at MIT
 * Created by Joseph Ravichandran for Spring 2022
 * Updated for Spring 2023
 */
#include "labspectrekm.h"
#include "labspectreipc.h"
#include <linux/kthread.h>
#include <linux/sched.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joseph Ravichandran <jravi@csail.mit.edu>");
MODULE_DESCRIPTION("Spectre lab target module for Secure Hardware Design at MIT");

// The secrets you're trying to leak!
static volatile char __attribute__((aligned(32768))) kernel_secret3[SHD_SPECTRE_LAB_SECRET_MAX_LEN] = "MIT{h4rd3st}";
static volatile char __attribute__((aligned(32768))) kernel_secret2[SHD_SPECTRE_LAB_SECRET_MAX_LEN] = "MIT{scary_sp3ctr3!}";
static volatile char __attribute__((aligned(32768))) kernel_secret1[SHD_SPECTRE_LAB_SECRET_MAX_LEN] = "MIT{k3rn3l_m3m0r135}";

// The bounds checks that you need to use speculative execution to bypass
static volatile size_t __attribute__((aligned(32768))) secret_leak_limit_part2 = 4;
static volatile size_t __attribute__((aligned(32768))) secret_leak_limit_part3 = 4;

static struct proc_dir_entry *spectre_lab_procfs_victim = NULL;
static const struct proc_ops spectre_lab_victim_ops = {
    .proc_write = spectre_lab_victim_write,
    .proc_read = spectre_lab_victim_read,
};


typedef struct {
    size_t sets, associativity, line_size;
} CacheSize;


void flush(void* addr)
{
    asm volatile("dc civac, %0"::"r"(addr));
}

void enable_pm(void)
{
    uint64_t control_register;
    uint64_t count_enable;
    uint64_t user_enable;

    // PM Control Register
    asm volatile("mrs %0, PMCR_EL0":"=r"(control_register));
    // bit 0: Affected counters are enabled by PMCNTENSET_EL0
    control_register |= 0b1;
    asm volatile("msr PMCR_EL0, %0"::"r"(control_register));

    // PM Count Enable Set Register
    asm volatile("mrs %0, PMCNTENSET_EL0":"=r"(count_enable));
    // bit 31: Reads to pmccntr_el0 are enabled
    count_enable |= 0x80000000;
    asm volatile("msr PMCNTENSET_EL0, %0"::"r"(count_enable));

    // PM User Enable Read Register
    asm volatile("mrs %0, PMUSERENR_EL0":"=r"(user_enable));
    // bit 0: Enables EL0 read/write access to PMU registers.
    // bit 2: Cycle counter Read enable.
    printk("Before Setting PMUSERENR_EL0:%#08x\n", user_enable);
    user_enable |= 0b0101;
    asm volatile("msr PMUSERENR_EL0, %0"::"r"(user_enable));
    asm volatile("mrs %0, PMUSERENR_EL0":"=r"(user_enable));
    printk("After Setting PMUSERENR_EL0:%#08x\n", user_enable);
}

int enable_pm_thread_wrapper(void*) { enable_pm(); return 0; }

void enable_pm_on_core(int core)
{
    struct task_struct* task = kthread_create_on_cpu(enable_pm_thread_wrapper, &core, core, "enable_pm_thread");
    wake_up_process(task);
}

static int select_cache(size_t cache_level, int is_data_cache)
{
    uint64_t cache_selection = 0;
    if (cache_level > 7) return 1;
    cache_selection |= ((uint64_t)cache_level << 1) | (uint64_t)(!is_data_cache);
    asm volatile("msr CSSELR_EL1, %0"::"r"(cache_selection));
    return 0;
}

CacheSize get_cache_info(size_t cache_level)
{
    uint64_t cache_size;
    if (select_cache(cache_level, 1) != 0)
    {
        return (CacheSize){0,0,0};
    }
    asm volatile("mrs %0, CCSIDR_EL1":"=r"(cache_size));
    // bits 0-2: line size
    // bits 12-3: associativity
    // bits 27-13: number of sets
    return (CacheSize) {
        .line_size     = (1 << (((cache_size >> 0) & ((uint64_t)(0x1 << 3) - 1)) + 2)) * 4,
        .associativity =      ((cache_size >> 3) & ((uint64_t)(0x1 << 10) - 1)) + 1,
        .sets          =      ((cache_size >> 13) & ((uint64_t)(0x1 << 15) - 1)) + 1,
    };
}

void print_cache_info(void)
{
    uint32_t cache_level_id, cache_id;
    asm volatile("mrs %0, CLIDR_EL1" : "=r"(cache_level_id));
    for (int i = 0; i < 7; i++)
    {
        cache_id = cache_level_id & 0x7;
        printk(SHD_PRINT_INFO "cache #%d: ", i);
        switch (cache_id)
        {
        case 0:
            printk("No cache"); break;
        case 1:
            printk("Instruction cache only"); break;
        case 2:
            printk("Data cache only"); break;
        case 3:
            printk("Seperate instruction and data caches"); break;
        case 4:
             printk("Unified cache"); break;
        default:
            printk("Reserved/Unknown"); break;
        }
        if (cache_id >= 2 && cache_id <= 4)
        {
            CacheSize size = get_cache_info(i);
            printk("Line Size: %ld\nSets: %lu\nAssociativity:%ld\nSize:%lu bytes",
                size.line_size, size.sets, size.associativity, size.line_size * size.sets * size.associativity
            );
        }
        printk("\n");
        cache_level_id >>= 3;
    }

    printk(SHD_PRINT_INFO "Level of Unification Inner Shareable: %d", cache_level_id & 0x7);
    cache_level_id >>= 3;
    printk(SHD_PRINT_INFO "Level of Coherence: %d", cache_level_id & 0x7);
    cache_level_id >>= 3;
    printk(SHD_PRINT_INFO "Level of Unification Uniprocessor: %d\n", cache_level_id & 0x7);
}

/*
 * print_cmd
 * Prints a nicely formatted command to stdout
 *
 * Arguments:
 *  - cmd: A pointer to a spectre_lab_command object to the kernel log
 *
 * Returns: None
 * Side Effects: Prints to kernel log
 */
void print_cmd(spectre_lab_command *cmd) {
    if (NULL == cmd) return;

    printk(SHD_PRINT_INFO "Command at %p\n\tKind: %d\n\tArg 1: 0x%llX\n",
            cmd,
            cmd->kind,
            cmd->arg1);
}

/*
 * spectre_lab_init
 * Installs the procfs handlers for communicating with this module.
 */
int spectre_lab_init(void) {
    printk(SHD_PRINT_INFO "SHD Spectre KM Loaded\n");
    for (int i = 0; i < num_possible_cpus(); i++) {
        enable_pm_on_core(i);
    }
    print_cache_info();
    spectre_lab_procfs_victim = proc_create(SHD_PROCFS_NAME, 0, NULL, &spectre_lab_victim_ops);
    return 0;
}

/*
 * spectre_lab_fini
 * Cleans up the procfs handlers and cleanly removes the module from the kernel.
 */
void spectre_lab_fini(void) {
    printk(SHD_PRINT_INFO "SHD Spectre KM Unloaded\n");
    proc_remove(spectre_lab_procfs_victim);
}

/*
 * spectre_lab_victim_read
 * procfs read handler that does nothing so that reading from /proc/SHD_PROCFS_NAME
 * can actually return.
 */
ssize_t spectre_lab_victim_read(struct file *file_in, char __user *userbuf, size_t num_bytes, loff_t *offset) {
    // Reading does nothing
    return 0;
}

/*
 * spectre_lab_victim_write
 * procfs write handler for interacting with the module
 * Writes expect the user to write a spectre_lab_command struct to the module.
 *
 * Input: A spectre_lab_command struct for us to parse.
 * Output: Number of bytes accepted by the module.
 * Side Effects: Will trigger a spectre bug based on the user_cmd.kind
 */
ssize_t spectre_lab_victim_write(struct file *file_in, const char __user *userbuf, size_t num_bytes, loff_t *offset)
{
    spectre_lab_command user_cmd;
    struct page *pages[SHD_SPECTRE_LAB_SHARED_MEMORY_NUM_PAGES];
    char *kernel_mapped_region[SHD_SPECTRE_LAB_SHARED_MEMORY_NUM_PAGES];
    int retval;
    int i, j, z;
    char secret_data;
    volatile char tmp;
    size_t long_latency;
    char *addr_to_leak;

    for (i = 0; i < SHD_SPECTRE_LAB_SHARED_MEMORY_NUM_PAGES; i++) {
        pages[i] = NULL;
    }

    if (copy_from_user(&user_cmd, userbuf, sizeof(user_cmd)) == 0) {
        // arg1 is always a pointer to the shared memory region
        if (!access_ok(user_cmd.arg1, SHD_SPECTRE_LAB_SHARED_MEMORY_SIZE)) {
            printk(SHD_PRINT_INFO "Invalid user request- shared memory is 0x%llX\n", user_cmd.arg1);
            return num_bytes;
        }

        // arg2 is always the secret index to use
        if (!(user_cmd.arg2 < SHD_SPECTRE_LAB_SECRET_MAX_LEN)) {
            printk(SHD_PRINT_INFO "Tried to access a secret that is too large! Requested offset %llu\n", user_cmd.arg2);
            return num_bytes;
        }

        // Pin the pages to RAM so they aren't swapped to disk
        retval = get_user_pages_fast(user_cmd.arg1, SHD_SPECTRE_LAB_SHARED_MEMORY_NUM_PAGES, FOLL_WRITE, pages);
        if (SHD_SPECTRE_LAB_SHARED_MEMORY_NUM_PAGES != retval) {
            printk(SHD_PRINT_INFO "Unable to pin the user pages! Requested %d pages, got %d\n", SHD_SPECTRE_LAB_SHARED_MEMORY_NUM_PAGES, retval);

            // If the return value is negative, its an error, so don't try to unpin!
            if (retval > 0) {
                // Unpin the pages that got pinned before exiting
                for (i = 0; i < retval; i++) {
                    put_page(pages[i]);
                }
            }

            return num_bytes;
        }

        // Map the new pages (aliases to the userspace pages) into the kernel address space
        // Accessing these pages will incur a TLB miss as they were just remapped
        for (i = 0; i < SHD_SPECTRE_LAB_SHARED_MEMORY_NUM_PAGES; i++) {
            kernel_mapped_region[i] = (char *)kmap(pages[i]);

            if (NULL == kernel_mapped_region[i]) {
                printk(SHD_PRINT_INFO "Unable to map page %d\n", i);

                // Unmap everything in reverse order and return early
                for (j = i - 1; j >= 0; j--) {
                    kunmap(pages[j]);
                }

                return num_bytes;
            }
        }

        // Process this command packet
        switch (user_cmd.kind) {
            // Part 1 is Flush+Reload, so access a secret without a bounds check
            case COMMAND_PART1:
                secret_data = kernel_secret1[user_cmd.arg2];
                if (secret_data < SHD_SPECTRE_LAB_SHARED_MEMORY_NUM_PAGES) {
                    tmp = *kernel_mapped_region[secret_data];
                }
            break;

            // Part 2 is Spectre, so access a secret bounded by a bounds check
            case COMMAND_PART2:
                // Load the secret:
                secret_data = kernel_secret2[user_cmd.arg2];

                // Trigger a page walk:
                addr_to_leak = kernel_mapped_region[secret_data];

                // Flush the limit variable to make this if statement take a long time to resolve
                flush(&secret_leak_limit_part2);
                if (user_cmd.arg2 < secret_leak_limit_part2) {
                    // Perform the speculative leak
                    tmp = *addr_to_leak;
                }
            break;

            // Part 3 is a more difficult version of Spectre
            case COMMAND_PART3:
                // No cache flush this time around!
                for (z = 0; z < 1000; z++);
                if (user_cmd.arg2 < secret_leak_limit_part3) {
                    long_latency = user_cmd.arg2 * 1ULL * 1ULL * 1ULL * 1ULL * 0ULL;
                    tmp = *kernel_mapped_region[kernel_secret3[user_cmd.arg2] + long_latency];
                }
            break;
        }

        // Unmap in reverse order- needs to be reverse order!
        for (i = SHD_SPECTRE_LAB_SHARED_MEMORY_NUM_PAGES - 1; i >= 0; i--) {
            kunmap(pages[i]);
        }

        // Unpin to ensure refcounts are valid
        for (i = 0; i < SHD_SPECTRE_LAB_SHARED_MEMORY_NUM_PAGES; i++) {
            put_page(pages[i]);
        }

        // Success!
        return num_bytes;
    }
    else {
        // Error
        return 0;
    }
}

module_init(spectre_lab_init);
module_exit(spectre_lab_fini);
