#include "mem/rp2350_sram.hh"

#include <algorithm>

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/RP2350SRAM.hh"
#include "mem/packet.hh"

namespace gem5::memory
{

RP2350SRAM::SRAMStats::SRAMStats(RP2350SRAM &memory)
    : statistics::Group(&memory, "banks"),
      ADD_STAT(grants, statistics::units::Count::get(), "32-bit bank grants"),
      ADD_STAT(contested, statistics::units::Count::get(),
               "Cycles with multiple managers requesting the bank"),
      ADD_STAT(waitCycles, statistics::units::Cycle::get(),
               "Sum of manager cycles waiting for this bank"),
      ADD_STAT(instructionGrants, statistics::units::Count::get(),
               "Instruction-fetch bank grants"),
      ADD_STAT(requestRetries, statistics::units::Count::get(),
               "Requests rejected by queue capacity"),
      ADD_STAT(responseRetries, statistics::units::Count::get(),
               "Responses rejected by upstream"),
      ADD_STAT(splitPackets, statistics::units::Count::get(),
               "Packets spanning multiple 32-bit transfers")
{
    for (auto *stat : {&grants, &contested, &waitCycles, &instructionGrants}) {
        stat->init(10).flags(statistics::total);
        for (unsigned bank = 0; bank < 10; ++bank)
            stat->subname(bank, "sram" + std::to_string(bank));
    }
}

RP2350SRAM::MemoryPort::MemoryPort(const std::string &name,
        RP2350SRAM &mem, PortID id, unsigned prio)
    : ResponsePort(name, id), memory(mem), priority(prio)
{}

AddrRangeList
RP2350SRAM::MemoryPort::getAddrRanges() const
{
    return {memory.getAddrRange()};
}

Tick
RP2350SRAM::MemoryPort::recvAtomic(PacketPtr pkt)
{
    memory.validate(pkt);
    memory.access(pkt);
    // Atomic mode has no inter-manager timing arbitration.
    return memory.latency;
}

void
RP2350SRAM::MemoryPort::recvFunctional(PacketPtr pkt)
{
    memory.functional(pkt);
}

bool
RP2350SRAM::MemoryPort::recvTimingReq(PacketPtr pkt)
{
    return memory.receive(pkt, id);
}

void
RP2350SRAM::MemoryPort::recvRespRetry()
{
    panic_if(!retryResp, "Unexpected SRAM response retry");
    retryResp = false;
    memory.sendResponses(*this);
    if (memory.idle() && memory.drainState() == DrainState::Draining)
        memory.signalDrainDone();
}

RP2350SRAM::RP2350SRAM(const RP2350SRAMParams &p)
    : AbstractMemory(p), window(p.window), latency(p.latency),
      queueDepth(p.queue_depth),
      arbitrateEvent([this] { arbitrate(); }, name() + ".arbitrate", false,
                     Event::CPU_Tick_Pri + 1),
      completeEvent([this] { completeBeats(); }, name() + ".complete"),
      sramStats(*this)
{
    fatal_if(range.start() != 0x20000000 || range.size() != 520 * 1024 ||
             range.interleaved(), "RP2350SRAM requires the 520 KiB SRAM range");
    fatal_if(queueDepth == 0 || p.port_priority.empty(),
             "Invalid RP2350 SRAM queue parameters");
    lastWinner.fill(InvalidPortID);
    bankFree.fill(0);
    bankOwner.fill(InvalidPortID);
    for (unsigned id = 0; id < p.port_port_connection_count; ++id) {
        unsigned priority = p.port_priority[
            std::min<size_t>(id, p.port_priority.size() - 1)];
        fatal_if(priority > 1, "SRAM manager priority must be 0 or 1");
        ports.emplace_back(std::make_unique<MemoryPort>(
            name() + ".port[" + std::to_string(id) + "]", *this, id, priority));
    }
}

Port &
RP2350SRAM::getPort(const std::string &name, PortID id)
{
    if (name == "port") {
        fatal_if(id < 0 || static_cast<size_t>(id) >= ports.size(),
                 "Invalid SRAM port index");
        return *ports[id];
    }
    return AbstractMemory::getPort(name, id);
}

void
RP2350SRAM::init()
{
    AbstractMemory::init();
    fatal_if(window + latency >= clockPeriod(),
             "RP2350SRAM answers within its clock cycle");
    for (auto &port : ports)
        port->sendRangeChange();
}

unsigned
RP2350SRAM::bankFor(Addr address) const
{
    if (address < 0x20080000)
        return ((address - range.start()) / 0x40000) * 4 +
               ((address >> 2) & 3);
    return 8 + ((address - 0x20080000) / 0x1000);
}

unsigned
RP2350SRAM::beatSize(Addr address, unsigned remaining) const
{
    return std::min<unsigned>(4 - (address & 3), remaining);
}

void
RP2350SRAM::validate(PacketPtr pkt) const
{
    panic_if(pkt->getSize() == 0 || !pkt->getAddrRange().isSubset(range),
             "SRAM request outside physical memory: %s", pkt->print());
    panic_if(pkt->cacheResponding() || !(pkt->isRead() || pkt->isWrite()),
             "Unsupported SRAM command: %s", pkt->cmdString());
    panic_if(pkt->isMaskedWrite() &&
             pkt->req->getByteEnable().size() != pkt->getSize(),
             "SRAM byte enable mask must describe the whole packet");
    // LL/SC and read-modify-write commands must remain indivisible. The
    // RP2350 cores use word-sized atomics; never silently split an atomic.
    panic_if((pkt->isLLSC() || pkt->isAtomicOp() || pkt->cmd == MemCmd::SwapReq)
             && pkt->getSize() > beatSize(pkt->getAddr(), pkt->getSize()),
             "SRAM atomic request must fit within one aligned word");
}

void
RP2350SRAM::arbitrateAt(Tick when)
{
    // The event runs last in its tick, so arrival callback order does not
    // decide equal-priority winners.
    if (!arbitrateEvent.scheduled())
        schedule(arbitrateEvent, when);
    else if (when < arbitrateEvent.when())
        reschedule(arbitrateEvent, when);
}

bool
RP2350SRAM::receive(PacketPtr pkt, PortID id)
{
    validate(pkt);
    auto &port = *ports[id];
    if (port.retryReq)
        return false;
    if (port.outstanding == queueDepth) {
        port.retryReq = true;
        ++sramStats.requestRetries;
        return false;
    }
    auto txn = std::make_shared<Transaction>();
    txn->pkt = pkt;
    txn->port = id;
    txn->ready = curTick() + pkt->headerDelay + pkt->payloadDelay;
    txn->needsResponse = pkt->needsResponse();
    pkt->headerDelay = pkt->payloadDelay = 0;
    // As with SimpleMemory, the initiator owns and supplies the packet's
    // data buffer. Reallocating here would discard write payloads.
    if (pkt->getSize() > beatSize(pkt->getAddr(), pkt->getSize()))
        ++sramStats.splitPackets;
    ++port.outstanding;
    port.requests.push_back(txn);
    active.push_back(txn);
    DPRINTF(RP2350SRAM, "manager %d %s %#x, ready at %llu\n", id,
            pkt->cmdString(), pkt->getAddr(), txn->ready);
    // The banks choose `window` into the cycle, or at once for a request
    // that arrives later in it.
    const Tick edge = clockEdge();
    txn->cycle = edge == curTick() ? edge : edge - clockPeriod();
    arbitrateAt(std::max(curTick(), txn->cycle + window));
    return true;
}

void
RP2350SRAM::functional(PacketPtr pkt)
{
    panic_if(!pkt->getAddrRange().isSubset(range), "Functional SRAM range error");
    functionalAccess(pkt);
    // Functional writes update in-flight data, including partial read
    // responses. Functional reads overlay uncompleted writes in acceptance
    // order, honoring their byte enables instead of exposing masked bytes.
    for (const auto &txn : active) {
        PacketPtr other = txn->pkt;
        auto data = const_cast<uint8_t *>(other->getConstPtr<uint8_t>());
        if (pkt->isWrite()) {
            if (pkt->isSecure() != other->isSecure())
                continue;
            Addr start = std::max(pkt->getAddr(), other->getAddr());
            Addr end = std::min(pkt->getAddr() + pkt->getSize(),
                                other->getAddr() + other->getSize());
            const auto &enable = pkt->req->getByteEnable();
            for (Addr address = start; address < end; ++address) {
                unsigned offset = address - pkt->getAddr();
                if (enable.size() != pkt->getSize() || enable[offset])
                    data[address - other->getAddr()] =
                        pkt->getConstPtr<uint8_t>()[offset];
            }
        } else if (pkt->isRead() && other->isWrite() &&
                   !other->isLLSC() && !other->isAtomicOp() &&
                   other->cmd != MemCmd::SwapReq &&
                   txn->completed < other->getSize()) {
            const auto &enable = other->req->getByteEnable();
            for (unsigned i = txn->completed; i < other->getSize(); ++i) {
                if (enable.size() != other->getSize() || enable[i])
                    pkt->trySatisfyFunctional(other, other->getAddr() + i,
                        other->isSecure(), 1,
                        data + i);
            }
        }
    }
}

void
RP2350SRAM::retire(const TransactionPtr &txn)
{
    auto &port = *ports[txn->port];
    active.remove(txn);
    --port.outstanding;
    if (port.retryReq) {
        port.retryReq = false;
        port.sendRetryReq();
    }
}

void
RP2350SRAM::complete(const Beat &beat)
{
    auto txn = beat.txn;
    PacketPtr pkt = txn->pkt;
    if (beat.size == pkt->getSize()) {
        // Keep the original request for LL/SC, atomic extraData and flags.
        access(pkt);
    } else {
        auto request = std::make_shared<Request>(pkt->getAddr() + beat.offset,
            beat.size, pkt->req->getFlags(), pkt->req->requestorId());
        if (pkt->req->hasContextId())
            request->setContext(pkt->req->contextId());
        const auto &enable = pkt->req->getByteEnable();
        // Cache-line packets can be larger than their originating Request.
        // Only slice a mask when it describes this whole packet.
        if (enable.size() == pkt->getSize())
            request->setByteEnable(std::vector<bool>(
                enable.begin() + beat.offset,
                enable.begin() + beat.offset + beat.size));
        Packet child(request, pkt->cmd);
        child.dataStatic(
            const_cast<uint8_t *>(pkt->getConstPtr<uint8_t>()) + beat.offset);
        access(&child);
    }
    txn->completed += beat.size;
    if (txn->completed == pkt->getSize()) {
        if (txn->needsResponse) {
            if (!pkt->isResponse())
                pkt->makeResponse();
            ports[txn->port]->responses.push_back(txn);
        } else {
            pendingDelete.reset(pkt);
            retire(txn);
        }
    }
}

void
RP2350SRAM::sendResponses(MemoryPort &port)
{
    while (!port.retryResp && !port.responses.empty()) {
        auto txn = port.responses.front();
        // The receiver may delete the packet and re-enter us with a
        // functional access before sendTimingResp returns. Stop exposing
        // that packet to functional accesses during the handoff.
        active.remove(txn);
        if (!port.sendTimingResp(txn->pkt)) {
            active.push_back(txn);
            port.retryResp = true;
            ++sramStats.responseRetries;
            return;
        }
        port.responses.pop_front();
        retire(txn);
    }
}

void
RP2350SRAM::completeBeats()
{
    while (!beats.empty() && beats.front().due <= curTick()) {
        Beat beat = beats.front();
        beats.pop_front();
        complete(beat);
    }
    for (auto &port : ports)
        sendResponses(*port);
    if (!beats.empty())
        schedule(completeEvent, beats.front().due);
    if (idle() && drainState() == DrainState::Draining)
        signalDrainDone();
}

void
RP2350SRAM::arbitrate()
{
    const Tick now = curTick();
    // The clock edge after now: when a bank used in this cycle is free
    // again.
    const Tick edge = clockEdge();
    const Tick nextEdge = edge == now ? edge + clockPeriod() : edge;
    Tick again = MaxTick;

    // Each manager presents its head beat. The delays a packet has picked
    // up on its way place its answer, not its turn. A bank serves one
    // manager per cycle, chosen with strict two-level priority and round
    // robin among equal priorities. The manager a bank serves may make
    // further beats on it in the cycle they arrive in, while no other
    // manager asks for it: a core model that issues two accesses in a cycle
    // pays for that in its own timing, as on a plain memory.
    bool granted = true;
    while (granted) {
        granted = false;
        again = MaxTick;
        std::array<std::vector<PortID>, 10> asking;
        for (PortID id = 0; static_cast<size_t>(id) < ports.size(); ++id) {
            const auto &requests = ports[id]->requests;
            if (requests.empty())
                continue;
            auto txn = requests.front();
            asking[bankFor(txn->pkt->getAddr() + txn->granted)].push_back(id);
        }
        for (unsigned bank = 0; bank < 10; ++bank) {
            const auto &ready = asking[bank];
            if (ready.empty())
                continue;
            PortID winner = ready.front();
            const bool prompt =
                ports[winner]->requests.front()->cycle + clockPeriod() ==
                    nextEdge;
            if (bankFree[bank] > now) {
                // The bank has served its beat for this cycle.
                const bool own = ready.size() == 1 &&
                    winner == bankOwner[bank] && prompt;
                if (!own) {
                    again = std::min(again, bankFree[bank] + window);
                    continue;
                }
            } else {
                if (ready.size() > 1)
                    ++sramStats.contested[bank];
                auto distance = [&](PortID id) {
                    const unsigned count = ports.size();
                    const unsigned start =
                        lastWinner[bank] == InvalidPortID ? 0 :
                        (lastWinner[bank] + 1) % count;
                    return (id + count - start) % count;
                };
                for (PortID id : ready) {
                    if (ports[id]->priority > ports[winner]->priority ||
                        (ports[id]->priority == ports[winner]->priority &&
                         distance(id) < distance(winner)))
                        winner = id;
                }
                lastWinner[bank] = winner;
            }
            DPRINTF(RP2350SRAM, "bank %d to manager %d of %d asking\n",
                    bank, winner, ready.size());
            bankFree[bank] = nextEdge;
            bankOwner[bank] = winner;
            auto &port = *ports[winner];
            auto txn = port.requests.front();
            unsigned size = beatSize(txn->pkt->getAddr() + txn->granted,
                                     txn->pkt->getSize() - txn->granted);
            // Kept in the order they complete
            const Beat beat = {txn, txn->granted, size,
                               std::max(now, txn->ready) + latency};
            beats.insert(std::upper_bound(beats.begin(), beats.end(), beat,
                [](const Beat &a, const Beat &b) { return a.due < b.due; }),
                beat);
            txn->granted += size;
            ++sramStats.grants[bank];
            if (txn->pkt->req->isInstFetch())
                ++sramStats.instructionGrants[bank];
            if (txn->granted == txn->pkt->getSize())
                port.requests.pop_front();
            granted = true;
        }
    }
    // Every manager still presenting a beat waits for its bank's next cycle.
    for (const auto &port : ports) {
        if (port->requests.empty())
            continue;
        auto txn = port->requests.front();
        ++sramStats.waitCycles[bankFor(txn->pkt->getAddr() + txn->granted)];
    }
    if (!beats.empty()) {
        if (!completeEvent.scheduled())
            schedule(completeEvent, beats.front().due);
        else if (beats.front().due < completeEvent.when())
            reschedule(completeEvent, beats.front().due);
    }
    if (again != MaxTick)
        arbitrateAt(again);
}

bool
RP2350SRAM::idle() const
{
    return active.empty();
}

DrainState
RP2350SRAM::drain()
{
    return idle() ? DrainState::Drained : DrainState::Draining;
}

} // namespace gem5::memory
