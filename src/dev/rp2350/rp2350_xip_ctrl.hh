#ifndef __DEV_RP2350_RP2350_XIP_CTRL_HH__
#define __DEV_RP2350_RP2350_XIP_CTRL_HH__

#include "dev/io_device.hh"
#include "params/RP2350XipCtrl.hh"

namespace gem5
{

// XIP_CTRL's cache counters (see RP2350XipCtrl.py).
class RP2350XipCtrl : public BasicPioDevice
{
  public:
    PARAMS(RP2350XipCtrl);
    RP2350XipCtrl(const Params &p)
        : BasicPioDevice(p, p.pio_size)
    {}

  protected:
    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;
};

} // namespace gem5

#endif // __DEV_RP2350_RP2350_XIP_CTRL_HH__
