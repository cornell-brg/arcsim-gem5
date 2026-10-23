#include "arch/riscv/xh3irq.hh"

#include "arch/riscv/faults.hh"
#include "arch/riscv/regs/misc.hh"
#include "base/bitfield.hh"
#include "base/logging.hh"
#include "cpu/base.hh"
#include "cpu/thread_context.hh"
#include "sim/serialize.hh"
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

// mie's timer and software interrupt enables, which h3.meicontext's clearts
// clears and saves and its mtiesave and msiesave set.
constexpr int MieMtie = 7;
constexpr int MieMsie = 3;

std::vector<uint8_t>
toBytes(const std::vector<bool> &v)
{
    return std::vector<uint8_t>(v.begin(), v.end());
}

void
fromBytes(std::vector<bool> &v, const std::vector<uint8_t> &b)
{
    for (size_t i = 0; i < v.size() && i < b.size(); ++i)
        v[i] = b[i];
}

} // namespace

Xh3irq::Xh3irq(const Params &p)
    : SimObject(p), system(p.system), hartId(p.hart_id),
      _numIrqs(p.num_irqs),
      priorityMask(0xf & ~(0xf >> p.priority_bits)),
      enable(p.num_irqs), force(p.num_irqs), level(p.num_irqs),
      highLines(p.num_irqs),
      priority(p.num_irqs)
{
    fatal_if(p.num_irqs == 0 || p.num_irqs > MaxIrqs,
             "Xh3irq: num_irqs=%u is not 1 to %u", p.num_irqs, MaxIrqs);
    fatal_if(p.priority_bits < 1 || p.priority_bits > 4,
             "Xh3irq: priority_bits=%u is not 1 to 4",
             (unsigned)p.priority_bits);
}

ThreadContext *
Xh3irq::thread() const
{
    return system->threads[hartId];
}

Xh3irq::Next
Xh3irq::next() const
{
    Next n{true, 0, 0};
    for (uint32_t i = 0; i < _numIrqs; ++i) {
        if (!enable[i] || !pending(i) || priority[i] < ppreempt)
            continue;
        if (n.noirq || priority[i] > n.priority)
            n = Next{false, i, priority[i]};
    }
    return n;
}

uint8_t
Xh3irq::preemptLevel(const Next &n) const
{
    // One above the interrupt's priority, in units of the lowest implemented
    // priority bit; 0x10 (nothing can preempt) when there is no interrupt.
    const uint8_t level = n.noirq ? 0x10 :
        (1 << (4 - __builtin_popcount(priorityMask))) + n.priority;
    return level & (0x10 | priorityMask);
}

RegVal
Xh3irq::read(int csr, RegVal raw, bool clear)
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
                     : pending(i);
            window |= RegVal(bit) << k;
        }
        return window << WindowShift;
      }
      case CSR_MEIPRA: {
        const uint32_t first = bits(raw, 6, 0) * 4;
        for (unsigned k = 0; k < 4 && first + k < _numIrqs; ++k)
            window |= RegVal(priority[first + k]) << (4 * k);
        return window << WindowShift;
      }
      case CSR_MEINEXT: {
        // Reading the interrupt clears its force bit, so an interrupt can be
        // posted once through h3.meifa.
        lastNext = next();
        if (!lastNext.noirq) {
            force[lastNext.irq] = false;
            update();
        }
        return (RegVal(lastNext.noirq) << 31) | (lastNext.irq << 2);
      }
      case CSR_MEICONTEXT: {
        // mtiesave and msiesave read mie's enables when this instruction
        // clears them (clearts).
        const bool clearts = !clear && bits(raw, 1);
        const RegVal mie = thread()->readMiscReg(MISCREG_IE);
        return (RegVal(pppreempt) << 28) | (RegVal(ppreempt) << 24) |
               (RegVal(preempt) << 16) | (RegVal(contextNoirq) << 15) |
               (RegVal(contextIrq) << 4) |
               (RegVal(clearts && bits(mie, MieMtie)) << 3) |
               (RegVal(clearts && bits(mie, MieMsie)) << 2) |
               RegVal(mreteirq);
      }
      default:
        panic("Xh3irq: read of CSR %#x", csr);
    }
}

void
Xh3irq::write(int csr, RegVal raw, RegVal data, bool clear, bool was_read)
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
      case CSR_MEINEXT:
        // update: take the interrupt h3.meinext shows into meicontext.
        if (bits(data, 0)) {
            const Next n = was_read ? lastNext : next();
            preempt = preemptLevel(n);
            contextNoirq = n.noirq;
            contextIrq = n.irq;
        }
        break;
      case CSR_MEICONTEXT: {
        pppreempt = bits(data, 31, 28) & priorityMask;
        ppreempt = bits(data, 27, 24) & priorityMask;
        preempt = bits(data, 20, 16) & (0x10 | priorityMask);
        contextNoirq = bits(data, 15);
        contextIrq = bits(data, 12, 4);
        mreteirq = bits(data, 0);
        // mtiesave and msiesave set mie's enables back; clearts clears them.
        const bool clearts = !clear && bits(raw, 1);
        RegVal mie = thread()->readMiscReg(MISCREG_IE);
        mie = insertBits(mie, MieMtie,
                         (bits(mie, MieMtie) || bits(data, 3)) && !clearts);
        mie = insertBits(mie, MieMsie,
                         (bits(mie, MieMsie) || bits(data, 2)) && !clearts);
        thread()->setMiscReg(MISCREG_IE, mie);
        break;
      }
      default:
        panic("Xh3irq: write of CSR %#x", csr);
    }
    update();
}

void
Xh3irq::trapEnter(bool eirq)
{
    if (eirq) {
        // Push the preemption priority stack; the new level is that of the
        // interrupt being taken.
        const Next n = next();
        pppreempt = ppreempt & priorityMask;
        ppreempt = bits(preempt, 3, 0) & priorityMask;
        preempt = preemptLevel(n);
        mreteirq = true;
    } else {
        mreteirq = false;
    }
    update();
}

void
Xh3irq::trapExit()
{
    if (mreteirq) {
        // Pop the preemption priority stack.
        preempt = ppreempt & priorityMask;
        ppreempt = pppreempt & priorityMask;
        pppreempt = 0;
    }
    mreteirq = false;
    update();
}

void
Xh3irq::setLevel(uint32_t irq, bool high)
{
    panic_if(irq >= _numIrqs, "Xh3irq: no interrupt %u", irq);
    panic_if(!high && highLines[irq] == 0, "Xh3irq: interrupt %u lowered more than raised", irq);
    highLines[irq] += high ? 1 : -1;
    level[irq] = highLines[irq] != 0;
    update();
}

void
Xh3irq::update()
{
    // mip.meip: an interrupt pending and enabled at or above
    // meicontext.preempt.
    bool any = false;
    for (uint32_t i = 0; i < _numIrqs && !any; ++i)
        any = enable[i] && pending(i) && priority[i] >= preempt;
    if (any == meip)
        return;
    meip = any;
    ThreadContext *tc = thread();
    if (meip)
        tc->getCpuPtr()->postInterrupt(tc->threadId(), INT_EXT_MACHINE, 0);
    else
        tc->getCpuPtr()->clearInterrupt(tc->threadId(), INT_EXT_MACHINE, 0);
}

void
Xh3irq::serialize(CheckpointOut &cp) const
{
    const std::vector<uint8_t> enable_b = toBytes(enable);
    const std::vector<uint8_t> force_b = toBytes(force);
    const std::vector<uint8_t> level_b = toBytes(level);
    SERIALIZE_CONTAINER(enable_b);
    SERIALIZE_CONTAINER(force_b);
    SERIALIZE_CONTAINER(level_b);
    SERIALIZE_CONTAINER(highLines);
    SERIALIZE_CONTAINER(priority);
    SERIALIZE_SCALAR(pppreempt);
    SERIALIZE_SCALAR(ppreempt);
    SERIALIZE_SCALAR(preempt);
    SERIALIZE_SCALAR(contextNoirq);
    SERIALIZE_SCALAR(contextIrq);
    SERIALIZE_SCALAR(mreteirq);
    SERIALIZE_SCALAR(meip);
}

void
Xh3irq::unserialize(CheckpointIn &cp)
{
    std::vector<uint8_t> enable_b, force_b, level_b;
    UNSERIALIZE_CONTAINER(enable_b);
    UNSERIALIZE_CONTAINER(force_b);
    UNSERIALIZE_CONTAINER(level_b);
    fromBytes(enable, enable_b);
    fromBytes(force, force_b);
    fromBytes(level, level_b);
    UNSERIALIZE_CONTAINER(highLines);
    UNSERIALIZE_CONTAINER(priority);
    UNSERIALIZE_SCALAR(pppreempt);
    UNSERIALIZE_SCALAR(ppreempt);
    UNSERIALIZE_SCALAR(preempt);
    UNSERIALIZE_SCALAR(contextNoirq);
    UNSERIALIZE_SCALAR(contextIrq);
    UNSERIALIZE_SCALAR(mreteirq);
    UNSERIALIZE_SCALAR(meip);
}

Xh3irqLine::Xh3irqLine(const Params &p)
    : ClockedObject(p), controller(p.controller), irqNum(p.irq_num),
      delay(p.delay), pin(name() + ".pin", 0, this),
      deliverEvent([this] { deliver(); }, name() + ".deliver")
{
}

void
Xh3irqLine::edge(bool high)
{
    if (delay == 0) {
        apply(high);
        return;
    }
    // The first register takes the level at the next rising edge after now;
    // each further register one clock later.
    Tick when = clockEdge();
    if (when == curTick())
        when = clockEdge(Cycles(1));
    when += (uint64_t(delay) - 1) * clockPeriod();
    inFlight.emplace_back(when, high);
    if (!deliverEvent.scheduled())
        schedule(deliverEvent, when);
}

void
Xh3irqLine::deliver()
{
    while (!inFlight.empty() && inFlight.front().first <= curTick()) {
        apply(inFlight.front().second);
        inFlight.pop_front();
    }
    if (!inFlight.empty())
        schedule(deliverEvent, inFlight.front().first);
}

// A device may raise a raised pin again; the controller hears only changes.
void
Xh3irqLine::apply(bool high)
{
    if (high == delivered)
        return;
    delivered = high;
    controller->setLevel(irqNum, high);
}

Port &
Xh3irqLine::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "pin")
        return pin;
    return ClockedObject::getPort(if_name, idx);
}

void
Xh3irqLine::init()
{
    ClockedObject::init();
    fatal_if(irqNum >= controller->numIrqs(),
             "Xh3irqLine: irq_num=%u is out of range (num_irqs=%u)",
             irqNum, controller->numIrqs());
}

void
Xh3irqLine::raiseInterruptPin(int number)
{
    edge(true);
}

void
Xh3irqLine::lowerInterruptPin(int number)
{
    edge(false);
}

} // namespace RiscvISA
} // namespace gem5
