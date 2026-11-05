#ifndef __DEV_RP2350_RP2350_XIP_CTRL_HH__
#define __DEV_RP2350_RP2350_XIP_CTRL_HH__

#include "dev/io_device.hh"
#include "mem/rp2350_xip.hh"
#include "params/RP2350XipCtrl.hh"

namespace gem5
{

// XIP_CTRL's cache counters (see RP2350XipCtrl.py).
class RP2350XipCtrl : public BasicPioDevice
{
  public:
    PARAMS(RP2350XipCtrl);
    RP2350XipCtrl(const Params &p)
        : BasicPioDevice(p, p.pio_size), xip(p.xip)
    {}

  protected:
    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;

  private:
    enum : Addr { CTR_HIT = 0x0c, CTR_ACC = 0x10 };
    memory::RP2350XIP *xip;
};

} // namespace gem5

#endif // __DEV_RP2350_RP2350_XIP_CTRL_HH__
