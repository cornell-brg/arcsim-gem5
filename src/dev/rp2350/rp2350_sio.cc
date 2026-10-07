#include "dev/rp2350/rp2350_sio.hh"

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/RP2350SIO.hh"
#include "mem/packet_access.hh"
#include "sim/serialize.hh"

namespace gem5
{

RP2350SIO::RP2350SIO(const Params &p)
    : BasicPioDevice(p, 0x10000)
{
}

uint32_t
RP2350SIO::gpioRead(GpioReg reg) const
{
    switch (reg) {
      case GpioReg::OutLo: return out[0];
      case GpioReg::OutHi: return out[1];
      case GpioReg::OeLo: return oe[0];
      case GpioReg::OeHi: return oe[1];
      // No pads: an enabled output reads back what it drives.
      case GpioReg::InLo: return out[0] & oe[0];
      case GpioReg::InHi: return out[1] & oe[1];
    }
    panic("RP2350SIO: bad GPIO register");
}

void
RP2350SIO::gpioWrite(GpioReg reg, GpioOp op, uint32_t value)
{
    uint32_t *r;
    uint32_t mask;
    switch (reg) {
      case GpioReg::OutLo: r = &out[0]; mask = LO_MASK; break;
      case GpioReg::OutHi: r = &out[1]; mask = HI_MASK; break;
      case GpioReg::OeLo: r = &oe[0]; mask = LO_MASK; break;
      case GpioReg::OeHi: r = &oe[1]; mask = HI_MASK; break;
      default:
        return;  // GPIO_IN is read-only
    }
    switch (op) {
      case GpioOp::Put: *r = value; break;
      case GpioOp::Set: *r |= value; break;
      case GpioOp::Clr: *r &= ~value; break;
      case GpioOp::Xor: *r ^= value; break;
    }
    *r &= mask;
    DPRINTF(RP2350SIO, "GPIO out %#010x %#010x oe %#010x %#010x\n",
            out[0], out[1], oe[0], oe[1]);
}

void
RP2350SIO::unmodelled(Addr offset, bool isWrite)
{
    if (warned.insert(offset).second)
        warn("RP2350SIO: register %#x is not modelled (%s); it reads 0 and "
             "ignores writes\n", offset, isWrite ? "write" : "read");
}

Tick
RP2350SIO::read(PacketPtr pkt)
{
    // Registers are 32-bit; a narrow read returns its byte lane.
    const Addr byte = pkt->getAddr() - pioAddr;
    const Addr offset = byte & ~Addr(3);
    const unsigned size = pkt->getSize();
    panic_if(size != 1 && size != 2 && size != 4,
             "RP2350SIO: %d-byte read at %#x", size, byte);

    uint32_t data = 0;
    switch (offset) {
      case CPUID:
        // The core number: the reading core's context, 0 on one core.
        data = pkt->req->hasContextId() ? pkt->req->contextId() : 0;
        break;
      case GPIO_IN: data = gpioRead(GpioReg::InLo); break;
      case GPIO_HI_IN: data = gpioRead(GpioReg::InHi); break;
      case GPIO_OUT: data = out[0]; break;
      case GPIO_HI_OUT: data = out[1]; break;
      case GPIO_OE: data = oe[0]; break;
      case GPIO_HI_OE: data = oe[1]; break;
      default:
        // Including reads of the SET, CLR and XOR aliases.
        unmodelled(offset, false);
    }
    data >>= 8 * (byte & 3);
    if (size == 1)
        pkt->setLE<uint8_t>(data);
    else if (size == 2)
        pkt->setLE<uint16_t>(data);
    else
        pkt->setLE<uint32_t>(data);
    pkt->makeAtomicResponse();
    return pioDelay;
}

Tick
RP2350SIO::write(PacketPtr pkt)
{
    // A narrow write updates the whole register, with the value replicated
    // across the 32-bit bus (RP2350 datasheet 2.1.5).
    const Addr byte = pkt->getAddr() - pioAddr;
    const Addr offset = byte & ~Addr(3);
    uint32_t data;
    switch (pkt->getSize()) {
      case 1: data = pkt->getLE<uint8_t>() * 0x01010101u; break;
      case 2: data = pkt->getLE<uint16_t>() * 0x00010001u; break;
      case 4: data = pkt->getLE<uint32_t>(); break;
      default:
        panic("RP2350SIO: %d-byte write at %#x", pkt->getSize(), byte);
    }

    if (offset >= GPIO_OUT && offset <= GPIO_LAST) {
        // 0x10-0x2C: OUT, 0x30-0x4C: OE; within each, plain, SET, CLR, XOR
        // every 8 bytes, lo then hi bank.
        const bool isOe = offset >= GPIO_OE;
        const Addr rel = offset - (isOe ? GPIO_OE : GPIO_OUT);
        const bool hi = rel & 0x4;
        static const GpioOp ops[] = {
            GpioOp::Put, GpioOp::Set, GpioOp::Clr, GpioOp::Xor};
        const GpioReg reg = isOe ? (hi ? GpioReg::OeHi : GpioReg::OeLo)
                                 : (hi ? GpioReg::OutHi : GpioReg::OutLo);
        gpioWrite(reg, ops[rel >> 3], data);
    } else if (offset != CPUID && offset != GPIO_IN && offset != GPIO_HI_IN) {
        unmodelled(offset, true);
    }
    pkt->makeAtomicResponse();
    return pioDelay;
}

void
RP2350SIO::serialize(CheckpointOut &cp) const
{
    SERIALIZE_ARRAY(out, 2);
    SERIALIZE_ARRAY(oe, 2);
}

void
RP2350SIO::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_ARRAY(out, 2);
    UNSERIALIZE_ARRAY(oe, 2);
}

} // namespace gem5
