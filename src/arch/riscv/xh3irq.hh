#ifndef __ARCH_RISCV_XH3IRQ_HH__
#define __ARCH_RISCV_XH3IRQ_HH__

#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

#include "base/types.hh"
#include "dev/intpin.hh"
#include "params/Xh3irq.hh"
#include "params/Xh3irqLine.hh"
#include "sim/clocked_object.hh"
#include "sim/eventq.hh"
#include "sim/sim_object.hh"

namespace gem5
{

class System;
class ThreadContext;

namespace RiscvISA
{

// Hazard3's external interrupt controller (see Xh3irq.py). The CSR
// instruction hands it the CSR, the instruction's source operand (which
// picks the array window), whether it is a clear (csrrc, csrrci) and, for
// the write, whether the instruction also read the CSR.
class Xh3irq : public SimObject
{
  public:
    PARAMS(Xh3irq);
    Xh3irq(const Params &p);

    // A read of one of its CSRs.
    RegVal read(int csr, RegVal raw, bool clear);
    // A write of data to one of its CSRs.
    void write(int csr, RegVal raw, RegVal data, bool clear, bool was_read);

    // Trap entry (in M-mode; eirq when it is the external interrupt) and
    // mret: the preemption priority stack's save and restore.
    void trapEnter(bool eirq);
    void trapExit();

    // An interrupt line's level.
    // One of the irq's lines went high or low; the irq is high while any is.
    void setLevel(uint32_t irq, bool high);

    uint32_t numIrqs() const { return _numIrqs; }

    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

  private:
    // The highest-priority interrupt that is pending, enabled and at or
    // above meicontext.ppreempt (the lowest-numbered of equals), as h3.meinext
    // shows it.
    struct Next
    {
        bool noirq;
        uint32_t irq;
        uint8_t priority;
    };
    Next next() const;
    // The preemption level after taking n (meicontext.preempt's new value).
    uint8_t preemptLevel(const Next &n) const;
    bool pending(uint32_t i) const { return level[i] || force[i]; }

    // Set or clear mip.meip to match the arrays and meicontext.preempt.
    void update();

    ThreadContext *thread() const;

    System *system;
    const uint32_t hartId;
    const uint32_t _numIrqs;
    const uint8_t priorityMask;  // the implemented bits of a 4-bit priority

    std::vector<bool> enable;    // h3.meiea
    std::vector<bool> force;     // h3.meifa
    std::vector<bool> level;     // the lines, ORed
    std::vector<uint32_t> highLines;  // lines high, per irq
    std::vector<uint8_t> priority;  // h3.meipra

    // h3.meicontext
    uint8_t pppreempt = 0;
    uint8_t ppreempt = 0;
    uint8_t preempt = 0;         // 5 bits: 0x10 masks every interrupt
    bool contextNoirq = true;
    uint32_t contextIrq = 0;
    bool mreteirq = false;

    // What the last read of h3.meinext returned, for the write of the same
    // instruction: its update uses the interrupt the read showed, before
    // the read cleared that interrupt's force bit.
    Next lastNext{true, 0, 0};

    bool meip = false;
};

// One external interrupt line of an Xh3irq as an interrupt sink pin (see
// Xh3irq.py).
class Xh3irqLine : public ClockedObject
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
    // Pass a level on to the controller delay clock edges from now.
    void edge(bool high);
    void deliver();

    Xh3irq *controller;
    const uint32_t irqNum;
    const Cycles delay;
    IntSinkPin<Xh3irqLine> pin;
    // Levels on their way through the registers, oldest first, with the
    // tick each reaches the controller.
    std::deque<std::pair<Tick, bool>> inFlight;
    bool delivered = false;      // the level last given to the controller
    void apply(bool high);
    EventFunctionWrapper deliverEvent;
};

} // namespace RiscvISA
} // namespace gem5

#endif // __ARCH_RISCV_XH3IRQ_HH__
