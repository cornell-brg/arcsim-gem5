#ifndef __CPU_SIMPLE_INST_LATENCY_HH__
#define __CPU_SIMPLE_INST_LATENCY_HH__

#include <string>
#include <vector>

#include "base/types.hh"
#include "cpu/op_class.hh"
#include "cpu/static_inst_fwd.hh"
#include "params/BaseInstLatency.hh"
#include "sim/faults.hh"
#include "sim/sim_object.hh"

namespace gem5
{

class PCStateBase;
class ThreadContext;

/**
 * The cycles each instruction takes on an atomic CPU that has one
 * (BaseAtomicSimpleCPU's inst_latency), in place of one clock period per
 * micro-op. The CPU tells it, in order, about:
 *
 * - each exception it takes before an instruction (exceptionEntry);
 * - each micro-op, or instruction that is not split, before and after it
 *   executes (beforeOp, afterOp);
 * - each memory access an instruction makes, fetch included, with the
 *   latency the memory returned (access);
 * - the end of each whole instruction (retire), which returns the
 *   instruction's cycles, its memory accesses' included.
 *
 * An instruction that takes no cycles (one the core folds into its
 * neighbour) leaves the CPU to run the next in the same cycle.
 */
class BaseInstLatency : public SimObject
{
  public:
    using Params = BaseInstLatencyParams;
    BaseInstLatency(const Params &p) : SimObject(p) {}

    /** An interrupt was taken before the next instruction; its entry's
     *  cycles. */
    virtual Cycles exceptionEntry(ThreadContext *tc) = 0;

    /** fault was invoked: an exception an instruction or its fetch raised
     *  (an exception return among them on Arm M-profile). Its cycles. */
    virtual Cycles faultTaken(const Fault &fault, ThreadContext *tc) = 0;

    /** inst, a micro-op of macro (or a whole instruction, macro null), is
     *  about to execute: the cycles its operands decide are read now. */
    virtual void beforeOp(const StaticInstPtr &inst,
                          const StaticInstPtr &macro, ThreadContext *tc) = 0;

    /** inst executed without a fault. */
    virtual void afterOp(const StaticInstPtr &inst,
                         const StaticInstPtr &macro, ThreadContext *tc) = 0;

    /** A memory access of size bytes at paddr that the memory answered
     *  in latency (the CPU's cycles, rounded up), an instruction fetch if
     *  fetch. */
    virtual void access(Addr paddr, unsigned size, Cycles latency,
                        bool fetch) = 0;

    /** The whole instruction (macro, or the instruction itself) at pc
     *  retired, the thread going on at next; now is the cycle it began.
     *  Returns its cycles. */
    virtual Cycles retire(const StaticInstPtr &whole, const PCStateBase &pc,
                          const PCStateBase &next, Cycles now,
                          ThreadContext *tc) = 0;

    /** After retire: whether the core had fetched past the instruction
     *  before it took effect (as past a taken branch), and if so the
     *  address of a word it read. The CPU reads that word, and the read's
     *  latency comes to access() as a fetch's. */
    virtual bool fetchedAhead(Addr &addr) { return false; }

    // Classification only, for probes that count by the same classes on
    // any CPU model (TaskInstCounter); none of it changes the state above.

    /** Each class's name, by class number; classes may share a name. */
    virtual std::vector<std::string> classNames() const { return {}; }

    /** The class of the whole instruction whole, whose first memory
     *  micro-op has mem_class (No_OpClass for none); -1 for none. */
    virtual int
    classOf(const StaticInst &whole, OpClass mem_class) const
    {
        return -1;
    }

    /** The exception the core is in now, 0 for none. */
    virtual int activeException(ThreadContext *tc) const { return 0; }
};

} // namespace gem5

#endif // __CPU_SIMPLE_INST_LATENCY_HH__
