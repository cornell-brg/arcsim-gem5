#ifndef __ARCH_RISCV_INST_LATENCY_HH__
#define __ARCH_RISCV_INST_LATENCY_HH__

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/addr_range.hh"
#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/op_class.hh"
#include "cpu/reg_class.hh"
#include "cpu/simple/inst_latency.hh"
#include "params/RiscvInstLatency.hh"
#include "params/RiscvLatencyClass.hh"
#include "sim/sim_object.hh"

namespace gem5
{

namespace RiscvISA
{

/** One class of instruction (RiscvInstLatency.py). */
class RiscvLatencyClass : public SimObject
{
  public:
    using Params = RiscvLatencyClassParams;
    RiscvLatencyClass(const Params &p);

    /** Whether it takes an instruction of op_class and machine code emi. */
    bool takes(OpClass op_class, uint64_t emi) const;

    /** Whether source operand i is read a cycle late, so that a result
     *  delay does not hold it. */
    bool late(int i) const;

    const std::string description;
    const Cycles cycles;
    const Cycles resultDelay;
    const int unit;
    const Cycles busy;

  private:
    std::vector<bool> opClasses;
    const uint64_t mask;
    const uint64_t match;
    std::vector<Cycles> srcLats;
    Cycles minSrcLat;
};

/** Each instruction's cycles on an in-order RISC-V core such as Hazard3
 *  (RiscvInstLatency.py). */
class RiscvInstLatency : public BaseInstLatency
{
  public:
    using Params = RiscvInstLatencyParams;
    RiscvInstLatency(const Params &p);

    Cycles exceptionEntry(ThreadContext *tc) override;
    Cycles faultTaken(const Fault &fault, ThreadContext *tc) override;
    void beforeOp(const StaticInstPtr &inst, const StaticInstPtr &macro,
                  ThreadContext *tc) override;
    void afterOp(const StaticInstPtr &inst, const StaticInstPtr &macro,
                 ThreadContext *tc) override;
    void access(Addr paddr, unsigned size, Cycles latency,
                bool fetch) override;
    Cycles retire(const StaticInstPtr &whole, const PCStateBase &pc,
                  const PCStateBase &next, Cycles now,
                  ThreadContext *tc) override;

    std::vector<std::string> classNames() const override;
    int classOf(const StaticInst &whole, OpClass mem_class) const override;
    /** No exception number to read: 0. */
    int activeException(ThreadContext *tc) const override { return 0; }

  private:
    /** A register: its class and flat index in one key. */
    using Reg = uint32_t;
    static Reg regKey(const RegId &reg);

    struct Source
    {
        Reg reg;
        bool late;
    };

    struct Result
    {
        Reg reg;
        int cls;
    };

    /** The class that takes an instruction, or -1. */
    int classify(OpClass op_class, uint64_t emi) const;

    void clearInFlight();

    std::vector<RiscvLatencyClass *> classes;
    const Cycles defaultCycles;
    const Cycles takenBranch;
    const Cycles notTakenBranch;
    const Cycles predictedBranch;
    const bool predictor;
    const Cycles unalignedTarget;
    const Cycles entryCycles;
    const bool e906DivSqrt;
    /** This instruction's cycles from its operands (e906DivSqrt), or 0 */
    Cycles operandCycles{0};
    std::vector<AddrRange> timedRanges;
    std::vector<AddrRange> waitRanges;
    std::vector<Cycles> waitCycles;
    std::vector<Cycles> waitWriteCycles;

    // The instruction in flight: what its micro-ops did so far.
    OpClass memOpClass = No_OpClass;
    bool hasMemOp = false;
    bool inFlightStore = false;
    bool control = false;
    unsigned ops = 0;
    std::vector<Source> sources;
    std::vector<Result> results;
    Cycles memoryCycles{0};

    /** When each register's last result can be read without waiting. */
    std::unordered_map<Reg, Cycles> ready;

    /** The instruction before was a taken branch or jump to this one. */
    bool branchedTo = false;
    /** The predictor's one entry: the last taken backward branch. */
    bool havePredicted = false;
    Addr predictedPC = 0;

    /** When each unit is done with its instruction. */
    std::array<Cycles, 4> unitFree{};

    struct ClassStats : public statistics::Group
    {
        ClassStats(statistics::Group *parent, const std::string &name);
        statistics::Scalar insts;
        statistics::Scalar cycles;
    };

    struct LatencyStats : public statistics::Group
    {
        LatencyStats(statistics::Group *parent);
        statistics::Scalar insts;
        statistics::Scalar cycles;
        statistics::Scalar memoryCycles;
        statistics::Scalar resultWaits;
        statistics::Scalar unitWaits;
        statistics::Scalar takenBranches;
        statistics::Scalar predictedBranches;
        statistics::Scalar mispredictedBranches;
        statistics::Scalar unalignedTargets;
        statistics::Scalar entries;
    } stats;

    std::vector<std::unique_ptr<ClassStats>> ownStats;
    std::vector<ClassStats *> classStats;
    ClassStats *otherStats = nullptr;
};

} // namespace RiscvISA
} // namespace gem5

#endif // __ARCH_RISCV_INST_LATENCY_HH__
