/* Two M33s on the RP2350 board (make_board cores=2): core 1 starts from arcsim_core1_launch,
 * each core has its own SCS, and the cores hand work to each other over single-producer
 * single-consumer rings in SRAM, sleeping with WFE and waking each other with SEV. Prints
 * PINGPONG_OK or PINGPONG_FAIL, and how often each core's WFE returned.
 *   arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -mfloat-abi=soft -O2 -ffreestanding -nostdlib \
 *     -T arm_dual.ld arm_dual.c -o arm_dual.elf
 *   gem5.opt configs/example/rp2350/run.py --core arm-m4-proxy --firmware arm_dual.elf \
 *     --model accurate --cores 2 [--cpu atomic] */
#include <stdint.h>

#define SIO_CPUID (*(volatile uint32_t*)0xd0000000u)
#define SYST_RVR (*(volatile uint32_t*)0xe000e014u)
#define SCB_VTOR (*(volatile uint32_t*)0xe000ed08u)
#define N_PINGS 1000u
#define SPIN_CMD 0xffffffffu
#define SPIN_LOOPS 200000u
#define GO 0x60u

typedef struct {
  uint32_t wr, rd;
  uint32_t slot[16];
} ring_t;

static void push(ring_t* q, uint32_t v) {
  uint32_t w = __atomic_load_n(&q->wr, __ATOMIC_RELAXED);
  while (w - __atomic_load_n(&q->rd, __ATOMIC_ACQUIRE) >= 16u) __asm volatile("wfe");
  q->slot[w & 15u] = v;
  __atomic_store_n(&q->wr, w + 1u, __ATOMIC_RELEASE);
  __asm volatile("dsb\n sev" ::: "memory");
}

/* Pops one value, sleeping while the ring is empty; *wakes counts the WFEs that returned. */
static uint32_t pop(ring_t* q, uint32_t* wakes) {
  uint32_t r = __atomic_load_n(&q->rd, __ATOMIC_RELAXED);
  while (__atomic_load_n(&q->wr, __ATOMIC_ACQUIRE) == r) {
    __asm volatile("wfe" ::: "memory");
    ++*wakes;
  }
  uint32_t v = q->slot[r & 15u];
  __atomic_store_n(&q->rd, r + 1u, __ATOMIC_RELEASE);
  __asm volatile("dsb\n sev" ::: "memory");
  return v;
}

/* In SRAM no loader writes: zero at reset, so neither core needs the other to clear it. */
struct shared {
  uint32_t go;
  uint32_t c1_cpuid, c1_vtor, c1_rvr_before, c1_ready, c1_wakes;
  ring_t to1, from1;
};
__attribute__((section(".shared"))) static struct shared S;

static void semi(uint32_t op, const void* arg) {
  register uint32_t r0 __asm("r0") = op;
  register const void* r1 __asm("r1") = arg;
  __asm volatile("bkpt #0xab" : "+r"(r0) : "r"(r1) : "memory");
}
static void puts_(const char* s) { semi(4, s); }
static void put_u32(const char* name, uint32_t v) {
  char b[48];
  int i = 0;
  while (*name) b[i++] = *name++;
  b[i++] = '=';
  char d[11];
  int n = 0;
  do { d[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
  while (n) b[i++] = d[--n];
  b[i++] = '\n';
  b[i] = 0;
  puts_(b);
}
static void finish(int ok) {
  puts_(ok ? "PINGPONG_OK\n" : "PINGPONG_FAIL\n");
  semi(0x18, (const void*)(ok ? 0x20026u : 0x20023u));
  for (;;) {}
}

void core1_main(void) {
  while (__atomic_load_n(&S.go, __ATOMIC_ACQUIRE) != GO) __asm volatile("wfe" ::: "memory");
  S.c1_cpuid = SIO_CPUID;
  S.c1_vtor = SCB_VTOR;
  S.c1_rvr_before = SYST_RVR;
  SYST_RVR = 0x222u;
  __atomic_store_n(&S.c1_ready, 1u, __ATOMIC_RELEASE);
  __asm volatile("dsb\n sev" ::: "memory");
  for (;;) {
    uint32_t v = pop(&S.to1, &S.c1_wakes);
    if (v == SPIN_CMD) {
      for (volatile uint32_t k = 0; k < SPIN_LOOPS; ++k) {}
      push(&S.from1, SPIN_CMD);
    } else {
      push(&S.from1, 3u * v + 1u);
    }
  }
}

/* The second core's {stack pointer, entry} (gem5's secondary_launch). */
__attribute__((used)) const struct { uint32_t sp; void (*entry)(void); } arcsim_core1_launch = {0x20081000u, core1_main};

void reset0(void) {
  int ok = 1;
  if (SIO_CPUID != 0u) finish(0);
  SYST_RVR = 0x111u;
  __atomic_store_n(&S.go, GO, __ATOMIC_RELEASE);
  __asm volatile("dsb\n sev" ::: "memory");
  uint32_t w0 = 0;
  while (!__atomic_load_n(&S.c1_ready, __ATOMIC_ACQUIRE)) { __asm volatile("wfe" ::: "memory"); ++w0; }
  put_u32("c1_cpuid", S.c1_cpuid);
  put_u32("c1_vtor", S.c1_vtor);
  put_u32("c1_rvr_before", S.c1_rvr_before);
  put_u32("c0_rvr", SYST_RVR);
  ok &= S.c1_cpuid == 1u && S.c1_vtor == 0x10000000u && S.c1_rvr_before == 0u && SYST_RVR == 0x111u;
  uint32_t wakes = 0, bad = 0;
  for (uint32_t i = 1; i <= N_PINGS; ++i) {
    push(&S.to1, i);
    if (pop(&S.from1, &wakes) != 3u * i + 1u) ++bad;
  }
  put_u32("pings", N_PINGS);
  put_u32("bad", bad);
  put_u32("c0_wakes_pings", wakes);
  ok &= bad == 0u;
  /* Core 1 spins a while; core 0 sleeps in WFE meanwhile, so it should wake only a few times. */
  uint32_t spin_wakes = 0;
  push(&S.to1, SPIN_CMD);
  ok &= pop(&S.from1, &spin_wakes) == SPIN_CMD;
  put_u32("c0_wakes_spin", spin_wakes);
  put_u32("c1_wakes", S.c1_wakes);
  ok &= spin_wakes <= 4u;
  finish(ok);
}

static void fault(void) { finish(0); }
__attribute__((section(".vectors"), used)) const void* vectors[16] = {
    (void*)0x20082000u, (void*)reset0, (void*)fault, (void*)fault, (void*)fault, (void*)fault, (void*)fault,
};
