/*
 * Hazard3's branch predictor (BRANCH_PREDICTOR = 1): a single-entry BTB
 * that holds the last taken backward conditional branch, and a branch is
 * predicted taken exactly when it hits that entry (Hazard3 hdl/
 * hazard3_frontend.v have_btb, hazard3_core.v x_btb_set and x_btb_clear).
 *
 * Hazard3BTB is the entry. Hazard3BP, the conditional predictor, says
 * taken for every conditional branch, so that BPredUnit takes a branch
 * exactly when the BTB has it, and clears the entry when a branch it
 * predicted falls through. Jumps never enter the BTB, so with no RAS and
 * no indirect predictor they are never predicted, as in the RTL.
 */

#ifndef __CPU_PRED_HAZARD3_BP_HH__
#define __CPU_PRED_HAZARD3_BP_HH__

#include <memory>

#include "cpu/pred/btb.hh"
#include "cpu/pred/conditional.hh"
#include "params/Hazard3BP.hh"
#include "params/Hazard3BTB.hh"

namespace gem5::branch_prediction
{

class Hazard3BTB : public BranchTargetBuffer
{
  public:
    Hazard3BTB(const Hazard3BTBParams &params);

    void memInvalidate() override { clear(); }
    bool valid(ThreadID tid, Addr instPC) override;
    const PCStateBase *lookup(ThreadID tid, Addr instPC,
                              BranchType type = BranchType::NoBranch) override;
    /** Called for a taken branch that was not predicted: a conditional
     *  branch to a lower address takes the entry (x_btb_set); anything
     *  else leaves it */
    void update(ThreadID tid, Addr instPC, const PCStateBase &target_pc,
                BranchType type = BranchType::NoBranch,
                StaticInstPtr inst = nullptr) override;
    const StaticInstPtr getInst(ThreadID tid, Addr instPC) override;

    /** x_btb_clear */
    void clear();

  private:
    bool entryValid = false;
    Addr srcPC = 0;
    std::unique_ptr<PCStateBase> target;
    StaticInstPtr srcInst;
};

class Hazard3BP : public ConditionalPredictor
{
  public:
    Hazard3BP(const Hazard3BPParams &params);

    Prediction lookup(ThreadID tid, Addr pc, void *&bp_history) override;
    void updateHistories(ThreadID tid, Addr pc, bool uncond, bool taken,
                         Addr target, const StaticInstPtr &inst,
                         void *&bp_history) override;
    void squash(ThreadID tid, void *&bp_history) override;
    void update(ThreadID tid, Addr pc, bool taken, void *&bp_history,
                bool squashed, const StaticInstPtr &inst,
                Addr target) override;

  private:
    Hazard3BTB *btb;
};

} // namespace gem5::branch_prediction

#endif // __CPU_PRED_HAZARD3_BP_HH__
