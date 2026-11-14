#include "arch/riscv/int_line.hh"

#include "cpu/base.hh"
#include "cpu/thread_context.hh"
#include "sim/system.hh"

namespace gem5
{

namespace RiscvISA
{

RiscvIntLine::RiscvIntLine(const Params &p)
    : ClockedObject(p), system(p.system), hartId(p.hart_id),
      intNum(p.int_num), delay(p.delay), pin(name() + ".pin", 0, this),
      deliverEvent([this] { deliver(); }, name() + ".deliver")
{
}

Port &
RiscvIntLine::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "pin")
        return pin;
    return ClockedObject::getPort(if_name, idx);
}

void
RiscvIntLine::set(bool high)
{
    ThreadContext *tc = system->threads[hartId];
    if (high)
        tc->getCpuPtr()->postInterrupt(tc->threadId(), intNum, 0);
    else
        tc->getCpuPtr()->clearInterrupt(tc->threadId(), intNum, 0);
}

void
RiscvIntLine::edge(bool high)
{
    if (delay == 0) {
        set(high);
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
RiscvIntLine::deliver()
{
    while (!inFlight.empty() && inFlight.front().first <= curTick()) {
        set(inFlight.front().second);
        inFlight.pop_front();
    }
    if (!inFlight.empty())
        schedule(deliverEvent, inFlight.front().first);
}

void
RiscvIntLine::raiseInterruptPin(int number)
{
    edge(true);
}

void
RiscvIntLine::lowerInterruptPin(int number)
{
    edge(false);
}

} // namespace RiscvISA
} // namespace gem5
