#ifndef __ARCH_ARM_M_INST_LATENCY_HH__
#define __ARCH_ARM_M_INST_LATENCY_HH__

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
#include "params/MProfileInstLatency.hh"
#include "params/MProfileLatencyClass.hh"
#include "sim/sim_object.hh"

namespace gem5
{

namespace ArmISA
{

/** One class of instruction (MProfileInstLatency.py). */
class MProfileLatencyClass : public SimObject
{
  public:
    using Params = MProfileLatencyClassParams;
    MProfileLatencyClass(const Params &p);

    /** Whether it takes an instruction of op_class and ExtMachInst emi. */
    bool takes(OpClass op_class, uint64_t emi) const;

    /** Whether source operand i is read a cycle late, so that a result
     *  delay does not hold it. */
    bool late(int i) const;

    const std::string description;
    const Cycles cycles;
    const Cycles resultDelay;
    const int unit;
    const Cycles busy;
    const bool divide;
    const bool accumulate;
    const Cycles branchExtra;

  private:
    std::vector<bool> opClasses;
    const uint64_t mask;
    const uint64_t match;
    std::vector<Cycles> srcLats;
    Cycles minSrcLat;
};

/** Each instruction's cycles on an Arm M-profile core
 *  (MProfileInstLatency.py). */
class MProfileInstLatency : public BaseInstLatency
{
  public:
    using Params = MProfileInstLatencyParams;
    MProfileInstLatency(const Params &p);

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
    bool fetchedAhead(Addr &addr) override;

  private:
    /** A register: its class and flat index in one key. */
    using Reg = uint32_t;
    static Reg regKey(const RegId &reg);

    /** A source the instruction in flight reads, and whether it reads it a
     *  cycle late. */
    struct Source
    {
        Reg reg;
        bool late;
    };

    /** A result the instruction in flight writes: the class of the
     *  micro-op that writes it (-1 for none) and, for a load's, the bus
     *  transfer it comes in. */
    struct Result
    {
        Reg reg;
        int cls;
        bool loaded;
        unsigned transfer;
    };

    /** The class that takes an instruction, or -1. */
    int classify(OpClass op_class, uint64_t emi) const;

    /** Its fold kind: 1 into the instruction before, 2 into the next. */
    unsigned foldKind(uint64_t emi) const;

    /** Back to the start: the next instruction follows no other (after an
     *  exception's entry or return). */
    void forgetPrevious();

    /** Forget the instruction in flight. */
    void clearInFlight();

    std::vector<MProfileLatencyClass *> classes;
    const Cycles defaultCycles;
    std::vector<uint64_t> foldMasks;
    std::vector<uint64_t> foldMatches;
    std::vector<unsigned> foldKinds;
    const Cycles takenBranch;
    const Cycles notTakenBranch;
    const Cycles branchFromLoad;
    const Cycles unalignedTarget;
    const Cycles entryCycles;
    const Cycles returnCycles;
    const Cycles tailChainCycles;
    const bool fetchAhead;
    std::vector<AddrRange> timedRanges;
    std::vector<AddrRange> waitRanges;
    std::vector<Cycles> waitCycles;
    std::vector<Cycles> waitWriteCycles;

    // The instruction in flight: what its micro-ops did so far.
    /** The OpClass it is classed by: its first memory micro-op's, else
     *  its own. */
    OpClass memOpClass = No_OpClass;
    bool hasMemOp = false;
    /** Its data accesses' aligned transfers on the 32-bit bus, a cycle
     *  each. */
    unsigned transfers = 0;
    /** Whether the micro-op executing now stores. */
    bool inFlightStore = false;
    bool readsMemory = false;
    bool control = false;
    /** The cycles its operands decide (a divide), 0 for none. */
    Cycles operandCycles{0};
    std::vector<Source> sources;
    std::vector<Result> results;
    bool writesFlags = false;
    /** Its memory accesses' cycles. */
    Cycles memoryCycles{0};

    /** When each register's last result can be read without waiting. */
    std::unordered_map<Reg, Cycles> ready;

    // The instruction before.
    bool havePrevious = false;
    Addr prevPC = 0;
    Addr prevNext = 0;
    unsigned prevWidth = 0;
    bool prevFolded = false;
    bool prevSequential = false;
    bool prevWritesFlags = false;
    bool prevPushPop = false;
    /** The instruction before was an IT that folds into this one if it is
     *  16-bit. */
    bool itPending = false;
    /** The instruction before was a taken branch to this one, after no
     *  PUSH or POP. */
    bool branchedTo = false;
    /** An exception return to charge to the next instruction, or to fold
     *  into the next exception's entry. */
    bool returnPending = false;
    /** The word fetched past the last instruction, if any. */
    bool aheadPending = false;
    Addr aheadAddr = 0;

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
        statistics::Scalar folded;
        statistics::Scalar takenBranches;
        statistics::Scalar unalignedTargets;
        statistics::Scalar entries;
        statistics::Scalar returns;
        statistics::Scalar tailChains;
    } stats;

    std::vector<std::unique_ptr<ClassStats>> ownStats;
    /** Each class's statistics (shared by classes of one name), and those
     *  of the instructions no class takes. */
    std::vector<ClassStats *> classStats;
    ClassStats *otherStats = nullptr;
};

} // namespace ArmISA
} // namespace gem5

#endif // __ARCH_ARM_M_INST_LATENCY_HH__
