#ifndef __DEV_HWPE_HWPE_SELECT_HH__
#define __DEV_HWPE_HWPE_SELECT_HH__

#include <cstdint>

#include "dev/io_device.hh"
#include "params/HWPESelect.hh"

namespace gem5
{

// The optical-flow wrapper's SELECT register (see HWPE.py).
class HWPESelect : public BasicPioDevice
{
  public:
    PARAMS(HWPESelect);
    HWPESelect(const Params &p) : BasicPioDevice(p, p.pio_size) {}

    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

  protected:
    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;

  private:
    uint32_t select = 0;
};

} // namespace gem5

#endif // __DEV_HWPE_HWPE_SELECT_HH__
