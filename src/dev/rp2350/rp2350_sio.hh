#ifndef __DEV_RP2350_RP2350_SIO_HH__
#define __DEV_RP2350_RP2350_SIO_HH__

#include <cstdint>
#include <set>

#include "dev/io_device.hh"
#include "params/RP2350SIO.hh"

namespace gem5
{

// The RP2350's single-cycle I/O block: CPUID and the GPIO registers (see
// RP2350SIO.py). The GPIO state is also reachable through gpioRead() and
// gpioWrite(), which the Cortex-M33 GPIO coprocessor uses.
class RP2350SIO : public BasicPioDevice
{
  public:
    PARAMS(RP2350SIO);
    RP2350SIO(const Params &p);

    // A GPIO register: one bank (lo: GPIO 0-31, hi: GPIO 32-47 and the
    // QSPI/USB pins) of OUT, OE or IN.
    enum class GpioReg { OutLo, OutHi, OeLo, OeHi, InLo, InHi };
    // How a write combines with the register: SIO's plain, SET, CLR and XOR
    // aliases.
    enum class GpioOp { Put, Set, Clr, Xor };

    uint32_t gpioRead(GpioReg reg) const;
    void gpioWrite(GpioReg reg, GpioOp op, uint32_t value);

    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

  protected:
    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;

  private:
    // Register offsets (RP2350 datasheet, SIO register list).
    enum : Addr
    {
        CPUID = 0x000,
        GPIO_IN = 0x004,
        GPIO_HI_IN = 0x008,
        GPIO_OUT = 0x010,      // then _SET, _CLR, _XOR every 8 bytes
        GPIO_HI_OUT = 0x014,
        GPIO_OE = 0x030,
        GPIO_HI_OE = 0x034,
        GPIO_LAST = 0x04C,     // GPIO_HI_OE_XOR
    };

    // GPIO 0-31 are all present; the high bank has GPIO 32-47 in bits 15:0
    // and the QSPI/USB pins in bits 31:24.
    static constexpr uint32_t LO_MASK = 0xFFFFFFFF;
    static constexpr uint32_t HI_MASK = 0xFF00FFFF;

    uint32_t out[2] = {0, 0};
    uint32_t oe[2] = {0, 0};

    // Offsets of unmodelled registers already reported.
    std::set<Addr> warned;

    void unmodelled(Addr offset, bool isWrite);
};

} // namespace gem5

#endif // __DEV_RP2350_RP2350_SIO_HH__
