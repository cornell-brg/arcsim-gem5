#include "dev/rp2350/rp2350_xip_ctrl.hh"

#include "mem/packet_access.hh"

namespace gem5
{

Tick
RP2350XipCtrl::read(PacketPtr pkt)
{
    const Addr offset = pkt->getAddr() - pioAddr;
    const uint32_t value = 0;
    pkt->setUintX(value >> 8 * (offset & 3), ByteOrder::little);
    pkt->makeResponse();
    return pioDelay;
}

Tick
RP2350XipCtrl::write(PacketPtr pkt)
{
    pkt->makeResponse();
    return pioDelay;
}

} // namespace gem5
