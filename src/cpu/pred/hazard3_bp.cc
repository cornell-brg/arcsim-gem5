#include "cpu/pred/hazard3_bp.hh"

#include "base/trace.hh"
#include "debug/BTB.hh"

namespace gem5::branch_prediction
{

Hazard3BTB::Hazard3BTB(const Hazard3BTBParams &p)
    : BranchTargetBuffer(p)
{
}

void
Hazard3BTB::clear()
{
    if (entryValid)
        DPRINTF(BTB, "Hazard3 BTB: clear entry %#x\n", srcPC);
    entryValid = false;
}

bool
Hazard3BTB::valid(ThreadID tid, Addr instPC)
{
    return entryValid && instPC == srcPC;
}

const PCStateBase *
Hazard3BTB::lookup(ThreadID tid, Addr instPC, BranchType type)
{
    stats.lookups[type]++;
    if (valid(tid, instPC))
        return target.get();
    stats.misses[type]++;
    return nullptr;
}

const StaticInstPtr
Hazard3BTB::getInst(ThreadID tid, Addr instPC)
{
    return valid(tid, instPC) ? srcInst : nullptr;
}

void
Hazard3BTB::update(ThreadID tid, Addr instPC, const PCStateBase &target_pc,
                   BranchType type, StaticInstPtr inst)
{
    /* x_btb_set: a taken conditional branch with a negative offset */
    if (!inst || !inst->isCondCtrl() || inst->isIndirectCtrl() ||
        target_pc.instAddr() >= instPC)
    {
        return;
    }
    stats.updates[type]++;
    if (entryValid && srcPC != instPC)
        stats.evictions++;
    DPRINTF(BTB, "Hazard3 BTB: set entry %#x -> %#x\n", instPC,
            target_pc.instAddr());
    entryValid = true;
    srcPC = instPC;
    target.reset(target_pc.clone());
    srcInst = inst;
}

Hazard3BP::Hazard3BP(const Hazard3BPParams &p)
    : ConditionalPredictor(p), btb(p.btb)
{
}

Prediction
Hazard3BP::lookup(ThreadID tid, Addr pc, void *&bp_history)
{
    bp_history = nullptr;
    /* Taken: BPredUnit then takes the branch only on a BTB hit */
    return predictWithDefaultLatency(true);
}

void
Hazard3BP::updateHistories(ThreadID tid, Addr pc, bool uncond, bool taken,
                           Addr target, const StaticInstPtr &inst,
                           void *&bp_history)
{
}

void
Hazard3BP::squash(ThreadID tid, void *&bp_history)
{
}

void
Hazard3BP::update(ThreadID tid, Addr pc, bool taken, void *&bp_history,
                  bool squashed, const StaticInstPtr &inst, Addr target)
{
    /* A branch predicted taken that falls through clears the entry
     *  (x_btb_clear: x_branch_was_predicted && x_jump_req_unchecked).
     *  Only a predicted branch (a BTB hit) can be squashed as not taken. */
    if (squashed && !taken && btb->valid(tid, pc))
        btb->clear();
}

} // namespace gem5::branch_prediction
