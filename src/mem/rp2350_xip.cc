#include "mem/rp2350_xip.hh"

#include <algorithm>
#include <cstring>
#include <limits>

#include "mem/packet_access.hh"

namespace gem5::memory
{

RP2350XIP::XipStats::XipStats(RP2350XIP &m)
    : statistics::Group(&m),
      ADD_STAT(hits, statistics::units::Count::get(), "Cache hits per word beat"),
      ADD_STAT(misses, statistics::units::Count::get(), "Cache line allocations"),
      ADD_STAT(uncached, statistics::units::Count::get(), "Uncached word beats"),
      ADD_STAT(continuations, statistics::units::Count::get(), "Sequential QMI reads"),
      ADD_STAT(commands, statistics::units::Count::get(), "New QMI read commands"),
      ADD_STAT(maintenance, statistics::units::Count::get(), "Cache maintenance writes"),
      ADD_STAT(bankWaitCycles, statistics::units::Cycle::get(), "Bank arbitration wait")
{}

RP2350XIP::RP2350XIP(const RP2350XIPParams &p)
    : AbstractMemory(p), port(name() + ".port", *this),
      responseEvent([this]{ respond(); }, name() + ".response"),
      randomState(p.replacement_seed ? p.replacement_seed : 1),
      hitCycles(p.hit_cycles), clkdiv(p.qmi_clkdiv),
      prefix(p.qmi_prefix_cycles), address(p.qmi_address_cycles),
      suffix(p.qmi_suffix_cycles), dummy(p.qmi_dummy_cycles),
      dataWidth(p.qmi_data_width), minDeselect(p.qmi_min_deselect),
      sampleCycles(p.qmi_sample_cycles), cooldown(p.qmi_cooldown_cycles),
      pagebreak(p.qmi_pagebreak_bytes), stats(*this)
{
    fatal_if(hitCycles == 0 || clkdiv == 0 || clkdiv > 256,
             "XIP hit cycles and QMI clock divisor must be positive");
    fatal_if(dataWidth != 1 && dataWidth != 2 && dataWidth != 4,
             "QMI data width must be 1, 2 or 4");
    fatal_if(pagebreak && (pagebreak & (pagebreak - 1)),
             "QMI page break must be zero or a power of two");
    fatal_if(range.start() != 0x10000000 || range.size() != 0x1000000,
             "This flash model supports chip select 0, 16 MiB");
}

Port &
RP2350XIP::getPort(const std::string &name, PortID id)
{
    return name == "port" ? port : AbstractMemory::getPort(name, id);
}

void
RP2350XIP::init()
{
    AbstractMemory::init();
    port.sendRangeChange();
}

AddrRangeList
RP2350XIP::XipPort::getAddrRanges() const
{
    return {AddrRange(0x10000000, 0x11000000),
            AddrRange(0x14000000, 0x15000000),
            AddrRange(0x18000000, 0x19000000),
            AddrRange(0x1c000000, 0x1d000000)};
}

void
RP2350XIP::increment(uint32_t &counter)
{
    if (counter != std::numeric_limits<uint32_t>::max())
        ++counter;
}

unsigned
RP2350XIP::victim()
{
    // Reproducible pseudo-random eviction, independent of guest instruction mix.
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState & 1;
}

Tick
RP2350XIP::flashRead(Addr addr, unsigned bytes, Tick when)
{
    const Tick cycle = clockPeriod();
    Tick start = std::max(when, flashFree);
    const unsigned halfSck = (clkdiv + 1) / 2;
    const Tick timeout = flashFree + (cooldown + halfSck) * cycle;
    const bool selected = flashSelected && start <= timeout;
    const bool sequential = selected && addr == flashNext;
    unsigned cycles = (bytes * 8 / dataWidth) * clkdiv;
    if (sequential) {
        ++stats.continuations;
    } else {
        ++stats.commands;
        if (selected) {
            // Interrupting an asserted transfer requires CS turnaround.
            start += (1 + halfSck + minDeselect) * cycle;
        } else if (flashNext != 0) {
            // CS has already deasserted, either at a forced page break or
            // after cooldown. Only wait for inactive time still remaining.
            const Tick off = flashSelected ? timeout :
                flashFree + halfSck * cycle;
            start = std::max(start, off + (halfSck + minDeselect) * cycle);
        }
        cycles += (prefix + address + suffix + dummy) * clkdiv;
    }
    flashFree = start + cycles * cycle;
    flashNext = addr + bytes;
    flashSelected = cooldown != 0 &&
        (!pagebreak || (flashNext % pagebreak) != 0);
    return flashFree + sampleCycles * cycle;
}

bool
RP2350XIP::maintain(Addr addr, Tick when)
{
    ++stats.maintenance;
    unsigned op = addr & 7;
    Addr tag = addr & ~Addr(7);
    auto &set = sets[(addr >> 3) & 1023];
    auto clear = [](Line &line) {
        line.valid = line.pinned = line.dirty = false;
    };
    if (op == 0) {
        clear(set[(addr >> 13) & 1]);
    } else if (op == 2) {
        for (auto &line : set)
            if (line.valid && line.tag == tag)
                clear(line);
    } else if (op == 7) {
        for (auto &line : set)
            if (line.valid && line.tag == tag)
                clear(line);
        auto &line = set[(addr >> 13) & 1];
        line.tag = tag;
        line.ready = when;
        line.valid = line.pinned = true;
        line.dirty = false;
    } else if (op == 1 || op == 3) {
        // Downstream writes require a QMI/flash programming implementation.
        // Clean lines need no work; never silently drop dirty data.
        for (unsigned way = 0; way < 2; ++way) {
            auto &line = set[way];
            if (line.valid && line.dirty &&
                (op == 1 ? way == ((addr >> 13) & 1) : line.tag == tag))
                return false;
        }
    } else {
        return false;
    }
    return true;
}

Tick
RP2350XIP::service(PacketPtr pkt, bool timing)
{
    const Addr original = pkt->getAddr();
    const Addr addr = 0x10000000 | (original & 0xffffff);
    const unsigned alias = (original >> 26) & 3;
    // The zero-latency board routers deliver the address just after the
    // launching edge. It belongs to that bus cycle, not the next one.
    Tick now = timing ? curTick() - curTick() % clockPeriod() : curTick();
    if (timing && curTick() >= now + clockPeriod() / 2)
        now += clockPeriod();
    Tick done = now;
    const bool needsResponse = pkt->needsResponse();
    bool error = false;
    if (alias == 2) {
        error = !pkt->isWrite() || !maintain(addr, now);
        done += hitCycles * clockPeriod();
    } else {
        const bool enabled = control & (pkt->isSecure() ? 1 : 2);
        error = (alias == 1 && (control & (pkt->isSecure() ? 0x10 : 0x20))) ||
                (alias == 3 && (control & (pkt->isSecure() ? 0x40 : 0x80)));
        unsigned offset = 0;
        while (!error && offset < pkt->getSize()) {
            const Addr beat = addr + offset;
            // The cache's data RAM reads an entire 64-bit line. A gem5
            // packet inside one line is one lookup, even when it bundles
            // two AHB words. Direct QMI reads remain word-size transfers.
            const unsigned boundary = alias == 0 && enabled ? 8 : 4;
            const unsigned size = std::min<unsigned>(
                pkt->getSize() - offset, boundary - (beat % boundary));
            const unsigned words = ((beat & 3) + size + 3) / 4;
            const unsigned bank = (beat >> 3) & 1;
            const Tick begin = timing ? std::max(now, bankFree[bank]) : now;
            if (timing) {
                stats.bankWaitCycles += (begin - now) / clockPeriod();
                bankFree[bank] = begin + clockPeriod();
            }
            // A one-cycle AHB hit supplies data before the closing edge,
            // while occupying its bank for the entire cycle. Returning on
            // the edge itself makes the board routers deliver it too late
            // for that edge and turns a one-cycle hit into two CPU cycles.
            Tick ready = begin + (hitCycles - 1) * clockPeriod() +
                         clockPeriod() / 2;
            // The direct uncached mirrors bypass the cache counter boundary
            // (confirmed by the paired cached/uncached hardware probes).
            if (alias == 0)
                for (unsigned i = 0; i < words; ++i)
                    increment(accesses);
            if (alias == 0 && enabled) {
                auto &set = sets[(beat >> 3) & 1023];
                const Addr tag = beat & ~Addr(7);
                Line *line = nullptr;
                for (auto &entry : set)
                    if (entry.valid && entry.tag == tag)
                        line = &entry;
                if (line) {
                    for (unsigned i = 0; i < words; ++i)
                        increment(hits);
                    stats.hits += words;
                    ready = std::max(ready, line->ready);
                } else {
                    // Hardware chooses a random way, including invalid ways.
                    Line &entry = set[victim()];
                    if (!entry.pinned && !entry.dirty) {
                        ++stats.misses;
                        ready = flashRead(tag, 8, ready);
                        entry.tag = tag;
                        entry.valid = true;
                        entry.dirty = false;
                        entry.ready = ready;
                        std::memcpy(entry.data.data(), toHostAddr(tag), 8);
                        line = &entry;
                        // The first word missed; the other bundled words
                        // are served by that allocation.
                        for (unsigned i = 1; i < words; ++i)
                            increment(hits);
                        stats.hits += words - 1;
                    } else if (entry.dirty && !entry.pinned) {
                        error = true;
                    }
                }
                if (line && !error) {
                    auto *data = line->data.data() + (beat & 7);
                    if (pkt->isRead()) {
                        std::memcpy(pkt->getPtr<uint8_t>() + offset, data, size);
                    } else if (pkt->isWrite()) {
                        for (unsigned i = 0; i < size; ++i)
                            if (pkt->req->getByteEnable()[offset + i])
                                data[i] = pkt->getConstPtr<uint8_t>()[offset+i];
                        line->dirty = true;
                    } else {
                        error = true;
                    }
                } else if (!error) {
                    // A selected pinned way demotes the access to uncached.
                    ++stats.uncached;
                    ready = flashRead(beat, size, ready);
                    if (pkt->isRead())
                        std::memcpy(pkt->getPtr<uint8_t>() + offset,
                                    toHostAddr(beat), size);
                    else
                        error = true;
                }
            } else {
                ++stats.uncached;
                ready = flashRead(beat, size, begin);
                if (pkt->isRead())
                    std::memcpy(pkt->getPtr<uint8_t>() + offset,
                                toHostAddr(beat), size);
                else
                    error = true;
            }
            done = std::max(done, ready);
            offset += size;
        }
    }
    if (needsResponse) {
        pkt->makeResponse();
        if (error)
            pkt->setBadAddress();
    }
    return std::max(done, curTick() + 1) - curTick();
}

void
RP2350XIP::functional(PacketPtr pkt)
{
    Addr original = pkt->getAddr();
    pkt->setAddr(0x10000000 | (original & 0xffffff));
    functionalAccess(pkt);
    pkt->setAddr(original);
}

void
RP2350XIP::queueResponse(PacketPtr pkt, Tick ready)
{
    auto pos = std::upper_bound(responses.begin(), responses.end(), ready,
        [](Tick t, const Response &r) { return t < r.ready; });
    responses.insert(pos, {pkt, ready});
    if (!retryResponse) {
        if (responseEvent.scheduled())
            reschedule(responseEvent, responses.front().ready);
        else
            schedule(responseEvent, responses.front().ready);
    }
}

bool
RP2350XIP::receive(PacketPtr pkt)
{
    if (responses.size() >= 32 || retryRequest) {
        retryRequest = true;
        return false;
    }
    const Tick delay = pkt->headerDelay + pkt->payloadDelay;
    pkt->headerDelay = pkt->payloadDelay = 0;
    bool response = pkt->needsResponse();
    Tick latency = service(pkt, true);
    if (response)
        queueResponse(pkt, curTick() + delay + latency);
    else
        pendingDelete.reset(pkt);
    return true;
}

void
RP2350XIP::respond()
{
    retryResponse = !port.sendTimingResp(responses.front().pkt);
    if (retryResponse)
        return;
    responses.pop_front();
    if (!responses.empty())
        schedule(responseEvent, std::max(curTick(), responses.front().ready));
    if (retryRequest && responses.size() < 32) {
        retryRequest = false;
        port.sendRetryReq();
    }
    if (responses.empty() && drainState() == DrainState::Draining)
        signalDrainDone();
}

bool RP2350XIP::XipPort::recvTimingReq(PacketPtr pkt) { return owner.receive(pkt); }
Tick RP2350XIP::XipPort::recvAtomic(PacketPtr pkt) { return owner.service(pkt, false); }
void RP2350XIP::XipPort::recvFunctional(PacketPtr pkt) { owner.functional(pkt); }
void RP2350XIP::XipPort::recvRespRetry() { owner.retryResponse = false; owner.respond(); }

DrainState RP2350XIP::drain()
{
    return responses.empty() ? DrainState::Drained : DrainState::Draining;
}

uint32_t
RP2350XIP::readControl(Addr offset) const
{
    switch (offset & ~Addr(3)) {
      case 0x00: return control;
      case 0x08: return 2; // Streaming FIFO empty.
      case 0x0c: return hits;
      case 0x10: return accesses;
      default: return 0;
    }
}

void
RP2350XIP::writeControl(Addr offset, uint32_t value, unsigned op)
{
    switch (offset & ~Addr(3)) {
      case 0x00:
        switch (op) {
          case 1: control ^= value; break;
          case 2: control |= value; break;
          case 3: control &= ~value; break;
          default: control = value;
        }
        if (control & 8)
            control &= ~3;
        break;
      case 0x0c: hits = 0; break;
      case 0x10: accesses = 0; break;
      default: break;
    }
}

} // namespace gem5::memory
