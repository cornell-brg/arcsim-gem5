// Two jobs of fast_cycles.c (cell 0, then the flat cell), short enough to trace;
// mcycle from just before the TRIGGER write to the STATUS read that returns 0.
#include <arcsim/test/main.h>
#include "soc_common.h"
#include "tb_cxxrtl_io.h"
#include <stdint.h>

#define FAST_FRAME_ATTR __attribute__((section(".tcdm.data"), aligned(32)))
#include "fast_cell_vectors.h"

#define HWPE_REG_TRIGGER     0x00
#define HWPE_REG_STATUS      0x0C
#define HWPE_REG_RESULT      0x18
#define HWPE_REG_IMAGE_PTR   0x40
#define HWPE_REG_CORNERS_PTR 0x44
#define CORNER_WORDS 8
#define GUARD_WORDS  4
#define GUARD        0xdeadbeefu

static __attribute__((section(".tcdm.bss"), aligned(32))) volatile uint32_t corners[CORNER_WORDS + GUARD_WORDS];
static __attribute__((section(".tcdm.bss"), aligned(32))) uint8_t flat_rows[FAST_CELL * FAST_FRAME_WIDTH];

static inline uint32_t mcycle(void) { uint32_t c; asm volatile("csrr %0, mcycle" : "=r"(c)); return c; }

static uint32_t cyc[16];
static int njobs;

static void put_u(uint32_t v) { char b[12]; int i = 11; b[i] = 0; do { b[--i] = '0' + v % 10; v /= 10; } while (v); tb_puts(&b[i]); }

static void run(const uint8_t* cell, const fast_cell_t* c, int concurrent) {
  for (int i = 0; i < CORNER_WORDS + GUARD_WORDS; i++) corners[i] = GUARD;
  write_mem(HWPE_PERIPH_BASE + HWPE_REG_IMAGE_PTR, (uint32_t)cell);
  write_mem(HWPE_PERIPH_BASE + HWPE_REG_CORNERS_PTR, (uint32_t)corners);
  uint32_t sum = 0;
  uint32_t t0 = mcycle();
  write_mem(HWPE_PERIPH_BASE + HWPE_REG_TRIGGER, 0);
  int done = 0;
  for (int i = 0; i < 1000000 && !done; i++) {
    if (concurrent) sum += fast_frame_rows[(uint32_t)i % sizeof(fast_frame_rows)];
    done = read_mem(HWPE_PERIPH_BASE + HWPE_REG_STATUS) == 0;
  }
  uint32_t t1 = mcycle();
  (void)sum;
  ARCSIM_ASSERT_TRUE(done);
  cyc[njobs++] = t1 - t0;
  ARCSIM_EXPECT_EQ(read_mem(HWPE_PERIPH_BASE + HWPE_REG_RESULT), c->count);
  for (uint32_t i = 0; i < c->n; i++) ARCSIM_EXPECT_EQ(corners[i], c->best[i]);
  for (uint32_t i = c->n; i < CORNER_WORDS + GUARD_WORDS; i++) ARCSIM_EXPECT_EQ(corners[i], GUARD);
}

static void test_case_1_cycles(void) {
  run(fast_frame_rows + fast_cells[0].col0, &fast_cells[0], 0);
  for (uint32_t i = 0; i < sizeof(flat_rows); i++) flat_rows[i] = 128;
  const fast_cell_t none = {0, 0, 0, {0}};
  run(flat_rows, &none, 0);
  tb_puts("CYCLES");
  for (int i = 0; i < njobs; i++) { tb_puts(" "); put_u(cyc[i]); }
  tb_puts("\n");
}

ARCSIM_TESTS(
  ARCSIM_TEST(test_case_1_cycles)
)
