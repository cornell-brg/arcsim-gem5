#include "dev/hwpe/hwpe_eqpyr.hh"

#include <algorithm>
#include <cstring>

#include "base/trace.hh"
#include "debug/HWPE.hh"
#include "sim/byteswap.hh"
#include "sim/serialize.hh"

namespace gem5
{

namespace
{

// reflect101: the index of a pixel past the edge, mirrored from the inside
int
reflect(int i, int n)
{
    while (i < 0 || i >= n)
        i = i < 0 ? -i : 2 * n - 2 - i;
    return i;
}

// Rows of the level below that row y of a level of n rows below needs
// (pyr_needs): the last one
unsigned
lastNeeded(unsigned y, unsigned n)
{
    int m = 0;
    for (int d = 0; d < 5; ++d)
        m = std::max(m, reflect(int(2 * y) + d - 2, int(n)));
    return unsigned(m);
}

std::vector<std::vector<unsigned>>
completes(unsigned below, unsigned above)
{
    std::vector<std::vector<unsigned>> c(below);
    for (unsigned y = 0; y < above; ++y)
        c[lastNeeded(y, below)].push_back(y);
    return c;
}

} // anonymous namespace

HWPEEqpyr::HWPEEqpyr(const Params &p)
    : HWPEBase(p),
      width(p.width),
      height(p.height),
      stripeRows(p.stripe_rows),
      beatBytes(4 * p.words),
      l2BeatBytes(16),
      pixelCycles(p.pixel_cycles),
      tableCycles(p.table_cycles),
      histCycles(p.hist_cycles),
      tailCycles(p.tail_cycles),
      outCycles(p.out_cycles),
      pyrCycles(p.pyr_cycles),
      histLatency(p.hist_latency),
      words(streamer.addSource("words", p.buffer_depth)),
      l0Out(streamer.addSink("l0", p.buffer_depth)),
      l1Out(streamer.addSink("l1", p.buffer_depth)),
      l2Out(streamer.addSink("l2", p.buffer_depth)),
      histOut(streamer.addSink("hist", p.buffer_depth)),
      computeEvent([this] { compute(); }, name() + ".compute")
{
    fatal_if(width % beatBytes || width % 64 || height % stripeRows,
             "%s: a %dx%d frame in %d-row stripes, %d-byte words", name(),
             width, height, stripeRows, beatBytes);
    for (unsigned l = 0; l < 3; ++l)
        level[l].assign(levelWidth(l) * levelHeight(l), 0);
    for (unsigned l = 0; l < 2; ++l) {
        completesRow[l] = completes(levelHeight(l), levelHeight(l + 1));
        completesCol[l] = completes(levelWidth(l), levelWidth(l + 1));
    }
}

uint8_t
HWPEEqpyr::pyrDown(unsigned l, unsigned y, unsigned x) const
{
    static const int k[5] = {1, 4, 6, 4, 1};
    const int sw = levelWidth(l), sh = levelHeight(l);
    const std::vector<uint8_t> &src = level[l];
    int s = 0;
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 5; ++j)
            s += k[i] * k[j] *
                 src[reflect(2 * y + i - 2, sh) * sw + reflect(2 * x + j - 2, sw)];
    return uint8_t((s + 128) >> 8);
}

void
HWPEEqpyr::make(unsigned l, unsigned y, unsigned x, uint8_t value)
{
    const unsigned per_word = l == 2 ? l2BeatBytes : beatBytes;
    level[l][y * levelWidth(l) + x] = value;
    pack[l].push_back(value);
    if (pack[l].size() == per_word) {
        due.push_back(
            {when + cyclesToTicks(Cycles(outCycles + l * pyrCycles)), l,
             pack[l]});
        pack[l].clear();
    }
    if (l < 2) {
        for (unsigned yy : completesRow[l][y])
            for (unsigned xx : completesCol[l][x])
                make(l + 1, yy, xx, pyrDown(l, yy, xx));
    }
}

bool
HWPEEqpyr::flush()
{
    Sink *out[3] = {&l0Out, &l1Out, &l2Out};
    while (!due.empty() && due.front().when <= curTick()) {
        Due &d = due.front();
        if (!out[d.level]->room())
            return false;
        out[d.level]->push(d.data.data(), d.data.size());
        ++wordsOut[d.level];
        due.pop_front();
    }
    return true;
}

void
HWPEEqpyr::startStores()
{
    AddressGenConfig config;
    config.base = l0Ptr;
    config.totLen = levelWidth(0) * levelHeight(0) / beatBytes;
    config.d0Stride = beatBytes;
    l0Out.start(config, beatBytes);
    config.base = l1Ptr;
    config.totLen = levelWidth(1) * levelHeight(1) / beatBytes;
    l1Out.start(config, beatBytes);
    config.base = l2Ptr;
    config.totLen = levelWidth(2) * levelHeight(2) / l2BeatBytes;
    config.d0Stride = l2BeatBytes;
    l2Out.start(config, l2BeatBytes);
}

void
HWPEEqpyr::startJob(const std::vector<uint32_t> &io)
{
    AddressGenConfig config;
    config.d0Stride = beatBytes;
    taken = 0;
    nextTake = 0;
    tailDone = MaxTick;
    if (io[NEW_FRAME] & 1) {
        l0Ptr = io[L0_PTR];
        l1Ptr = io[L1_PTR];
        l2Ptr = io[L2_PTR];
        tablePtr = io[TABLE_PTR];
        histPtr = io[HIST_PTR];
        ringPtr = io[RING_PTR];
        ringStripes = io[RING_STRIPES];
        ringSlot = 0;
        stripeRow = 0;
        hist.fill(0);
        for (auto &p : pack)
            p.clear();
        for (auto &w : wordsOut)
            w = 0;
        histWritten = false;
        startStores();
        config.base = tablePtr;
        config.totLen = 256 / beatBytes;
        words.start(config, beatBytes);
        total = 256 / 4;
        phase = Phase::Table;
        DPRINTF(HWPE, "eqpyr: new frame, table %#x, ring %#x of %d\n",
                tablePtr, ringPtr, ringStripes);
    } else {
        const uint32_t slot_bytes = stripeRows * width;
        if (ringStripes) {
            config.base = ringPtr + ringSlot * slot_bytes;
            ringSlot = ringSlot + 1 >= ringStripes ? 0 : ringSlot + 1;
        } else {
            config.base = l0Ptr + stripeRow * width;
        }
        config.totLen = slot_bytes / beatBytes;
        words.start(config, beatBytes);
        total = slot_bytes;
        phase = Phase::Stripe;
        DPRINTF(HWPE, "eqpyr: stripe from row %d at %#x\n", stripeRow,
                config.base);
    }
    compute();
}

void
HWPEEqpyr::clearJob()
{
    phase = Phase::Idle;
    for (auto &p : pack)
        p.clear();
    due.clear();
    if (computeEvent.scheduled())
        deschedule(computeEvent);
}

uint32_t
HWPEEqpyr::extFlags() const
{
    const uint32_t r0 = wordsOut[0] * beatBytes / levelWidth(0);
    const uint32_t r1 = wordsOut[1] * beatBytes / levelWidth(1);
    const uint32_t r2 = wordsOut[2] * l2BeatBytes / levelWidth(2);
    return (histWritten ? 1u << 31 : 0) | (r2 << 17) | (r1 << 9) | r0;
}

void
HWPEEqpyr::compute()
{
    if (phase == Phase::Idle)
        return;
    const Tick edge = clockEdge();
    const Tick cycle = edge == curTick() ? edge : edge - clockPeriod();
    if (phase == Phase::Table) {
        // A beat's 32-bit table words, one a cycle
        while (taken < total && words.ready() && nextTake <= curTick()) {
            const std::vector<uint8_t> &data = words.front();
            Tick when = std::max(nextTake, cycle);
            for (unsigned i = 0; i < beatBytes; ++i)
                lut[taken * 4 + i] = data[i];
            taken += beatBytes / 4;
            when += cyclesToTicks(Cycles(tableCycles * (beatBytes / 4)));
            nextTake = when;
            words.pop();
        }
        if (taken == total && tailDone == MaxTick)
            tailDone = std::max(nextTake, cycle) + cyclesToTicks(tailCycles);
    } else if (phase == Phase::Stripe) {
        // A stripe's pixels, one each pixel_cycles: equalized as level 0,
        // and the pixels of levels 1 and 2 each completes. A word goes to
        // its store out_cycles after its last pixel; the engine waits while
        // more than a beat of words is held up by a full store.
        const bool flowing = flush();
        while (flowing && due.size() <= 3 && taken < total && words.ready() &&
               nextTake <= curTick()) {
            const std::vector<uint8_t> &data = words.front();
            when = std::max(nextTake, cycle);
            for (unsigned i = 0; i < beatBytes; ++i) {
                const unsigned y = stripeRow + (taken + i) / width;
                const unsigned x = (taken + i) % width;
                const uint8_t raw = data[i];
                ++hist[raw];
                make(0, y, x, lut[raw]);
                when += cyclesToTicks(pixelCycles);
            }
            taken += beatBytes;
            nextTake = when;
            words.pop();
        }
        flush();
        if (taken == total && due.empty() && tailDone == MaxTick) {
            stripeRow += stripeRows;
            tailDone = std::max(nextTake, cycle) + cyclesToTicks(
                stripeRow == height ? histLatency : tailCycles);
        }
    } else if (phase == Phase::Hist) {
        while (histBeats < 256 * 4 / beatBytes && histOut.room() &&
               nextTake <= curTick()) {
            std::vector<uint8_t> beat(beatBytes);
            for (unsigned i = 0; i < beatBytes / 4; ++i) {
                const uint32_t le = htole(hist[histBeats * beatBytes / 4 + i]);
                std::memcpy(&beat[4 * i], &le, 4);
            }
            histOut.push(beat.data(), beatBytes);
            ++histBeats;
            nextTake = std::max(nextTake, cycle) + cyclesToTicks(histCycles);
        }
        if (histBeats == 256 * 4 / beatBytes && histOut.done()) {
            histWritten = true;
            phase = Phase::Idle;
            DPRINTF(HWPE, "eqpyr: histogram written\n");
            jobDone();
            return;
        }
    }
    if ((phase == Phase::Table || phase == Phase::Stripe) &&
        tailDone != MaxTick && curTick() >= tailDone) {
        if (phase == Phase::Stripe && stripeRow == height) {
            AddressGenConfig config;
            config.base = histPtr;
            config.totLen = 256 * 4 / beatBytes;
            config.d0Stride = beatBytes;
            histOut.start(config, beatBytes);
            histBeats = 0;
            nextTake = curTick();
            phase = Phase::Hist;
            compute();
            return;
        }
        phase = Phase::Idle;
        jobDone();
        return;
    }
    Tick next = MaxTick;
    if (nextTake > curTick() && (phase == Phase::Hist || words.ready()))
        next = nextTake;
    if (!due.empty() && due.front().when > curTick())
        next = std::min(next, due.front().when);
    if (tailDone != MaxTick && tailDone > curTick())
        next = std::min(next, tailDone);
    if (next != MaxTick)
        reschedule(computeEvent, next, true);
}

DrainState
HWPEEqpyr::drain()
{
    if (phase != Phase::Idle || computeEvent.scheduled())
        return DrainState::Draining;
    return HWPEBase::drain();
}

void
HWPEEqpyr::serializeJob(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(l0Ptr);
    SERIALIZE_SCALAR(l1Ptr);
    SERIALIZE_SCALAR(l2Ptr);
    SERIALIZE_SCALAR(tablePtr);
    SERIALIZE_SCALAR(histPtr);
    SERIALIZE_SCALAR(ringPtr);
    SERIALIZE_SCALAR(ringStripes);
    SERIALIZE_SCALAR(ringSlot);
    SERIALIZE_SCALAR(stripeRow);
    SERIALIZE_ARRAY(lut.data(), lut.size());
    SERIALIZE_ARRAY(hist.data(), hist.size());
    SERIALIZE_CONTAINER(level[0]);
    SERIALIZE_CONTAINER(level[1]);
    SERIALIZE_CONTAINER(level[2]);
    SERIALIZE_ARRAY(wordsOut, 3);
    SERIALIZE_SCALAR(histWritten);
}

void
HWPEEqpyr::unserializeJob(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(l0Ptr);
    UNSERIALIZE_SCALAR(l1Ptr);
    UNSERIALIZE_SCALAR(l2Ptr);
    UNSERIALIZE_SCALAR(tablePtr);
    UNSERIALIZE_SCALAR(histPtr);
    UNSERIALIZE_SCALAR(ringPtr);
    UNSERIALIZE_SCALAR(ringStripes);
    UNSERIALIZE_SCALAR(ringSlot);
    UNSERIALIZE_SCALAR(stripeRow);
    UNSERIALIZE_ARRAY(lut.data(), lut.size());
    UNSERIALIZE_ARRAY(hist.data(), hist.size());
    UNSERIALIZE_CONTAINER(level[0]);
    UNSERIALIZE_CONTAINER(level[1]);
    UNSERIALIZE_CONTAINER(level[2]);
    UNSERIALIZE_ARRAY(wordsOut, 3);
    UNSERIALIZE_SCALAR(histWritten);
    phase = Phase::Idle;
}

} // namespace gem5
