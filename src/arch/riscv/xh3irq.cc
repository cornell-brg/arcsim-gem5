#include "arch/riscv/xh3irq.hh"

#include "arch/riscv/faults.hh"
#include "arch/riscv/regs/misc.hh"
#include "base/bitfield.hh"
#include "base/logging.hh"
#include "cpu/base.hh"
#include "cpu/thread_context.hh"
#include "sim/system.hh"

namespace gem5
{

namespace RiscvISA
{

namespace
{

// The array CSRs carry their 16-bit window in bits 31:16: 16 interrupts for
// h3.meiea, h3.meipa and h3.meifa (window chosen by the operand's bits 4:0),
// four 4-bit priorities for h3.meipra (its bits 6:0).
constexpr int WindowShift = 16;
constexpr uint32_t MaxIrqs = 512;

} // namespace

Xh3irq::Xh3irq(const Params &p)
    : SimObject(p), system(p.system), hartId(p.hart_id),
      _numIrqs(p.num_irqs),
      priorityMask(0xf & ~(0xf >> p.priority_bits)),
      enable(p.num_irqs), force(p.num_irqs), level(p.num_irqs),
      priority(p.num_irqs)
{
    fatal_if(p.num_irqs == 0 || p.num_irqs > MaxIrqs,
             "Xh3irq: num_irqs=%u is not 1 to %u", p.num_irqs, MaxIrqs);
    fatal_if(p.priority_bits < 1 || p.priority_bits > 4,
             "Xh3irq: priority_bits=%u is not 1 to 4",
             (unsigned)p.priority_bits);
}

RegVal
Xh3irq::read(int csr, RegVal raw) const
{
    RegVal window = 0;
    switch (csr) {
      case CSR_MEIEA:
      case CSR_MEIPA:
      case CSR_MEIFA: {
        const uint32_t first = bits(raw, 4, 0) * 16;
        for (unsigned k = 0; k < 16 && first + k < _numIrqs; ++k) {
            const uint32_t i = first + k;
            bool bit = csr == CSR_MEIEA ? enable[i]
                     : csr == CSR_MEIFA ? force[i]
                     : level[i] || force[i];
            window |= RegVal(bit) << k;
        }
        break;
      }
      case CSR_MEIPRA: {
        const uint32_t first = bits(raw, 6, 0) * 4;
        for (unsigned k = 0; k < 4 && first + k < _numIrqs; ++k)
            window |= RegVal(priority[first + k]) << (4 * k);
        break;
      }
      default:
        panic("Xh3irq: read of CSR %#x", csr);
    }
    return window << WindowShift;
}

void
Xh3irq::write(int csr, RegVal raw, RegVal data)
{
    const RegVal window = bits(data, 31, WindowShift);
    switch (csr) {
      case CSR_MEIEA:
      case CSR_MEIFA: {
        auto &array = csr == CSR_MEIEA ? enable : force;
        const uint32_t first = bits(raw, 4, 0) * 16;
        for (unsigned k = 0; k < 16 && first + k < _numIrqs; ++k)
            array[first + k] = bits(window, k);
        break;
      }
      case CSR_MEIPA:
        // Read-only: the lines and the force array.
        break;
      case CSR_MEIPRA: {
        const uint32_t first = bits(raw, 6, 0) * 4;
        for (unsigned k = 0; k < 4 && first + k < _numIrqs; ++k)
            priority[first + k] = bits(window, 4 * k + 3, 4 * k) &
                                  priorityMask;
        break;
      }
      default:
        panic("Xh3irq: write of CSR %#x", csr);
    }
    update();
}

void
Xh3irq::setLevel(uint32_t irq, bool high)
{
    panic_if(irq >= _numIrqs, "Xh3irq: no interrupt %u", irq);
    level[irq] = high;
    update();
}

void
Xh3irq::update()
{
    bool any = false;
    for (uint32_t i = 0; i < _numIrqs && !any; ++i)
        any = enable[i] && (level[i] || force[i]);
    if (any == meip)
        return;
    meip = any;
    ThreadContext *tc = system->threads[hartId];
    if (meip)
        tc->getCpuPtr()->postInterrupt(tc->threadId(), INT_EXT_MACHINE, 0);
    else
        tc->getCpuPtr()->clearInterrupt(tc->threadId(), INT_EXT_MACHINE, 0);
}

Xh3irqLine::Xh3irqLine(const Params &p)
    : SimObject(p), controller(p.controller), irqNum(p.irq_num),
      pin(name() + ".pin", 0, this)
{
}

Port &
Xh3irqLine::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "pin")
        return pin;
    return SimObject::getPort(if_name, idx);
}

void
Xh3irqLine::init()
{
    SimObject::init();
    fatal_if(irqNum >= controller->numIrqs(),
             "Xh3irqLine: irq_num=%u is out of range (num_irqs=%u)",
             irqNum, controller->numIrqs());
}

void
Xh3irqLine::raiseInterruptPin(int number)
{
    controller->setLevel(irqNum, true);
}

void
Xh3irqLine::lowerInterruptPin(int number)
{
    controller->setLevel(irqNum, false);
}

} // namespace RiscvISA
} // namespace gem5
