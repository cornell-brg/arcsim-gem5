#ifndef __DEV_ARM_RP2350_COPROCESSORS_HH__
#define __DEV_ARM_RP2350_COPROCESSORS_HH__

#include "dev/arm/m_profile_coprocessor.hh"
#include "params/RP2350DcpCoprocessor.hh"
#include "params/RP2350GpioCoprocessor.hh"

namespace gem5
{

class RP2350SIO;

// The RP2350 GPIO coprocessor (p0) on an RP2350SIO's GPIO state.
class RP2350GpioCoprocessor : public MProfileCoprocessor
{
  public:
    PARAMS(RP2350GpioCoprocessor);
    RP2350GpioCoprocessor(const Params &p);

    bool access(MProfileCoprocAccess &a) override;

  private:
    RP2350SIO *sio;
};

// The RP2350 DCP (p4, p5): only the RCMP status read.
class RP2350DcpCoprocessor : public MProfileCoprocessor
{
  public:
    PARAMS(RP2350DcpCoprocessor);
    RP2350DcpCoprocessor(const Params &p) : MProfileCoprocessor(p) {}

    bool access(MProfileCoprocAccess &a) override;
};

} // namespace gem5

#endif // __DEV_ARM_RP2350_COPROCESSORS_HH__
