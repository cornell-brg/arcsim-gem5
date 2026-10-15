#ifndef __DEV_RP2350_HAZARD3_TB_IO_HH__
#define __DEV_RP2350_HAZARD3_TB_IO_HH__

#include <cstdint>

#include "dev/io_device.hh"
#include "params/Hazard3TbIO.hh"

namespace gem5
{

// The Hazard3 testbench IO block (see Hazard3TbIO.py): print and exit
// registers, so firmware built for rtl-sim runs unchanged.
class Hazard3TbIO : public BasicPioDevice
{
  public:
    PARAMS(Hazard3TbIO);
    Hazard3TbIO(const Params &p);

    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

  protected:
    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;

  private:
    // Register offsets (tb_cxxrtl_io.h, rtl-sim include/sim_constants.h).
    enum : Addr
    {
        PRINT_CHAR = 0x000,
        PRINT_U32 = 0x004,
        EXIT = 0x008,
        SET_SOFTIRQ = 0x010,
        CLR_SOFTIRQ = 0x014,
        SET_IRQ = 0x020,
        CLR_IRQ = 0x030,
        MTIME = 0x100,
        MTIMEH = 0x104,
        MTIMECMP0 = 0x108,
        MTIMECMP0H = 0x10C,
        MTIMECMP1 = 0x110,
        MTIMECMP1H = 0x114,
        SIZE = 0x200,
    };

    // The first EXIT write has ended the simulation; later ones are
    // ignored, as on rtl-sim.
    bool exited = false;

    // The name of the register at offset, or nullptr if there is none.
    static const char *regName(Addr offset);
    // Checks pkt is an aligned 32-bit access; fatal otherwise.
    void checkAccess(PacketPtr pkt, Addr offset) const;
    // Fatal: a register rtl-sim has but this model lacks, or no register.
    // Names the access's PC when the request carries one.
    [[noreturn]] void unmodelled(PacketPtr pkt, Addr offset) const;
};

} // namespace gem5

#endif // __DEV_RP2350_HAZARD3_TB_IO_HH__
