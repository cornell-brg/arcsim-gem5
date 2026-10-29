#include "dev/hwpe/of_reference.hh"

#include "arcsim/kernels/klt/lk.h"

namespace gem5
{
namespace of_reference
{

namespace klt = arcsim::kernels::klt;
namespace kd = arcsim::kernels::klt::detail;

struct LkTemplate::State
{
    klt::LkScratch scratch;
    kd::GradientQ g;
};

namespace
{

klt::Config
config()
{
    klt::Config c;
    c.win = lkWin;
    return c;
}

kd::FixedLimits
limits(int64_t min_eig, int64_t eps2, int b_shift)
{
    kd::FixedLimits f = kd::fixed_limits(config());
    f.min_eig = min_eig;
    f.eps2 = eps2;
    f.b_shift = b_shift;
    return f;
}

} // anonymous namespace

LkTemplate::LkTemplate() : state(new State) {}

LkTemplate::~LkTemplate() = default;

bool
LkTemplate::make(const uint8_t *block, int32_t px, int32_t py, int width,
                 int height, int64_t min_eig, int b_shift)
{
    constexpr int32_t frac = kd::kQOne - 1;
    int64_t a11, a12, a22;
    kd::template_block(config(), {block, lkBlock}, px >> kd::kQBits,
                       py >> kd::kQBits, width, height,
                       kd::weights_q(px & frac, py & frac), state->scratch,
                       a11, a12, a22);
    kd::SolveQ q;
    if (!kd::solve_fixed(min_eig, b_shift, a11, a12, a22, q))
        return false;
    state->g = q.g;
    return true;
}

bool
LkTemplate::makeMapped(const uint8_t *block, int32_t pcx, int32_t pcy,
                       const int32_t m[4], int width, int height,
                       int64_t min_eig, int b_shift)
{
    const klt::Config c = config();
    klt::TemplateMap map;
    map.m11 = m[0];
    map.m12 = m[1];
    map.m21 = m[2];
    map.m22 = m[3];
    const int h = (c.win - 1) / 2;
    const int bx = (pcx >> kd::kQBits) - lkRotReach - 1;
    const int by = (pcy >> kd::kQBits) - lkRotReach - 1;
    const int32_t x0 = pcx - h * (map.m11 + map.m12);
    const int32_t y0 = pcy - h * (map.m21 + map.m22);
    const auto at = [&](int i, int j, int &fx, int &fy, kd::Weights &w) {
        const int32_t X = x0 + i * map.m11 + j * map.m12;
        const int32_t Y = y0 + i * map.m21 + j * map.m22;
        fx = X >> kd::kQBits;
        fy = Y >> kd::kQBits;
        w = kd::weights_q(X & (kd::kQOne - 1), Y & (kd::kQOne - 1));
    };
    int64_t a11, a12, a22;
    kd::template_block_mapped(c, {block, lkRotSide}, bx, by, lkRotSide,
                              width, height, map, at, state->scratch, a11,
                              a12, a22);
    kd::SolveQ q;
    if (!kd::solve_fixed(min_eig, b_shift, a11, a12, a22, q))
        return false;
    state->g = q.g;
    return true;
}

void
LkTemplate::mismatch(const uint8_t *window, int32_t qx, int32_t qy,
                     int64_t &b1, int64_t &b2) const
{
    constexpr int32_t frac = kd::kQOne - 1;
    kd::mismatch_window(config(), {window, lkWindow},
                        kd::weights_q(qx & frac, qy & frac), state->scratch,
                        b1, b2);
}

LkTemplate::Step
LkTemplate::step(int64_t b1, int64_t b2, int32_t pdx, int32_t pdy,
                 bool first, int64_t eps2, int b_shift) const
{
    const kd::StepQ s = kd::step_fixed(limits(0, eps2, b_shift), state->g,
                                       b1, b2, pdx, pdy, first);
    return {s.dx, s.dy, s.converged, s.oscillating};
}

} // namespace of_reference
} // namespace gem5
