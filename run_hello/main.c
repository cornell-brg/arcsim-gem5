/*
 * main.c — Minimal "hello world" for gem5's Cortex-M4 (M-profile) model.
 *
 * Prints a greeting and a small computed value to gem5's stdout via ARM
 * semihosting (SYS_WRITE0), then exits the simulation cleanly via
 * semihosting SYS_EXIT.  No libc syscall retargeting is used — the
 * semihosting calls are hand-rolled inline asm, the same pattern used
 * throughout gem5/tests/gem5/m_profile_tests/programs/ and
 * stm32-board-microbenchmarks/microbenchmark/src/main.c.
 */
#include <stdint.h>

/* ---- ARM semihosting (BKPT #0xab) --------------------------------- */

#define SYS_WRITE0 0x04

static void
semi_write0(const char *s)
{
    register uint32_t r0 __asm__("r0") = SYS_WRITE0;
    register const char *r1 __asm__("r1") = s;
    __asm__ volatile("bkpt #0xab" : "+r"(r0) : "r"(r1) : "memory");
}

/*
 * Semihosting exit — SYS_EXIT (0x18) with
 * ADP_Stopped_ApplicationExit (0x20026).  Same pattern as
 * gem5/tests/gem5/m_profile_tests/programs/freertos/main_freertos_test.c's
 * semihosting_exit().
 */
static void
semi_exit(void)
{
    __asm__ volatile(
        "mov r0, #0x18\n"
        "ldr r1, =0x20026\n"
        "bkpt #0xab\n"
        ::: "r0", "r1"
    );
}

/* ---- tiny decimal printer (no libc) -------------------------------- */

static void
print_u32(uint32_t v)
{
    char buf[11];   /* max "4294967295" + NUL */
    char *p = buf + sizeof(buf) - 1;
    *p = '\0';
    if (v == 0) {
        *--p = '0';
    } else {
        while (v > 0) {
            *--p = '0' + (v % 10);
            v /= 10;
        }
    }
    semi_write0(p);
}

/* ---- program body ---------------------------------------------------- */

static volatile uint32_t result;   /* volatile so the compiler can't optimize the sum away */

int
main(void)
{
    semi_write0("Hello from Cortex-M4 on gem5!\n");

    uint32_t sum = 0;
    for (uint32_t i = 1; i <= 10; i++)
        sum += i;
    result = sum;

    semi_write0("Sum of 1..10 = ");
    print_u32(result);
    semi_write0("\n");

    semi_write0("Exiting.\n");
    semi_exit();

    for (;;)
        ;   /* not reached */
}
