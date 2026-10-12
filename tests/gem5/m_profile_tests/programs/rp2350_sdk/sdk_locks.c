/*
 * Test: the Pico SDK's locks on the RP2350
 *
 * On the RP2350 the SDK builds its spin locks from LDAEXB, STREXB and STLB
 * (PICO_USE_SW_SPIN_LOCKS), and every mutex, critical section, malloc and
 * printf takes one. This runs each of them to completion.
 *
 * Output goes through semihosting: RP2350_SDK_LOCKS_OK, or
 * RP2350_SDK_LOCKS_FAIL and the number of the step that failed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/sync.h"
#include "pico/critical_section.h"
#include "pico/mutex.h"
#include "pico/stdio_semihosting.h"

static int
semihosting(int op, void *args)
{
    register int r0 __asm("r0") = op;
    register void *r1 __asm("r1") = args;
    __asm volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

/* The SDK writes to handle 1 without opening it. gem5 has no handle open
 * at start-up and gives the first SYS_OPEN handle 1, so open the console. */
static void
open_console(void)
{
    static const char name[] = ":tt";
    volatile struct { const char *name; size_t mode; size_t length; } args =
        { name, 4, sizeof(name) - 1 };
    semihosting(0x01, (void *)&args);
}

static void
finish(int failed_step)
{
    if (failed_step)
        printf("RP2350_SDK_LOCKS_FAIL %d\n", failed_step);
    else
        printf("RP2350_SDK_LOCKS_OK\n");
    semihosting(0x18, (void *)0x20026);
    for (;;)
        ;
}

#define CHECK(step, condition) do { if (!(condition)) finish(step); } while (0)

enum { BLOCKS = 16 };

int
main(void)
{
    open_console();
    stdio_semihosting_init();

    /* 1: a mutex */
    mutex_t mutex;
    mutex_init(&mutex);
    for (int i = 0; i < 4; ++i) {
        mutex_enter_blocking(&mutex);
        CHECK(1, !mutex_try_enter(&mutex, NULL));
        mutex_exit(&mutex);
    }
    CHECK(1, mutex_try_enter(&mutex, NULL));
    mutex_exit(&mutex);

    /* 2: a recursive mutex, entered twice by its owner */
    recursive_mutex_t recursive;
    recursive_mutex_init(&recursive);
    recursive_mutex_enter_blocking(&recursive);
    CHECK(2, recursive_mutex_try_enter(&recursive, NULL));
    recursive_mutex_exit(&recursive);
    recursive_mutex_exit(&recursive);

    /* 3: a critical section, which holds a spin lock with interrupts off */
    critical_section_t section;
    critical_section_init(&section);
    critical_section_enter_blocking(&section);
    CHECK(3, is_spin_locked(section.spin_lock));
    critical_section_exit(&section);
    CHECK(3, !is_spin_locked(section.spin_lock));
    critical_section_deinit(&section);

    /* 4: a spin lock on its own */
    spin_lock_t *lock = spin_lock_init(spin_lock_claim_unused(true));
    uint32_t saved = spin_lock_blocking(lock);
    CHECK(4, is_spin_locked(lock));
    spin_unlock(lock, saved);
    CHECK(4, !is_spin_locked(lock));

    /* 5: malloc, calloc, realloc and free, each behind the SDK's mutex */
    unsigned char *block[BLOCKS];
    for (int i = 0; i < BLOCKS; ++i) {
        size_t size = 8u << (i % 8);
        block[i] = malloc(size);
        CHECK(5, block[i] != NULL);
        memset(block[i], i + 1, size);
    }
    for (int i = 0; i < BLOCKS; ++i) {
        size_t size = 8u << (i % 8);
        for (size_t j = 0; j < size; ++j)
            CHECK(5, block[i][j] == i + 1);
    }
    block[0] = realloc(block[0], 4096);
    CHECK(5, block[0] != NULL && block[0][0] == 1 && block[0][7] == 1);
    for (int i = 0; i < BLOCKS; ++i)
        free(block[i]);
    unsigned *zeroed = calloc(64, sizeof(unsigned));
    CHECK(5, zeroed != NULL);
    for (int i = 0; i < 64; ++i)
        CHECK(5, zeroed[i] == 0);
    free(zeroed);

    /* 6: printf, behind the stdio mutex */
    printf("RP2350_SDK_LOCKS mutex=%d blocks=%d\n", 4, BLOCKS);
    finish(0);
}
