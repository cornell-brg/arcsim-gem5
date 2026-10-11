#include "mem/rp2350_xip.hh"

#include <algorithm>
#include <cstring>

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/RP2350XIP.hh"
#include "mem/packet.hh"

namespace gem5::memory
{

RP2350XIP::XIPStats::XIPStats(RP2350XIP &xip)
    : statistics::Group(&xip, "cache"),
      ADD_STAT(hits, statistics::units::Count::get(),
               "Line lookups that found their line"),
      ADD_STAT(misses, statistics::units::Count::get(),
               "Lines read from flash"),
      ADD_STAT(chained, statistics::units::Count::get(),
               "Lines read by continuing the last transfer"),
      ADD_STAT(bankWaits, statistics::units::Count::get(),
               "Accesses that waited for a bank"),
      ADD_STAT(invalidations, statistics::units::Count::get(),
               "Lines invalidated by maintenance writes")
{
}

RP2350XIP::XipPort::XipPort(const std::string &name, RP2350XIP &_xip)
    : QueuedResponsePort(name, queue), xip(_xip),
      queue(_xip, *this, true, name)
{
}

AddrRangeList
RP2350XIP::XipPort::getAddrRanges() const
{
    return {xip.getAddrRange(), xip.maintenanceRange};
}

Tick
RP2350XIP::XipPort::recvAtomic(PacketPtr pkt)
{
    // Atomic mode has no cache or flash timing.
    if (xip.maintenanceRange.contains(pkt->getAddr()))
        xip.maintain(pkt);
    else
        xip.access(pkt);
    return xip.clockPeriod();
}

void
RP2350XIP::XipPort::recvFunctional(PacketPtr pkt)
{
    panic_if(!pkt->getAddrRange().isSubset(xip.getAddrRange()),
             "Functional XIP access outside flash: %s", pkt->print());
    xip.functionalAccess(pkt);
}

bool
RP2350XIP::XipPort::recvTimingReq(PacketPtr pkt)
{
    return xip.receive(pkt);
}

RP2350XIP::RP2350XIP(const RP2350XIPParams &p)
    : AbstractMemory(p), port(name() + ".port", *this),
      maintenanceRange(p.maintenance_base,
                       p.maintenance_base + p.range.size()),
      missCycles(p.miss_cycles), chainedCycles(p.chained_cycles),
      reselectCycles(p.reselect_cycles), cooldownCycles(p.cooldown_cycles),
      pageBreak(p.page_break),
      qmiEvent([this] { finishQmi(); }, name() + ".qmi"),
      releaseEvent([this] { release(); }, name() + ".release"),
      serveEvents{{{[this] { serve(0); }, name() + ".serve0"},
                   {[this] { serve(1); }, name() + ".serve1"}}},
      xipStats(*this)
{
    fatal_if(pageBreak % LineBytes != 0,
             "RP2350XIP: page_break must be a whole number of lines");
}

Port &
RP2350XIP::getPort(const std::string &name, PortID id)
{
    if (name == "port")
        return port;
    return AbstractMemory::getPort(name, id);
}

void
RP2350XIP::init()
{
    AbstractMemory::init();
    if (port.isConnected())
        port.sendRangeChange();
}

Tick
RP2350XIP::nextCycle() const
{
    const Tick edge = clockEdge();
    return edge == curTick() ? edge + clockPeriod() : edge;
}

RP2350XIP::Way *
RP2350XIP::lookup(Addr line)
{
    for (Way &way : sets[setOf(line)]) {
        if (way.valid && way.line == line)
            return &way;
    }
    return nullptr;
}

// The way is chosen at random even when the other holds nothing, as a
// Pico 2 does: of two lines read into an empty set, the first is gone half
// the time.
void
RP2350XIP::fill(Addr line)
{
    Way &way = sets[setOf(line)][rng->random<unsigned>(0, NumWays - 1)];
    way.valid = true;
    way.line = line;
}

void
RP2350XIP::maintain(PacketPtr pkt)
{
    if (pkt->isWrite()) {
        const Addr target = pkt->getAddr() - maintenanceRange.start();
        const unsigned op = target & 0x7;
        if (op == 0) {
            // Invalidate by set and way
            Way &way = sets[setOf(target)][(target >> 13) & 1];
            xipStats.invalidations += way.valid;
            way.valid = false;
        } else if (op == 2) {
            // Invalidate by address
            if (Way *way = lookup(range.start() + lineOf(target))) {
                way->valid = false;
                ++xipStats.invalidations;
            }
        } else if (op == 7) {
            warn_once("RP2350XIP: pinning cache lines is not modelled\n");
        }
        // Cleaning (1, 3) has nothing to do: no line is ever dirty.
    } else if (pkt->isRead()) {
        std::memset(pkt->getPtr<uint8_t>(), 0, pkt->getSize());
    }
    if (pkt->needsResponse())
        pkt->makeResponse();
}

bool
RP2350XIP::receive(PacketPtr pkt)
{
    panic_if(pkt->cacheResponding() || !(pkt->isRead() || pkt->isWrite()),
             "Unsupported XIP command: %s", pkt->cmdString());
    // As the bus in front adds no cycles, its delays are not charged.
    pkt->headerDelay = pkt->payloadDelay = 0;

    auto access = std::make_shared<Access>();
    access->pkt = pkt;
    // A hit answers at the next clock edge.
    access->due = nextCycle();
    ++outstanding;

    if (maintenanceRange.contains(pkt->getAddr())) {
        access->maintenance = true;
        respond(access);
        return true;
    }
    panic_if(!pkt->getAddrRange().isSubset(range),
             "XIP request outside flash: %s", pkt->print());
    access->nextLine = lineOf(pkt->getAddr());
    access->endLine = lineOf(pkt->getAddr() + pkt->getSize() - 1);
    access->requestor = pkt->requestorId();
    DPRINTF(RP2350XIP, "%s %#x\n", pkt->cmdString(), pkt->getAddr());
    Manager &manager = managers[access->requestor];
    if (manager.waiting || !manager.held.empty() ||
        curTick() < manager.freeAt)
    {
        manager.held.push_back(access);
        if (!manager.waiting && !releaseEvent.scheduled())
            schedule(releaseEvent, std::max(manager.freeAt, curTick()));
        return true;
    }
    advance(access);
    return true;
}

unsigned
RP2350XIP::wordsIn(const Access &access, Addr line)
{
    const Addr start = std::max(access.pkt->getAddr(), line);
    const Addr last = std::min<Addr>(
        access.pkt->getAddr() + access.pkt->getSize(), line + LineBytes) - 1;
    return (last >> 2) - (start >> 2) + 1;
}

void
RP2350XIP::markWaited(const AccessPtr &access)
{
    access->waited = true;
    managers[access->requestor].waiting = true;
}

void
RP2350XIP::startMiss(unsigned index, const AccessPtr &access, Addr line)
{
    Bank &bank = banks[index];
    ++xipStats.misses;
    markWaited(access);
    bank.busy = true;
    bank.owner = access;
    bank.missLine = line;
    qmiQueue.push_back(index);
    if (!qmiBusy)
        startQmi(nextCycle());
}

void
RP2350XIP::advance(const AccessPtr &access)
{
    while (access->nextLine <= access->endLine) {
        const Addr line = access->nextLine;
        const unsigned index = bankOf(line);
        Bank &bank = banks[index];
        // A bank that waits for flash, or is working through the accesses
        // that waited with it, takes this one in its turn.
        const Tick edge = clockEdge();
        const Tick cycle = edge == curTick() ? edge : edge - clockPeriod();
        if (bank.busy || !bank.waiting.empty() || bank.servedCycle == cycle) {
            DPRINTF(RP2350XIP, "line %#x waits for bank %d\n", line, index);
            markWaited(access);
            access->beatsLeft = 0;
            bank.waiting.push_back(access);
            ++xipStats.bankWaits;
            if (!bank.busy && !serveEvents[index].scheduled())
                schedule(serveEvents[index], nextCycle());
            return;
        }
        if (lookup(line)) {
            ++xipStats.hits;
            access->nextLine += LineBytes;
            continue;
        }
        startMiss(index, access, line);
        return;
    }
    respond(access);
}

void
RP2350XIP::respond(const AccessPtr &access)
{
    PacketPtr pkt = access->pkt;
    const bool needs_response = pkt->needsResponse();
    if (access->maintenance)
        maintain(pkt);
    else
        this->access(pkt);
    if (needs_response)
        port.schedTimingResp(pkt, std::max(access->due, curTick()));
    else
        pendingDelete.reset(pkt);
    if (access->waited) {
        // Its manager's next access follows its answer.
        Manager &manager = managers[access->requestor];
        manager.waiting = false;
        manager.freeAt = std::max(access->due, curTick());
        if (!manager.held.empty() && !releaseEvent.scheduled())
            schedule(releaseEvent, manager.freeAt);
    }
    --outstanding;
    if (outstanding == 0 && drainState() == DrainState::Draining)
        signalDrainDone();
}

// Each manager whose access has been answered makes its next.
void
RP2350XIP::release()
{
    Tick again = MaxTick;
    for (auto &entry : managers) {
        Manager &manager = entry.second;
        while (!manager.waiting && !manager.held.empty()) {
            if (manager.freeAt > curTick()) {
                again = std::min(again, manager.freeAt);
                break;
            }
            AccessPtr access = manager.held.front();
            manager.held.pop_front();
            access->due = std::max(access->due, nextCycle());
            advance(access);
            // One a cycle
            if (!manager.waiting)
                manager.freeAt = std::max(manager.freeAt, nextCycle());
        }
    }
    if (again != MaxTick && !releaseEvent.scheduled())
        schedule(releaseEvent, again);
}

// The front of the queue goes out on the serial interface from `start`.
void
RP2350XIP::startQmi(Tick start)
{
    const Addr line = banks[qmiQueue.front()].missLine;
    // The chip select stays low for the cooldown after a transfer, unless
    // the transfer ended at a page break.
    Cycles cost = missCycles;
    if (qmiLastEnd != MaxTick) {
        const bool at_break = pageBreak && qmiNextLine % pageBreak == 0;
        const Tick released =
            at_break ? qmiLastEnd : qmiLastEnd + cooldownCycles * clockPeriod();
        if (start < released) {
            if (line == qmiNextLine) {
                cost = chainedCycles;
                ++xipStats.chained;
            } else {
                cost = missCycles + reselectCycles;
            }
        } else if (start < released + reselectCycles * clockPeriod()) {
            cost = missCycles + reselectCycles;
        }
    }
    DPRINTF(RP2350XIP, "line %#x from flash, %d cycles\n", line,
            static_cast<uint64_t>(cost));
    qmiBusy = true;
    schedule(qmiEvent, start + cost * clockPeriod());
}

void
RP2350XIP::finishQmi()
{
    const unsigned index = qmiQueue.front();
    qmiQueue.pop_front();
    Bank &bank = banks[index];
    fill(bank.missLine);
    qmiLastEnd = curTick();
    qmiNextLine = bank.missLine + LineBytes;
    qmiBusy = false;

    AccessPtr owner = bank.owner;
    bank.owner = nullptr;
    bank.busy = false;
    bank.lastRequestor = owner->requestor;
    if (!qmiQueue.empty())
        startQmi(curTick());
    owner->due = std::max(owner->due, curTick());
    const unsigned words = wordsIn(*owner, bank.missLine);
    if (words > 1 && !bank.waiting.empty()) {
        // The line's first word has come; the rest take their turns with
        // the accesses that waited.
        owner->beatsLeft = words - 1;
        bank.waiting.push_front(owner);
    } else {
        owner->nextLine += LineBytes;
        advance(owner);
    }
    // The accesses that waited follow from this cycle.
    if (!bank.busy)
        serve(index);
}

// One word a cycle to those waiting, the managers taking turns.
void
RP2350XIP::serve(unsigned index)
{
    Bank &bank = banks[index];
    if (bank.busy || bank.waiting.empty())
        return;
    auto turn = [&bank](const AccessPtr &access) {
        return static_cast<RequestorID>(
            access->requestor - bank.lastRequestor - 1);
    };
    auto chosen = std::min_element(bank.waiting.begin(), bank.waiting.end(),
        [&turn](const AccessPtr &a, const AccessPtr &b) {
            return turn(a) < turn(b);
        });
    AccessPtr access = *chosen;
    const Addr line = access->nextLine;
    bank.servedCycle = curTick();
    bank.lastRequestor = access->requestor;
    if (access->beatsLeft == 0) {
        if (!lookup(line)) {
            bank.waiting.erase(chosen);
            startMiss(index, access, line);
            return;
        }
        ++xipStats.hits;
        access->beatsLeft = wordsIn(*access, line);
    }
    if (--access->beatsLeft == 0) {
        bank.waiting.erase(chosen);
        access->due = std::max(access->due, curTick() + clockPeriod());
        access->nextLine += LineBytes;
        advance(access);
    }
    if (!bank.busy && !bank.waiting.empty() &&
        !serveEvents[index].scheduled())
        schedule(serveEvents[index], curTick() + clockPeriod());
}

DrainState
RP2350XIP::drain()
{
    return outstanding == 0 ? DrainState::Drained : DrainState::Draining;
}

void
RP2350XIP::serialize(CheckpointOut &cp) const
{
    AbstractMemory::serialize(cp);
    std::vector<uint8_t> valid;
    std::vector<Addr> lines;
    for (const auto &set : sets) {
        for (const Way &way : set) {
            valid.push_back(way.valid);
            lines.push_back(way.line);
        }
    }
    SERIALIZE_CONTAINER(valid);
    SERIALIZE_CONTAINER(lines);
    SERIALIZE_SCALAR(qmiLastEnd);
    SERIALIZE_SCALAR(qmiNextLine);
}

void
RP2350XIP::unserialize(CheckpointIn &cp)
{
    AbstractMemory::unserialize(cp);
    std::vector<uint8_t> valid;
    std::vector<Addr> lines;
    UNSERIALIZE_CONTAINER(valid);
    UNSERIALIZE_CONTAINER(lines);
    fatal_if(valid.size() != NumSets * NumWays || lines.size() != valid.size(),
             "RP2350XIP: checkpoint of another cache geometry");
    size_t i = 0;
    for (auto &set : sets) {
        for (Way &way : set) {
            way.valid = valid[i];
            way.line = lines[i];
            ++i;
        }
    }
    UNSERIALIZE_SCALAR(qmiLastEnd);
    UNSERIALIZE_SCALAR(qmiNextLine);
}

} // namespace gem5::memory
