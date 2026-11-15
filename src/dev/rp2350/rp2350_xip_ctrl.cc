#include "dev/rp2350/rp2350_xip_ctrl.hh"

#include "mem/packet_access.hh"

namespace gem5
{

Tick
RP2350XipCtrl::read(PacketPtr pkt)
{
    const Addr offset = pkt->getAddr() - pioAddr;
    uint32_t value = 0;
    if (xip && (offset & ~Addr(3)) == CTR_HIT)
        value = xip->counterHit();
    else if (xip && (offset & ~Addr(3)) == CTR_ACC)
        value = xip->counterAcc();
    pkt->setUintX(value >> 8 * (offset & 3), ByteOrder::little);
    pkt->makeResponse();
    return pioDelay;
}

Tick
RP2350XipCtrl::write(PacketPtr pkt)
{
    const Addr offset = pkt->getAddr() - pioAddr;
    if (xip && (offset & ~Addr(3)) == CTR_HIT)
        xip->clearCounterHit();
    else if (xip && (offset & ~Addr(3)) == CTR_ACC)
        xip->clearCounterAcc();
    pkt->makeResponse();
    return pioDelay;
}

} // namespace gem5
