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
