#ifndef __ARCH_RISCV_XH3IRQ_HH__
#define __ARCH_RISCV_XH3IRQ_HH__

#include <cstdint>
#include <vector>

#include "base/types.hh"
#include "dev/intpin.hh"
#include "params/Xh3irq.hh"
#include "params/Xh3irqLine.hh"
#include "sim/sim_object.hh"

namespace gem5
{

class System;

namespace RiscvISA
{

// Hazard3's external interrupt controller (see Xh3irq.py). The CSR
// instruction hands it the CSR and the instruction's source operand, which
// picks the array window.
class Xh3irq : public SimObject
{
  public:
    PARAMS(Xh3irq);
    Xh3irq(const Params &p);

    // A read of one of its CSRs, the window chosen by raw.
    RegVal read(int csr, RegVal raw) const;
    // A write of data to one of its CSRs, the window chosen by raw.
    void write(int csr, RegVal raw, RegVal data);

    // An interrupt line's level.
    void setLevel(uint32_t irq, bool high);

    uint32_t numIrqs() const { return _numIrqs; }

  private:
    // Set or clear mip.meip to match the arrays.
    void update();

    System *system;
    const uint32_t hartId;
    const uint32_t _numIrqs;
    const uint8_t priorityMask;  // the implemented bits of a 4-bit priority

    std::vector<bool> enable;    // h3.meiea
    std::vector<bool> force;     // h3.meifa
    std::vector<bool> level;     // the lines
    std::vector<uint8_t> priority;  // h3.meipra
    bool meip = false;
};

// One external interrupt line of an Xh3irq as an interrupt sink pin (see
// Xh3irq.py).
class Xh3irqLine : public SimObject
{
  public:
    PARAMS(Xh3irqLine);
    Xh3irqLine(const Params &p);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
    void init() override;

    // The pin's two edges
    void raiseInterruptPin(int number);
    void lowerInterruptPin(int number);

  private:
    Xh3irq *controller;
    const uint32_t irqNum;
    IntSinkPin<Xh3irqLine> pin;
};

} // namespace RiscvISA
} // namespace gem5

#endif // __ARCH_RISCV_XH3IRQ_HH__
