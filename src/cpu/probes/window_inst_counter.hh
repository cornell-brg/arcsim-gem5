// Counts the instructions a core retires inside measurement windows that the program
// marks by address: a window opens when an instruction at an open address retires and
// closes when one at a close address retires. The count of a window is the instructions
// retired after the opening one, up to and including the closing one. Each closed window
// adds one line, its count, to a file, in the order the windows ran.
//
// It listens to the CPU's RetiredInstsPC probe point, which every CPU model notifies
// once per whole instruction (at its last micro-op), so the count is of instructions,
// not micro-ops, on the atomic CPU and on MinorCPU alike.

#ifndef __CPU_PROBES_WINDOW_INST_COUNTER_HH__
#define __CPU_PROBES_WINDOW_INST_COUNTER_HH__

#include <cstdint>
#include <fstream>
#include <unordered_set>

#include "params/WindowInstCounter.hh"
#include "sim/probe/probe_listener_object.hh"

namespace gem5
{

class WindowInstCounter : public ProbeListenerObject
{
  public:
    WindowInstCounter(const WindowInstCounterParams &params);

    void regProbeListeners() override;

    void retired(const Addr &pc);

  private:
    std::unordered_set<Addr> openPCs;
    std::unordered_set<Addr> closePCs;
    std::ofstream out;
    bool counting = false;
    uint64_t count = 0;
};

} // namespace gem5

#endif // __CPU_PROBES_WINDOW_INST_COUNTER_HH__
