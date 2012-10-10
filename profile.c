#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>

#include "profile.h"

#define unlikely(x)     __builtin_expect((x), 0)
#define likely(x)       __builtin_expect((x), 1)

#define PROFILE_TABLE_ORDER     12
#define PROFILE_TABLE_SIZE      (1 << PROFILE_TABLE_ORDER)
#define PROFILE_TABLE_MASK      (PROFILE_TABLE_SIZE - 1)
#define PROFILE_TABLE_HASH(f)   ((uint64_t)((uintptr_t)f & PROFILE_TABLE_MASK))

#define PROFILE_STACK_SIZE      32


/* Contains all data that is kept for a single function */
struct profile_node {
    void *f;
    struct profile_data pdata;
    union {
        uint64_t flags;
        struct {
            uint64_t in_profiling : 1;
        };
    };
};

/* Contains all data that is kept for a currently executing function */
struct profile_node_run {
    struct profile_node *pnode;
    uint64_t start_cycles;
};


static struct profile_node *profile_table[PROFILE_TABLE_SIZE];
static struct profile_node_run profile_stack[PROFILE_STACK_SIZE];
static int profile_stack_depth;
static int profiling_enabled;
static cpu_set_t old_affinity_cpu_set;


static uint64_t profile_get_ticks(void)
{
     unsigned int __a, __d;
     asm volatile("rdtsc" : "=a" (__a), "=d" (__d));
     return ((unsigned long)__a) | (((unsigned long)__d)<<32);
}

static void profile_func_enter(struct profile_node *pnode)
{
    struct profile_node_run *prun = &profile_stack[profile_stack_depth];

    profile_stack_depth += 1;

    prun->pnode = pnode;
    prun->pnode->in_profiling = 1;
    prun->start_cycles = profile_get_ticks();
}

static void profile_func_exit(struct profile_node_run *prun)
{
    uint64_t end_cycles = profile_get_ticks();

    prun->pnode->pdata.cycles += end_cycles - prun->start_cycles;
    prun->pnode->pdata.hits += 1;
    prun->pnode->in_profiling = 0;

    profile_stack_depth -= 1;
}

void __cyg_profile_func_enter(void *this_fn, void *call_site)
{
    if (likely(!profiling_enabled)) {
        return;
    }

    uint64_t prof_tkey = PROFILE_TABLE_HASH(this_fn);
    struct profile_node *pnode = profile_table[prof_tkey];

    if (unlikely(pnode != NULL)) {
        /* No recursion allowed */
        if (pnode->in_profiling == 0) {
            profile_func_enter(pnode);
        }
    }
}

void __cyg_profile_func_exit(void *this_fn, void *call_site)
{
    if (likely(!profiling_enabled)) {
        return;
    }

    if (profile_stack_depth) {
        struct profile_node_run *prun = &profile_stack[profile_stack_depth-1];

        if (unlikely(prun->pnode->f == this_fn)) {
            profile_func_exit(prun);
        }
    }
}


int libprofile_enable(void)
{
    cpu_set_t cpumask;

    CPU_ZERO(&cpumask);
    CPU_SET(0, &cpumask);

    sched_getaffinity(getpid(), sizeof(cpu_set_t), &old_affinity_cpu_set);
    sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpumask);

    profiling_enabled = 1;
    return 0;
}

int libprofile_disable(void)
{
    sched_setaffinity(getpid(), sizeof(cpu_set_t), &old_affinity_cpu_set);
    profiling_enabled = 0;
    return 0;
}

int libprofile_register(void *f)
{
    uint64_t prof_tkey = PROFILE_TABLE_HASH(f);
    if (profile_table[prof_tkey]) {
        /* TODO: Handle collisions */
        return -1;
    }

    /* TODO: Should use a pool instead of malloc */
    struct profile_node *pnode = malloc(sizeof(struct profile_node));
    memset(pnode, 0, sizeof(struct profile_node));
    pnode->f = f;
    profile_table[prof_tkey] = pnode;

    return 0;
}

int libprofile_unregister(void *f)
{
    uint64_t prof_tkey = PROFILE_TABLE_HASH(f);
    struct profile_node *pnode = profile_table[prof_tkey];

    if (pnode == NULL || pnode->in_profiling) {
        return -1;
    }

    free(pnode);
    return 0;
}

int libprofile_get_profiling(void *f, struct profile_data *pdata)
{
    uint64_t prof_tkey = PROFILE_TABLE_HASH(f);
    struct profile_node *pnode = profile_table[prof_tkey];

    if (!pnode) {
        return -1;
    }

    memcpy(pdata, &pnode->pdata, sizeof(struct profile_data));
    return 0;
}
