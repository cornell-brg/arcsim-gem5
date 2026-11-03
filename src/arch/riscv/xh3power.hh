#ifndef __ARCH_RISCV_XH3POWER_HH__
#define __ARCH_RISCV_XH3POWER_HH__

#include <cstdint>
#include <vector>

#include "params/Xh3power.hh"
#include "sim/sim_object.hh"

namespace gem5
{

class System;
class ThreadContext;

namespace RiscvISA
{

// Hazard3's h3.block and h3.unblock for a system's cores (see Xh3power.py).
// The instructions hand it the core that executes them.
class Xh3power : public SimObject
{
  public:
    PARAMS(Xh3power);
    Xh3power(const Params &p);

    void startup() override;

    // h3.block; wake is whether an interrupt would wake a wfi now. The core
    // sleeps unless it or the core's unblock request lets it fall through.
    void block(ThreadContext *tc, bool wake);
    // h3.unblock: every other core's request, or its wake from h3.block.
    void unblock(ThreadContext *tc);
    // wfi: the core's sleep, if it sleeps, is not on h3.block.
    void wfi(ThreadContext *tc);

    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

  private:
    // The index of tc's core in hartIds.
    size_t core(ThreadContext *tc) const;

    System *system;
    const std::vector<uint32_t> hartIds;
    // Each core's sticky unblock request (hazard3_power_ctrl.v's
    // block_wakeup_req), and whether its last sleep was on h3.block; that
    // sleep is the one an h3.unblock ends while the core is suspended.
    std::vector<bool> wakeupReq;
    std::vector<bool> sleepingOnBlock;
};

} // namespace RiscvISA
} // namespace gem5

#endif // __ARCH_RISCV_XH3POWER_HH__
