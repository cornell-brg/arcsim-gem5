#include "dev/hwpe/hci_mux.hh"

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/HCIMux.hh"
#include "sim/serialize.hh"

namespace gem5
{

HCIMux::InPort::InPort(const std::string &name, HCIMux &_mux,
                       unsigned _channel)
    : ResponsePort(name), channel(_channel), mux(_mux)
{}

Tick
HCIMux::InPort::recvAtomic(PacketPtr pkt)
{
    return mux.outPort.sendAtomic(pkt);
}

void
HCIMux::InPort::recvFunctional(PacketPtr pkt)
{
    mux.outPort.sendFunctional(pkt);
}

bool
HCIMux::InPort::recvTimingReq(PacketPtr pkt)
{
    return mux.receive(*this, pkt);
}

void
HCIMux::InPort::recvRespRetry()
{
    panic("%s: an HWPE refused a response", name());
}

AddrRangeList
HCIMux::InPort::getAddrRanges() const
{
    return mux.outPort.getAddrRanges();
}

HCIMux::OutPort::OutPort(const std::string &name, HCIMux &_mux)
    : RequestPort(name), mux(_mux)
{}

bool
HCIMux::OutPort::recvTimingResp(PacketPtr pkt)
{
    auto *state = safe_cast<Channel *>(pkt->popSenderState());
    const unsigned channel = state->channel;
    delete state;
    panic_if(!mux.outstanding, "%s: a response with none outstanding",
             name());
    --mux.outstanding;
    const bool taken = mux.inPorts[channel]->sendTimingResp(pkt);
    panic_if(!taken, "%s: HWPE %d refused a response", name(), channel);
    if (mux.drainState() == DrainState::Draining &&
        mux.drain() == DrainState::Drained)
        mux.signalDrainDone();
    return true;
}

void
HCIMux::OutPort::recvReqRetry()
{
    PacketPtr pkt = mux.refused;
    if (!pkt)
        return;
    mux.refused = nullptr;
    if (sendTimingReq(pkt))
        mux.accepted(mux.refusedChannel, mux.sentNeedsResponse);
    else
        mux.refused = pkt;
}

void
HCIMux::OutPort::recvRangeChange()
{
    for (auto &port : mux.inPorts)
        port->sendRangeChange();
}

HCIMux::HCIMux(const Params &p)
    : ClockedObject(p), numChannels(p.num_channels),
      outPort(name() + ".mem_side_port", *this),
      arbitrateEvent([this] { arbitrate(); }, name() + ".arbitrate", false,
                     Event::Default_Pri + 1),
      ADD_STAT(granted, statistics::units::Count::get(),
               "Requests granted"),
      ADD_STAT(conflicts, statistics::units::Count::get(),
               "Requests that lost a cycle to another input")
{
    fatal_if(numChannels == 0 || (numChannels & (numChannels - 1)),
             "%s: num_channels must be a power of two", name());
    fatal_if(p.port_cpu_side_ports_connection_count > numChannels,
             "%s: more inputs than channels", name());
    for (unsigned c = 0; c < p.port_cpu_side_ports_connection_count; ++c)
        inPorts.emplace_back(new InPort(
            csprintf("%s.cpu_side_ports[%d]", name(), c), *this, c));
}

Port &
HCIMux::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "mem_side_port")
        return outPort;
    if (if_name == "cpu_side_ports" && idx != InvalidPortID &&
        idx < (PortID)inPorts.size())
        return *inPorts[idx];
    return ClockedObject::getPort(if_name, idx);
}

void
HCIMux::init()
{
    ClockedObject::init();
    fatal_if(!outPort.isConnected(), "%s: mem_side_port unconnected", name());
    for (auto &port : inPorts)
        port->sendRangeChange();
}

bool
HCIMux::receive(InPort &port, PacketPtr pkt)
{
    if (port.pending) {
        port.retry = true;
        return false;
    }
    port.pending = pkt;
    wake(curTick());
    return true;
}

void
HCIMux::wake(Tick when)
{
    if (!arbitrateEvent.scheduled())
        schedule(arbitrateEvent, when);
    else if (when < arbitrateEvent.when())
        reschedule(arbitrateEvent, when);
}

void
HCIMux::arbitrate()
{
    if (refused)
        return;
    InPort *chosen = nullptr;
    for (unsigned n = numChannels; n-- > 0;) {
        const unsigned index = (rrCounter + n) % numChannels;
        if (index >= inPorts.size() || !inPorts[index]->pending)
            continue;
        if (!chosen)
            chosen = inPorts[index].get();
        else
            ++conflicts;
    }
    if (!chosen)
        return;
    rrCounter = (rrCounter + 1) % numChannels;
    PacketPtr pkt = chosen->pending;
    DPRINTF(HCIMux, "channel %d: %s %#x\n", chosen->channel, pkt->cmdString(),
            pkt->getAddr());
    sentNeedsResponse = pkt->needsResponse();
    if (sentNeedsResponse)
        pkt->pushSenderState(new Channel(chosen->channel));
    if (outPort.sendTimingReq(pkt)) {
        accepted(chosen->channel, sentNeedsResponse);
    } else {
        refused = pkt;
        refusedChannel = chosen->channel;
    }
}

void
HCIMux::accepted(unsigned channel, bool needs_response)
{
    InPort &port = *inPorts[channel];
    if (needs_response)
        ++outstanding;
    port.pending = nullptr;
    ++granted;
    if (port.retry) {
        port.retry = false;
        port.sendRetryReq();
    }
    for (const auto &p : inPorts) {
        if (p->pending) {
            wake(clockEdge(Cycles(1)));
            break;
        }
    }
}

DrainState
HCIMux::drain()
{
    if (refused || outstanding)
        return DrainState::Draining;
    for (const auto &port : inPorts)
        if (port->pending)
            return DrainState::Draining;
    return DrainState::Drained;
}

void
HCIMux::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(rrCounter);
}

void
HCIMux::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(rrCounter);
}

} // namespace gem5
