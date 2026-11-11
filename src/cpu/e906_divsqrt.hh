/*
 * The cycles of a single-precision divide or square root on T-Head's E906
 * divide and square root unit (pa_fdsu, the FPnew TH32 unit in arcsim-soc's
 * Hazard3 FPU), from its operands. Its SRT iteration runs at most 14 turns
 * and stops once the partial remainder is zero (pa_fdsu_ctrl.v:
 * srt_last_round), so a result exact in few bits finishes sooner; a zero,
 * infinite or NaN operand (or the root of a negative number) skips the
 * iteration. On arcsim-soc's RTL (soc 9d3bed4, arcsim-bench's micro/fp_ops
 * calibration bodies) an operation takes 5 cycles plus floor(n / 2) + 1
 * turns, at most 14, where n is the significant bits of the exact result
 * (1.0 / 1.0: 6; 255.0 / 1.0: 10; pi / e: 19), and 3 when it skips.
 */

#ifndef __CPU_E906_DIVSQRT_HH__
#define __CPU_E906_DIVSQRT_HH__

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gem5
{

inline unsigned
e906DivSqrtCycles(bool sqrt, uint32_t a, uint32_t b)
{
    const auto skips = [](uint32_t x) {
        return ((x >> 23) & 0xff) == 0xff || (x & 0x7fffffff) == 0;
    };
    if (skips(a) || (!sqrt && skips(b)) || (sqrt && (a >> 31)))
        return 3;

    // The significand as an integer, with its hidden bit when normal
    const auto significand = [](uint32_t x) -> uint64_t {
        const uint32_t m = x & 0x7fffff;
        return ((x >> 23) & 0xff) ? (m | 0x800000) : m;
    };

    // The exact result's significand as an integer, 0 when not exact
    uint64_t exact = 0;
    if (!sqrt) {
        // a / b is exact when b's odd part divides a's significand
        const uint64_t a_sig = significand(a);
        uint64_t b_sig = significand(b);
        b_sig >>= __builtin_ctzll(b_sig);
        exact = a_sig % b_sig ? 0 : a_sig / b_sig;
    } else {
        // a = sig * 2^p; with p made even, the root is exact when sig is
        // a perfect square
        const int e = (a >> 23) & 0xff;
        uint64_t sig = significand(a);
        if (((e ? e : 1) - 150) & 1)
            sig <<= 1;
        const uint64_t root = std::llround(std::sqrt(double(sig)));
        exact = root * root == sig ? root : 0;
    }

    unsigned turns = 14;
    if (exact) {
        exact >>= __builtin_ctzll(exact);
        const unsigned bits = 64 - __builtin_clzll(exact);
        turns = std::min(14u, bits / 2 + 1);
    }
    return 5 + turns;
}

} // namespace gem5

#endif // __CPU_E906_DIVSQRT_HH__
