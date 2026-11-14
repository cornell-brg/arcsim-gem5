#include "dev/arm/m_profile_irq_line.hh"

#include "base/logging.hh"

namespace gem5
{

MProfileIrqLine::MProfileIrqLine(const Params &p)
    : SimObject(p), scs(p.scs), irqNum(p.irq_num),
      pin(name() + ".pin", 0, this)
{
}

Port &
MProfileIrqLine::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "pin")
        return pin;
    return SimObject::getPort(if_name, idx);
}

void
MProfileIrqLine::init()
{
    SimObject::init();
    fatal_if(irqNum >= scs->params().num_irqs,
             "MProfileIrqLine: irq_num=%u is out of range (SCS num_irqs=%u)",
             irqNum, (unsigned)scs->params().num_irqs);
}

void
MProfileIrqLine::raiseInterruptPin(int number)
{
    scs->setIrqLevel(irqNum, true);
}

void
MProfileIrqLine::lowerInterruptPin(int number)
{
    scs->setIrqLevel(irqNum, false);
}

} // namespace gem5
