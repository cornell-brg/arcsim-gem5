#include "arch/riscv/xh3power.hh"

#include <algorithm>

#include "base/logging.hh"
#include "cpu/base.hh"
#include "cpu/thread_context.hh"
#include "sim/serialize.hh"
#include "sim/system.hh"

namespace gem5
{

namespace RiscvISA
{

Xh3power::Xh3power(const Params &p)
    : SimObject(p), system(p.system), hartIds(p.hart_ids),
      wakeupReq(p.hart_ids.size()), sleepingOnBlock(p.hart_ids.size())
{
    fatal_if(hartIds.empty(), "Xh3power: no hart_ids");
    for (size_t i = 0; i < hartIds.size(); ++i) {
        fatal_if(std::count(hartIds.begin(), hartIds.end(), hartIds[i]) > 1,
                 "Xh3power: hart %u is listed twice", hartIds[i]);
    }
}

// The cores register their threads in their init()
void
Xh3power::startup()
{
    SimObject::startup();
    for (uint32_t hart : hartIds) {
        fatal_if(hart >= system->threads.size(),
                 "Xh3power: hart %u is not a thread of the system", hart);
    }
}

size_t
Xh3power::core(ThreadContext *tc) const
{
    auto it = std::find(hartIds.begin(), hartIds.end(),
                        (uint32_t)tc->contextId());
    panic_if(it == hartIds.end(),
             "Xh3power: hart %d is not in hart_ids", tc->contextId());
    return it - hartIds.begin();
}

void
Xh3power::block(ThreadContext *tc, bool wake)
{
    size_t n = core(tc);
    if (wakeupReq[n] || wake) {
        // Released at once; the release clears the request
        wakeupReq[n] = false;
        sleepingOnBlock[n] = false;
        return;
    }
    sleepingOnBlock[n] = true;
    tc->quiesce();
}

void
Xh3power::unblock(ThreadContext *tc)
{
    size_t self = core(tc);
    for (size_t n = 0; n < hartIds.size(); ++n) {
        if (n == self)
            continue;
        ThreadContext *peer = system->threads[hartIds[n]];
        if (sleepingOnBlock[n] &&
                peer->status() == ThreadContext::Suspended) {
            // The request wakes it, and its release clears the request
            peer->getCpuPtr()->wakeup(peer->threadId());
        } else {
            wakeupReq[n] = true;
        }
        sleepingOnBlock[n] = false;
    }
}

void
Xh3power::wfi(ThreadContext *tc)
{
    sleepingOnBlock[core(tc)] = false;
}

void
Xh3power::serialize(CheckpointOut &cp) const
{
    std::vector<uint8_t> wakeup_req(wakeupReq.begin(), wakeupReq.end());
    std::vector<uint8_t> sleeping_on_block(sleepingOnBlock.begin(),
                                           sleepingOnBlock.end());
    SERIALIZE_CONTAINER(wakeup_req);
    SERIALIZE_CONTAINER(sleeping_on_block);
}

void
Xh3power::unserialize(CheckpointIn &cp)
{
    std::vector<uint8_t> wakeup_req;
    std::vector<uint8_t> sleeping_on_block;
    UNSERIALIZE_CONTAINER(wakeup_req);
    UNSERIALIZE_CONTAINER(sleeping_on_block);
    for (size_t n = 0; n < hartIds.size(); ++n) {
        wakeupReq[n] = n < wakeup_req.size() && wakeup_req[n];
        sleepingOnBlock[n] = n < sleeping_on_block.size() &&
                             sleeping_on_block[n];
    }
}

} // namespace RiscvISA
} // namespace gem5
