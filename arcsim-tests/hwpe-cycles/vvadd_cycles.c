// Cycles of one vvadd job of 100 elements (test_vvadd's shape): mcycle from just before the
// TRIGGER write to the STATUS read that returns 0; the sums checked.
#include <arcsim/test/main.h>
#include "soc_common.h"
#include "tb_cxxrtl_io.h"
#include <stdint.h>

#define HWPE_REG_TRIGGER 0x00
#define HWPE_REG_STATUS  0x0C
#define HWPE_REG_A_PTR   0x40
#define HWPE_REG_B_PTR   0x44
#define HWPE_REG_C_PTR   0x48
#define HWPE_REG_LEN     0x4C
#define LEN 100

static __attribute__((section(".tcdm.bss"), aligned(32))) int a[LEN];
static __attribute__((section(".tcdm.bss"), aligned(32))) int b[LEN];
static __attribute__((section(".tcdm.bss"), aligned(32))) volatile int c[LEN];

static inline uint32_t mcycle(void) { uint32_t x; asm volatile("csrr %0, mcycle" : "=r"(x)); return x; }
static void put_u(uint32_t v) { char s[12]; int i = 11; s[i] = 0; do { s[--i] = '0' + v % 10; v /= 10; } while (v); tb_puts(&s[i]); }

static void test_case_1_cycles(void) {
  for (int i = 0; i < LEN; i++) { a[i] = i * 7 + 1; b[i] = 1000 - i * 3; c[i] = 0; }
  uint32_t cyc[3];
  for (int r = 0; r < 3; r++) {
    write_mem(HWPE_PERIPH_BASE + HWPE_REG_A_PTR, (uint32_t)a);
    write_mem(HWPE_PERIPH_BASE + HWPE_REG_B_PTR, (uint32_t)b);
    write_mem(HWPE_PERIPH_BASE + HWPE_REG_C_PTR, (uint32_t)c);
    write_mem(HWPE_PERIPH_BASE + HWPE_REG_LEN, LEN);
    uint32_t t0 = mcycle();
    write_mem(HWPE_PERIPH_BASE + HWPE_REG_TRIGGER, 0);
    int done = 0;
    for (int i = 0; i < 100000 && !done; i++) done = read_mem(HWPE_PERIPH_BASE + HWPE_REG_STATUS) == 0;
    cyc[r] = mcycle() - t0;
    ARCSIM_ASSERT_TRUE(done);
    for (int i = 0; i < LEN; i++) ARCSIM_EXPECT_EQ(c[i], a[i] + b[i]);
  }
  tb_puts("CYCLES");
  for (int r = 0; r < 3; r++) { tb_puts(" "); put_u(cyc[r]); }
  tb_puts("\n");
}

ARCSIM_TESTS(
  ARCSIM_TEST(test_case_1_cycles)
)
