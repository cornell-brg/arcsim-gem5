// Counts the instructions and cycles of each task a program runs, one run of a node's
// step, from the marks the program makes. A mark is a call to its mark function, whose
// first instruction (at mark_pc) leaves the argument registers alone; when it commits,
// the kind and value are read from arg_regs:
//   kind 1, switch: the thread now running belongs to node value, or to none of the
//           nodes for 0xffffffff (an idle or timer thread);
//   kind 2, begin:  node value's task begins (its thread is the one running);
//   kind 3, end:    node value's task ends.
//
// Each whole instruction counts in one context: the exception the core is in, if the
// classifier tells one (activeException); otherwise the open task of the node whose
// thread runs; otherwise that node's thread outside its tasks, or the threads of none
// of the nodes, or the time before the first switch. Its cycles are those from the
// commit of the instruction before to its own (CPU::curCycle()), so an instruction
// takes the cycles it waited for; time the CPU sleeps is its own context instead.
//
// Each task ends with a line in file:
//   task node=<n> index=<k> begin=<tick> end=<tick> insts=<i> cycles=<c>
//        [c.<class>=<count> ...] [s.<stat>=<growth> ...]
// its index counting the node's tasks from 0, its instructions and cycles those in the
// task's context (an exception or another node's task that preempts it counts apart),
// its instructions by the classifier's class names (c.other for none, classes of one
// name together), and how much each statistic grew in its context, read whenever the
// context changes. At the end of the simulation, a line each for the other contexts:
//   other context=<exception.<n>|node.<n>|none|before|sleep> insts=... cycles=... ...
// and a task still open as an "open" line.

#ifndef __CPU_PROBES_TASK_INST_COUNTER_HH__
#define __CPU_PROBES_TASK_INST_COUNTER_HH__

#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/stats/info.hh"
#include "base/types.hh"
#include "cpu/base.hh"
#include "cpu/op_class.hh"
#include "cpu/simple/inst_latency.hh"
#include "params/TaskInstCounter.hh"
#include "sim/probe/probe_listener_object.hh"

namespace gem5
{

class TaskInstCounter : public ProbeListenerObject
{
  public:
    TaskInstCounter(const TaskInstCounterParams &params);

    void regProbeListeners() override;
    void startup() override;

    void committed(const BaseCPU::CommittedOp &op);
    void sleeping(const bool &asleep);

  private:
    /** What one context ran. */
    struct Counts
    {
        uint64_t insts = 0;
        uint64_t cycles = 0;
        std::vector<uint64_t> classes;
        std::vector<double> stats;
    };

    struct Task
    {
        uint64_t index;
        Tick begin;
        Counts counts;
    };

    /** The context of an instruction committing now. */
    Counts &context();
    Counts &other(const std::string &name);
    /** Give the statistics' growth since they were last read to c. */
    void readStats(Counts &c);
    void mark(uint32_t kind, uint32_t value);
    void write(const std::string &head, const Counts &c);
    void finish();

    BaseCPU *cpu;
    const Addr markPC;
    std::vector<RegIndex> argRegs;
    std::ofstream out;
    BaseInstLatency *classifier;
    const std::vector<std::string> statNames;
    std::vector<const statistics::ScalarInfo *> statInfos;
    std::vector<double> statLast;

    /** Column names, and each class's column; the last column is c.other. */
    std::vector<std::string> columns;
    std::vector<size_t> classColumn;

    /** The node whose thread runs; None before the first switch. */
    static constexpr int64_t Before = -2;
    static constexpr int64_t NoNode = -1;
    int64_t thread = Before;
    std::map<uint32_t, Task> open;
    std::unordered_map<uint32_t, uint64_t> taskCount;
    std::map<std::string, Counts> others;

    /** The context counted last, whose statistics are not read yet. */
    Counts *last = nullptr;
    Cycles lastCycle{0};
    bool started = false;
    bool asleep = false;
    Cycles sleepStart{0};

    // The whole instruction in flight: its first memory micro-op's class.
    OpClass memClass = No_OpClass;
};

} // namespace gem5

#endif // __CPU_PROBES_TASK_INST_COUNTER_HH__
