#include "dev/hwpe/hci_tcdm.hh"

#include <algorithm>

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/HCITcdm.hh"
#include "sim/serialize.hh"

namespace gem5
{

// ---- ports ------------------------------------------------------------

HCITcdm::TcdmPort::TcdmPort(const std::string &name, HCITcdm &_tcdm,
                            bool _hwpe)
    : QueuedResponsePort(name, queue), hwpe(_hwpe), tcdm(_tcdm),
      queue(_tcdm, *this)
{
}

AddrRangeList
HCITcdm::TcdmPort::getAddrRanges() const
{
    return {tcdm.getAddrRange()};
}

Tick
HCITcdm::TcdmPort::recvAtomic(PacketPtr pkt)
{
    // Atomic mode has no arbitration
    tcdm.validate(pkt, hwpe);
    tcdm.access(pkt);
    return tcdm.latency;
}

void
HCITcdm::TcdmPort::recvFunctional(PacketPtr pkt)
{
    tcdm.functionalAccess(pkt);
}

bool
HCITcdm::TcdmPort::recvTimingReq(PacketPtr pkt)
{
    return tcdm.receive(*this, pkt);
}

// ---- construction -----------------------------------------------------

HCITcdm::TcdmStats::TcdmStats(HCITcdm &tcdm)
    : statistics::Group(&tcdm, "tcdm"),
      ADD_STAT(coreGrants, statistics::units::Count::get(),
               "Core-side accesses granted"),
      ADD_STAT(hwpeGrants, statistics::units::Count::get(),
               "HWPE accesses granted"),
      ADD_STAT(coreWaits, statistics::units::Count::get(),
               "Cycles a core-side access waited for its grant"),
      ADD_STAT(guardCycles, statistics::units::Count::get(),
               "Cycles the starvation guard masked the core side"),
      ADD_STAT(hwpeConflicts, statistics::units::Count::get(),
               "Cycles the HWPE's access waited for a bank the core "
               "side took"),
      ADD_STAT(refusals, statistics::units::Count::get(),
               "Requests refused while the port's last one waited")
{
}

HCITcdm::HCITcdm(const Params &p)
    : memory::AbstractMemory(p),
      hwpePort(name() + ".hwpe_port", *this, true),
      numBanks(p.num_banks),
      numBridges(p.num_bridges),
      hwpeWords(p.hwpe_words),
      lowPrioMaxStall(p.low_prio_max_stall),
      window(p.window),
      latency(p.latency),
      arbitrateEvent([this] { arbitrate(); }, name() + ".arbitrate", false,
                     Event::CPU_Tick_Pri + 1),
      tcdmStats(*this)
{
    fatal_if(numBanks < 1 || hwpeWords < 1 || hwpeWords > numBanks,
             "%s: %d HWPE words on %d banks", name(), hwpeWords, numBanks);
    fatal_if(numBridges < 1 || numBanks % numBridges,
             "%s: %d bridges for %d banks", name(), numBridges, numBanks);
    fatal_if(range.start() % (4 * numBanks) || range.interleaved(),
             "%s: the range must start at a bank 0 word", name());
    for (unsigned n = 0; n < p.port_core_port_connection_count; ++n) {
        corePorts.emplace_back(std::make_unique<TcdmPort>(
            csprintf("%s.core_port[%d]", name(), n), *this, false));
    }
}

Port &
HCITcdm::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "core_port") {
        fatal_if(idx < 0 || size_t(idx) >= corePorts.size(),
                 "%s: no core_port[%d]", name(), idx);
        return *corePorts[idx];
    }
    if (if_name == "hwpe_port")
        return hwpePort;
    return memory::AbstractMemory::getPort(if_name, idx);
}

void
HCITcdm::init()
{
    memory::AbstractMemory::init();
    // A write is answered in the cycle it is granted, after the grant
    fatal_if(window >= latency || latency >= clockPeriod(),
             "%s: needs window < latency < a clock period", name());
    for (auto &port : corePorts)
        port->sendRangeChange();
    if (hwpePort.isConnected())
        hwpePort.sendRangeChange();
}

// ---- requests ---------------------------------------------------------

unsigned
HCITcdm::bankOf(Addr addr) const
{
    return ((addr - range.start()) / 4) % numBanks;
}

void
HCITcdm::validate(PacketPtr pkt, bool hwpe) const
{
    panic_if(pkt->getSize() == 0 || !pkt->getAddrRange().isSubset(range),
             "%s: access outside the TCDM: %s", name(), pkt->print());
    panic_if(!pkt->isRead() && !pkt->isWrite(),
             "%s: unsupported command: %s", name(), pkt->print());
    // Within the words the access's banks hold. Core-side accesses are
    // made a word at a time.
    panic_if(hwpe && (pkt->getAddr() & 3) + pkt->getSize() > 4 * hwpeWords,
             "%s: an HWPE access wider than %d words: %s", name(),
             hwpeWords, pkt->print());
}

Tick
HCITcdm::cycleStart() const
{
    const Tick edge = clockEdge();
    return edge == curTick() ? edge : edge - clockPeriod();
}

void
HCITcdm::arbitrateAt(Tick when)
{
    if (!arbitrateEvent.scheduled())
        schedule(arbitrateEvent, when);
    else if (when < arbitrateEvent.when())
        reschedule(arbitrateEvent, when);
}

bool
HCITcdm::receive(TcdmPort &port, PacketPtr pkt)
{
    validate(pkt, port.hwpe);
    pendingDelete.reset();
    if (port.pending) {
        port.retry = true;
        ++tcdmStats.refusals;
        return false;
    }
    DPRINTF(HCITcdm, "%s: %s %d bytes at %#x\n", port.name(),
            pkt->isRead() ? "read" : "write", pkt->getSize(),
            pkt->getAddr());
    port.pending = pkt;
    port.granted = 0;
    // It competes in this cycle if it came within the window and the
    // cycle is not arbitrated yet, else in the next
    const Tick cycle = cycleStart();
    if (curTick() <= cycle + window && lastCycle != cycle)
        port.competes = cycle;
    else
        port.competes = cycle + clockPeriod();
    arbitrateAt(port.competes + window);
    return true;
}

void
HCITcdm::arbitrate()
{
    const Tick cycle = cycleStart();
    // The guard counts consecutive cycles: one without requests, so not
    // arbitrated, resets it
    if (lastCycle == MaxTick || cycle != lastCycle + clockPeriod())
        stallCount = 0;
    lastCycle = cycle;

    // A port requests if its request (or next beat) competes in this cycle
    auto requesting = [cycle](const TcdmPort &port) {
        return port.pending && port.competes <= cycle;
    };
    bool core_request = false;
    for (const auto &port : corePorts)
        core_request = core_request || requesting(*port);
    const bool hwpe_request = requesting(hwpePort);
    // The starvation guard masks the core side for this cycle
    const bool core_side = core_request &&
        !(lowPrioMaxStall && stallCount >= lowPrioMaxStall);
    if (core_side && hwpe_request)
        ++stallCount;
    else
        stallCount = 0;
    if (core_request && !core_side) {
        DPRINTF(HCITcdm, "Starvation guard: the core side waits\n");
        ++tcdmStats.guardCycles;
    }

    // The core side first: each beat takes its bank, one beat per bridge,
    // the lower port first
    std::vector<bool> taken(numBanks, false);
    std::vector<bool> bridge_taken(numBridges, false);
    for (auto &port : corePorts) {
        if (!requesting(*port))
            continue;
        PacketPtr pkt = port->pending;
        const Addr addr = pkt->getAddr() + port->granted;
        const unsigned bank = bankOf(addr);
        const unsigned bridge = bank % numBridges;
        if (!core_side || bridge_taken[bridge]) {
            ++tcdmStats.coreWaits;
            continue;
        }
        bridge_taken[bridge] = true;
        taken[bank] = true;
        ++tcdmStats.coreGrants;
        port->granted += std::min<unsigned>(4 - (addr & 3),
                                            pkt->getSize() - port->granted);
        if (port->granted < pkt->getSize()) {
            // The next beat's address phase waits for this beat's data
            // phase: two cycles after a read (its wait state), one after
            // a write
            port->competes = cycle + (pkt->isRead() ? 2 : 1) * clockPeriod();
            continue;
        }
        // A read's data comes a cycle later: the bridge's wait state
        grant(*port, pkt->isRead() ? 1 : 0);
    }

    // The HWPE gets all of its banks or none
    if (hwpe_request) {
        const unsigned first = bankOf(hwpePort.pending->getAddr());
        bool free = true;
        for (unsigned n = 0; n < hwpeWords; ++n)
            free = free && !taken[(first + n) % numBanks];
        if (free) {
            ++tcdmStats.hwpeGrants;
            grant(hwpePort, 1);
        } else {
            DPRINTF(HCITcdm, "HWPE waits for a bank the core side took\n");
            ++tcdmStats.hwpeConflicts;
        }
    }

    if (!idle())
        arbitrateAt(cycle + clockPeriod() + window);
    else if (drainState() == DrainState::Draining)
        signalDrainDone();
}

void
HCITcdm::grant(TcdmPort &port, unsigned delay)
{
    PacketPtr pkt = port.pending;
    port.pending = nullptr;
    DPRINTF(HCITcdm, "%s: granted %#x\n", port.name(), pkt->getAddr());
    const bool needs_response = pkt->needsResponse();
    // The delays the packet picked up on its way do not move its answer
    pkt->headerDelay = pkt->payloadDelay = 0;
    access(pkt);
    if (needs_response) {
        port.schedTimingResp(pkt,
                             cycleStart() + delay * clockPeriod() + latency);
    } else {
        pendingDelete.reset(pkt);
    }
    if (port.retry) {
        port.retry = false;
        port.sendRetryReq();
    }
}

bool
HCITcdm::idle() const
{
    for (const auto &port : corePorts) {
        if (port->pending)
            return false;
    }
    return hwpePort.pending == nullptr;
}

// ---- checkpoints ------------------------------------------------------

DrainState
HCITcdm::drain()
{
    // The response queues drain themselves
    return idle() ? DrainState::Drained : DrainState::Draining;
}

void
HCITcdm::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(stallCount);
    SERIALIZE_SCALAR(lastCycle);
}

void
HCITcdm::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(stallCount);
    UNSERIALIZE_SCALAR(lastCycle);
}

} // namespace gem5
