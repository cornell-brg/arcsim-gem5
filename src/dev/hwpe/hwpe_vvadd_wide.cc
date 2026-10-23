#include "dev/hwpe/hwpe_vvadd_wide.hh"

#include <algorithm>
#include <cstring>

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/HWPE.hh"
#include "sim/byteswap.hh"
#include "sim/serialize.hh"

namespace gem5
{

HWPEVvaddWide::HWPEVvaddWide(const Params &p)
    : HWPEBase(p),
      elems(p.elems),
      computeLatency(p.compute_latency),
      computeInterval(p.compute_interval),
      a(streamer.addSource("a", p.buffer_depth)),
      b(streamer.addSource("b", p.buffer_depth)),
      c(streamer.addSink("c", p.buffer_depth)),
      computeEvent([this] { compute(); }, name() + ".compute")
{
    fatal_if(elems < 1 || elems > 8,
             "%s: %d elements a beat; 1 to 8 (256 bits)", name(), elems);
    fatal_if(computeInterval < 1, "%s: compute_interval must be at least 1",
             name());
}

void
HWPEVvaddWide::startJob(const std::vector<uint32_t> &io_regs)
{
    const uint32_t len = io_regs[LEN];
    DPRINTF(HWPE, "vvadd: A %#x, B %#x, C %#x, LEN %d\n", io_regs[A_PTR],
            io_regs[B_PTR], io_regs[C_PTR], len);
    if (len == 0) {
        jobDone();
        return;
    }
    const uint32_t beats = (uint64_t(len) + elems - 1) / elems;
    const unsigned beat_bytes = 4 * elems;
    const unsigned last_beat_bytes = 4 * (len - (beats - 1) * elems);
    AddressGenConfig config;
    config.totLen = beats;
    config.d0Stride = beat_bytes;
    config.base = io_regs[A_PTR];
    a.start(config, beat_bytes, last_beat_bytes);
    config.base = io_regs[B_PTR];
    b.start(config, beat_bytes, last_beat_bytes);
    config.base = io_regs[C_PTR];
    c.start(config, beat_bytes);
    remaining = len;
    nextTake = 0;
    active = true;
}

void
HWPEVvaddWide::clearJob()
{
    active = false;
    remaining = 0;
    results.clear();
    if (computeEvent.scheduled())
        deschedule(computeEvent);
}

void
HWPEVvaddWide::compute()
{
    if (!active)
        return;
    // The start of the current cycle. Timing counts from there, so a beat
    // that arrives in the middle of a cycle is taken in that cycle.
    const Tick edge = clockEdge();
    const Tick cycle = edge == curTick() ? edge : edge - clockPeriod();
    // Pipeline depth: at most one result per cycle of latency in flight,
    // and at least one place for a result waiting for room in c
    const size_t stages = std::max<uint64_t>(computeLatency, 1);
    // Repeat until nothing changes: with no latency, a pair taken gives a
    // result that can go to c at once
    bool moved = true;
    while (moved) {
        moved = false;
        // Push results whose latency has passed to c
        while (!results.empty() && results.front().due <= curTick() &&
               c.room()) {
            c.push(results.front().data.data(), results.front().bytes);
            results.pop_front();
            moved = true;
        }
        // Take a beat from each of a and b and add them
        if (remaining && a.ready() && b.ready() && curTick() >= nextTake &&
            results.size() < stages) {
            const unsigned n = std::min<uint32_t>(remaining, elems);
            const std::vector<uint8_t> &x = a.front();
            const std::vector<uint8_t> &y = b.front();
            std::vector<uint8_t> sum(4 * elems, 0);
            for (unsigned i = 0; i < n; ++i) {
                uint32_t u, v;
                std::memcpy(&u, &x[4 * i], 4);
                std::memcpy(&v, &y[4 * i], 4);
                // Wraps on overflow
                const uint32_t w = htole<uint32_t>(letoh(u) + letoh(v));
                std::memcpy(&sum[4 * i], &w, 4);
            }
            a.pop();
            b.pop();
            remaining -= n;
            nextTake = cycle + computeInterval * clockPeriod();
            results.push_back(Result{std::move(sum), 4 * n,
                                     cycle + computeLatency * clockPeriod()});
            moved = true;
        }
    }
    if (!remaining && results.empty() && c.done()) {
        DPRINTF(HWPE, "vvadd: done\n");
        active = false;
        jobDone();
        return;
    }
    // Nothing more can be done now. If we wait for time (a result's
    // latency, or compute_interval) rather than for a stream, schedule
    // computeEvent for then. Waiting for a stream ends with a call of
    // streamProgress().
    Tick when = MaxTick;
    if (!results.empty() && results.front().due > curTick())
        when = results.front().due;
    if (remaining && a.ready() && b.ready() && nextTake > curTick() &&
        results.size() < stages) {
        when = std::min(when, nextTake);
    }
    if (when != MaxTick)
        reschedule(computeEvent, when, true);
}

void
HWPEVvaddWide::serializeJob(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(active);
    SERIALIZE_SCALAR(remaining);
    SERIALIZE_SCALAR(nextTake);
    const unsigned num_results = results.size();
    SERIALIZE_SCALAR(num_results);
    for (unsigned n = 0; n < num_results; ++n) {
        arrayParamOut(cp, csprintf("result%d.data", n), results[n].data);
        paramOut(cp, csprintf("result%d.bytes", n), results[n].bytes);
        paramOut(cp, csprintf("result%d.due", n), results[n].due);
    }
    const Tick compute_tick =
        computeEvent.scheduled() ? computeEvent.when() : MaxTick;
    SERIALIZE_SCALAR(compute_tick);
}

void
HWPEVvaddWide::unserializeJob(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(active);
    UNSERIALIZE_SCALAR(remaining);
    UNSERIALIZE_SCALAR(nextTake);
    unsigned num_results;
    UNSERIALIZE_SCALAR(num_results);
    results.clear();
    for (unsigned n = 0; n < num_results; ++n) {
        Result result;
        arrayParamIn(cp, csprintf("result%d.data", n), result.data);
        paramIn(cp, csprintf("result%d.bytes", n), result.bytes);
        paramIn(cp, csprintf("result%d.due", n), result.due);
        results.push_back(std::move(result));
    }
    Tick compute_tick;
    UNSERIALIZE_SCALAR(compute_tick);
    if (compute_tick != MaxTick)
        schedule(computeEvent, compute_tick);
}

} // namespace gem5
