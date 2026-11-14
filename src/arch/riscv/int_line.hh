#ifndef __ARCH_RISCV_INT_LINE_HH__
#define __ARCH_RISCV_INT_LINE_HH__

#include <cstdint>
#include <deque>
#include <utility>

#include "dev/intpin.hh"
#include "params/RiscvIntLine.hh"
#include "sim/clocked_object.hh"
#include "sim/eventq.hh"

namespace gem5
{

class System;

namespace RiscvISA
{

// A level-sensitive line into one mip bit of a core (see RiscvIntLine.py).
class RiscvIntLine : public ClockedObject
{
  public:
    PARAMS(RiscvIntLine);
    RiscvIntLine(const Params &p);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

    // The pin's two edges
    void raiseInterruptPin(int number);
    void lowerInterruptPin(int number);

  private:
    // Pass a level on to the core delay clock edges from now.
    void edge(bool high);
    void deliver();
    void set(bool high);

    System *system;
    const uint32_t hartId;
    const uint32_t intNum;
    const Cycles delay;
    IntSinkPin<RiscvIntLine> pin;
    // Levels on their way through the registers, oldest first, with the
    // tick each reaches the core.
    std::deque<std::pair<Tick, bool>> inFlight;
    EventFunctionWrapper deliverEvent;
};

} // namespace RiscvISA
} // namespace gem5

#endif // __ARCH_RISCV_INT_LINE_HH__
