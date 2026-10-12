#ifndef __DEV_ARM_M_PROFILE_IRQ_LINE_HH__
#define __DEV_ARM_M_PROFILE_IRQ_LINE_HH__

#include "dev/arm/m_profile_scs.hh"
#include "dev/intpin.hh"
#include "params/MProfileIrqLine.hh"
#include "sim/sim_object.hh"

namespace gem5
{

// An external interrupt line of the M-profile NVIC as an interrupt sink
// pin (see MProfileIrqLine.py).
class MProfileIrqLine : public SimObject
{
  public:
    PARAMS(MProfileIrqLine);
    MProfileIrqLine(const Params &p);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
    void init() override;

    // The pin's two edges
    void raiseInterruptPin(int number);
    void lowerInterruptPin(int number);

  private:
    MProfileSCS *scs;
    const uint32_t irqNum;
    IntSinkPin<MProfileIrqLine> pin;
};

} // namespace gem5

#endif // __DEV_ARM_M_PROFILE_IRQ_LINE_HH__
