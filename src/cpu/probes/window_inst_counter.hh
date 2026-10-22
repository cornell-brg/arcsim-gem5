// Counts the instructions a core retires inside measurement windows that the program
// marks by address: a window opens when an instruction at an open address retires and
// closes when one at a close address retires. The count of a window is the instructions
// retired after the opening one, up to and including the closing one. Each closed window
// adds one line, its count, to a file, in the order the windows ran.
//
// With a classes file, each window also adds a line there with the same instructions by
// gem5's OpClass, two ways, as name=count pairs for the classes that occur:
// inst_<class> counts whole instructions by the class of the instruction itself (for an
// instruction split into micro-ops, its macro-op's class), and op_<class> counts
// micro-ops by their own class (an instruction that is not split is one micro-op). The
// inst_ counts add up to the window's count.
//
// It listens to the CPU's CommittedOps probe point, which the CPU models notify for
// every committed instruction and micro-op; a whole instruction ends at its last
// micro-op, as for the RetiredInstsPC probe point. Only the atomic CPU reports the
// macro-op of a micro-op, so the classes need it.

#ifndef __CPU_PROBES_WINDOW_INST_COUNTER_HH__
#define __CPU_PROBES_WINDOW_INST_COUNTER_HH__

#include <array>
#include <cstdint>
#include <fstream>
#include <unordered_set>

#include "cpu/base.hh"
#include "cpu/op_class.hh"
#include "params/WindowInstCounter.hh"
#include "sim/probe/probe_listener_object.hh"

namespace gem5
{

class WindowInstCounter : public ProbeListenerObject
{
  public:
    WindowInstCounter(const WindowInstCounterParams &params);

    void regProbeListeners() override;

    void committed(const BaseCPU::CommittedOp &op);

  private:
    void writeClasses();

    std::unordered_set<Addr> openPCs;
    std::unordered_set<Addr> closePCs;
    std::ofstream out;
    std::ofstream classesOut;
    const bool classes;
    bool counting = false;
    uint64_t count = 0;
    std::array<uint64_t, Num_OpClasses> instClasses{};
    std::array<uint64_t, Num_OpClasses> opClasses{};
};

} // namespace gem5

#endif // __CPU_PROBES_WINDOW_INST_COUNTER_HH__
