#ifndef __DEV_RP2350_HAZARD3_TB_IO_HH__
#define __DEV_RP2350_HAZARD3_TB_IO_HH__

#include <cstdint>
#include <memory>
#include <vector>

#include "dev/intpin.hh"
#include "dev/io_device.hh"
#include "params/Hazard3TbIO.hh"
#include "sim/eventq.hh"

namespace gem5
{

// The Hazard3 testbench IO block (see Hazard3TbIO.py): print and exit
// registers, the IRQ and soft IRQ registers and the timer, so firmware built
// for rtl-sim runs unchanged.
class Hazard3TbIO : public BasicPioDevice
{
  public:
    PARAMS(Hazard3TbIO);
    Hazard3TbIO(const Params &p);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
    void startup() override;

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

    // The IRQ and soft IRQ states (SET_ and CLR_ registers), as rtl-sim.
    uint32_t irqState = 0;
    uint32_t softIrqState = 0;

    // mtime is this device's cycle count plus mtimeOffset, so a write to
    // MTIME or MTIMEH moves the offset.
    uint64_t mtimeOffset = 0;
    uint64_t mtimecmp[2] = {0, 0};

    std::vector<std::unique_ptr<IntSourcePin<Hazard3TbIO>>> irqPins;
    std::unique_ptr<IntSourcePin<Hazard3TbIO>> softIrqPin;
    std::unique_ptr<IntSourcePin<Hazard3TbIO>> timerIrqPin;
    // The pins' levels as last driven, so each edge is sent once.
    uint32_t irqDriven = 0;
    bool softIrqDriven = false;
    bool timerIrqDriven = false;

    // When mtime reaches mtimecmp0.
    EventFunctionWrapper timerEvent;

    uint64_t mtime() const;
    void setMtime(uint64_t value);
    // Drive the IRQ and soft IRQ pins from their states.
    void driveIrqs();
    // Drive timer_irq from mtime and mtimecmp0, and schedule timerEvent for
    // when it next rises.
    void updateTimer();

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
