#include "dev/hwpe/hwpe_select.hh"

#include "mem/packet_access.hh"
#include "sim/serialize.hh"

namespace gem5
{

Tick
HWPESelect::read(PacketPtr pkt)
{
    const Addr offset = pkt->getAddr() - pioAddr;
    const uint32_t value = offset == 0 ? select : 0;
    pkt->setUintX(value >> 8 * (offset & 3), ByteOrder::little);
    pkt->makeResponse();
    return pioDelay;
}

Tick
HWPESelect::write(PacketPtr pkt)
{
    if (pkt->getAddr() == pioAddr)
        select = pkt->getUintX(ByteOrder::little) & 3;
    pkt->makeResponse();
    return pioDelay;
}

void
HWPESelect::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(select);
}

void
HWPESelect::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(select);
}

} // namespace gem5
