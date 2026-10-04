/* bthread-style same-program runtime: one master and pinned workers.
 * This is a minimal investigation fixture, not a production thread library.
 */
#include <stdatomic.h>
#include <stdint.h>

#ifndef BTHREAD_NUM_CORES
#define BTHREAD_NUM_CORES 2
#endif
_Static_assert(BTHREAD_NUM_CORES >= 1 && BTHREAD_NUM_CORES <= 4, "core count");
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "flags must be lock-free");

typedef void (*worker_func)(void *);
static worker_func functions[4];
static void *arguments[4];
static _Atomic unsigned flags[4];
static _Atomic unsigned entered[4];
/* Keep the startup handshake outside BSS: secondaries poll this while
 * core zero is clearing BSS. The ELF initializes it before any core runs.
 */
static _Atomic unsigned boot_ready __attribute__((section(".boot_sync"))) = 0;
static unsigned input[256];
extern unsigned char __stacks_end[];
extern void semihost(unsigned op, uintptr_t argument);

static inline unsigned core_id(void)
{
    unsigned id;
    __asm__ volatile("csrr %0, mhartid" : "=r"(id));
    return id;
}

static inline void pause_poll(void)
{
    __asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop; nop; nop" ::: "memory");
}

static int bthread_spawn(unsigned id, worker_func function, void *argument)
{
    if (BTHREAD_NUM_CORES == 1) {
        function(argument);
        return 0;
    }
    if (id == 0 || id >= BTHREAD_NUM_CORES || !function ||
        atomic_load_explicit(&flags[id], memory_order_acquire))
        return -1;
    arguments[id] = argument;
    functions[id] = function;
    atomic_store_explicit(&flags[id], 1, memory_order_release);
    return 0;
}

static void bthread_join(unsigned id)
{
    if (BTHREAD_NUM_CORES == 1)
        return;
    while (atomic_load_explicit(&flags[id], memory_order_acquire))
        pause_poll();
}

struct job {
    unsigned start, end, sum, hart;
    uintptr_t stack;
};
static struct job jobs[4];

static void sum_job(void *argument)
{
    struct job *job = argument;
    unsigned sum = 0;
    for (unsigned i = job->start; i < job->end; ++i)
        sum += input[i];
    job->sum = sum;
    job->hart = core_id();
    __asm__ volatile("mv %0, sp" : "=r"(job->stack));
}

/* A distinct target tests publishing a changed function pointer. */
static void reverse_sum_job(void *argument)
{
    struct job *job = argument;
    unsigned sum = 0;
    for (unsigned i = job->end; i > job->start; --i)
        sum += input[i - 1];
    job->sum = sum;
    job->hart = core_id();
    __asm__ volatile("mv %0, sp" : "=r"(job->stack));
}

static _Noreturn void finish(int success)
{
    char message[] = "BTHREAD_PASS cores=0 waves=8\n";
    if (success) {
        message[19] = '0' + BTHREAD_NUM_CORES;
        semihost(4, (uintptr_t)message);
    } else {
        semihost(4, (uintptr_t)"BTHREAD_FAIL\n");
    }
    semihost(0x18, 0x20026);
    for (;;) pause_poll();
}

void boot_main(unsigned id)
{
    if (id == 0)
        atomic_store_explicit(&boot_ready, 1, memory_order_release);
    else
        while (!atomic_load_explicit(&boot_ready, memory_order_acquire))
            pause_poll();
    atomic_store_explicit(&entered[id], 1, memory_order_release);
    if (id != 0) {
        for (;;) {
            while (!atomic_load_explicit(&flags[id], memory_order_acquire))
                pause_poll();
            functions[id](arguments[id]);
            atomic_store_explicit(&flags[id], 0, memory_order_release);
        }
    }
    for (unsigned core = 1; core < BTHREAD_NUM_CORES; ++core)
        while (!atomic_load_explicit(&entered[core], memory_order_acquire))
            pause_poll();
    for (unsigned wave = 0; wave < 8; ++wave) {
        for (unsigned i = 0; i < 256; ++i)
            input[i] = i + 1 + wave;
        for (unsigned core = 0; core < BTHREAD_NUM_CORES; ++core) {
            jobs[core].start = core * (256 / BTHREAD_NUM_CORES);
            jobs[core].end = (core + 1) * (256 / BTHREAD_NUM_CORES);
            jobs[core].sum = 0;
            jobs[core].hart = 99;
        }
        worker_func function = wave & 1 ? reverse_sum_job : sum_job;
        if (BTHREAD_NUM_CORES == 1) {
            if (bthread_spawn(0, function, &jobs[0])) finish(0);
            bthread_join(0);
        } else {
            for (unsigned core = 1; core < BTHREAD_NUM_CORES; ++core)
                if (bthread_spawn(core, function, &jobs[core])) finish(0);
            function(&jobs[0]);
            for (unsigned core = 1; core < BTHREAD_NUM_CORES; ++core)
                bthread_join(core);
        }
        unsigned sum = 0;
        for (unsigned core = 0; core < BTHREAD_NUM_CORES; ++core) {
            uintptr_t top = (uintptr_t)__stacks_end - core * 4096;
            if (jobs[core].hart != core || jobs[core].stack > top ||
                jobs[core].stack <= top - 4096)
                finish(0);
            sum += jobs[core].sum;
        }
        if (sum != 256 * 257 / 2 + 256 * wave) finish(0);
    }
    finish(1);
}
