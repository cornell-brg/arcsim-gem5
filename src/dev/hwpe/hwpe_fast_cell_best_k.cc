#include "dev/hwpe/hwpe_fast_cell_best_k.hh"

#include <algorithm>
#include <cstring>

#include "base/bitfield.hh"
#include "base/intmath.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/HWPE.hh"
#include "sim/byteswap.hh"
#include "sim/serialize.hh"

namespace gem5
{

namespace
{

// The 16 ring pixels around a centre pixel, as {row, col} offsets, in the
// RTL's order (fastCornerDetectorWithScoringMulticycle's ring_pixels)
const int ring[16][2] = {
    {0, -3}, {1, -3}, {2, -2}, {3, -1}, {3, 0}, {3, 1}, {2, 2}, {1, 3},
    {0, 3}, {-1, 3}, {-2, 2}, {-3, 1}, {-3, 0}, {-3, -1}, {-2, -2}, {-1, -3},
};

// Bits of a row or col of a cell of 'size' (the RTL's $clog2, at least 1)
unsigned
positionBits(unsigned size)
{
    return size > 1 ? ceilLog2(size) : 1;
}

} // anonymous namespace

HWPEFastCellBestK::HWPEFastCellBestK(const Params &p)
    : HWPEBase(p),
      width(p.cell_width),
      height(p.cell_height),
      frameWidth(p.frame_width),
      n(p.n),
      threshold(p.threshold),
      cycles(p.cycles),
      k(p.k),
      beatBytes(4 * p.words),
      colBits(positionBits(p.cell_width)),
      rowBits(positionBits(p.cell_height)),
      pixels(streamer.addSource("pixels", p.buffer_depth)),
      // One beat a job; the RTL's corners FIFO holds 4
      corners(streamer.addSink("corners", 4)),
      computeEvent([this] { compute(); }, name() + ".compute")
{
    fatal_if(p.words < 1 || p.words > 8,
             "%s: %d words an access; 1 to 8 (256 bits)", name(), p.words);
    fatal_if(width < 7 || height < 7,
             "%s: a %dx%d cell; at least 7x7", name(), width, height);
    fatal_if(width % beatBytes, "%s: cell_width must be a multiple of %d "
             "(the pixels of an access), got %d", name(), beatBytes, width);
    fatal_if(n < 1 || n > 16, "%s: n is %d; 1 to 16", name(), n);
    fatal_if(threshold < 1 || threshold > 255,
             "%s: threshold is %d; 1 to 255", name(), threshold);
    fatal_if(cycles != 1 && cycles != 2 && cycles != 4 && cycles != 8 &&
             cycles != 16, "%s: cycles is %d; 1, 2, 4, 8 or 16", name(),
             cycles);
    fatal_if(k < 1 || k > p.words, "%s: k is %d; 1 to words (%d)", name(), k,
             p.words);
    fatal_if(8 + rowBits + colBits > 32, "%s: a %dx%d cell's corner words "
             "do not fit 32 bits", name(), width, height);
}

void
HWPEFastCellBestK::startJob(const std::vector<uint32_t> &io_regs)
{
    DPRINTF(HWPE, "fast: IMAGE %#x, CORNERS %#x\n", io_regs[IMAGE_PTR],
            io_regs[CORNERS_PTR]);
    // The cell: beats along each row, then the next row frame_width bytes
    // further
    const uint32_t row_beats = width / beatBytes;
    AddressGenConfig config;
    config.base = io_regs[IMAGE_PTR];
    config.totLen = height * row_beats;
    config.d0Len = row_beats;
    config.d0Stride = beatBytes;
    config.d1Stride = frameWidth;
    config.dims = 2;
    pixels.start(config, beatBytes);
    // The corners: one beat
    config = AddressGenConfig();
    config.base = io_regs[CORNERS_PTR];
    config.totLen = 1;
    config.d0Stride = beatBytes;
    corners.start(config, beatBytes);
    cell.assign(width * height, 0);
    scores.assign((width - 6) * (height - 6), 0);
    taken = 0;
    consuming = false;
    beatEnd = 0;
    beatCorners = 0;
    nextTake = 0;
    cornersDue = MaxTick;
    written = false;
    cornerCount = 0;
    active = true;
}

void
HWPEFastCellBestK::clearJob()
{
    active = false;
    consuming = false;
    written = false;
    cornersDue = MaxTick;
    cornerCount = 0;
    if (computeEvent.scheduled())
        deschedule(computeEvent);
}

Cycles
HWPEFastCellBestK::pixelCycles(unsigned row, unsigned col) const
{
    // A pixel that completes a window is scored: it is taken, its window
    // is formed, the score starts, cycles of arc minimums, the maximum.
    // Any other pixel: taken, then its window is formed.
    return Cycles(row >= 6 && col >= 6 ? cycles + 4 : 2);
}

uint8_t
HWPEFastCellBestK::score(unsigned row, unsigned col) const
{
    // How much brighter and how much darker each ring pixel is
    const unsigned centre = cell[row * width + col];
    unsigned brighter[16], darker[16];
    for (unsigned i = 0; i < 16; ++i) {
        const unsigned pixel =
            cell[(row + ring[i][0]) * width + col + ring[i][1]];
        brighter[i] = pixel > centre ? pixel - centre : 0;
        darker[i] = centre > pixel ? centre - pixel : 0;
    }
    // The largest minimum of an arc of n pixels, in either direction
    unsigned largest = 0;
    for (unsigned start = 0; start < 16; ++start) {
        unsigned min_brighter = 255, min_darker = 255;
        for (unsigned i = 0; i < n; ++i) {
            min_brighter = std::min(min_brighter, brighter[(start + i) % 16]);
            min_darker = std::min(min_darker, darker[(start + i) % 16]);
        }
        largest = std::max({largest, min_brighter, min_darker});
    }
    // The ring is more than score brighter or darker; 0 stays 0
    const unsigned score = largest ? largest - 1 : 0;
    return score >= threshold ? score : 0;
}

void
HWPEFastCellBestK::takeBeat()
{
    // The beat's first pixel is taken once the engine is free, and not
    // before the cycle the beat is here
    const Tick edge = clockEdge();
    const Tick cycle = edge == curTick() ? edge : edge - clockPeriod();
    Tick when = std::max(nextTake, cycle);
    const std::vector<uint8_t> &data = pixels.front();
    beatCorners = 0;
    for (unsigned i = 0; i < beatBytes; ++i, ++taken) {
        const unsigned row = taken / width;
        const unsigned col = taken % width;
        cell[taken] = data[i];
        // The pixel completes the window around row - 3, col - 3
        if (row >= 6 && col >= 6) {
            const uint8_t s = score(row - 3, col - 3);
            scores[(row - 6) * (width - 6) + col - 6] = s;
            beatCorners += s != 0;
        }
        beatEnd = when;
        when += cyclesToTicks(pixelCycles(row, col));
    }
    nextTake = when;
    consuming = true;
    if (taken == cell.size()) {
        // After the last pixel: its score (cycles + 4), then the non-max
        // suppression decides the positions still waiting for it, from
        // the one above and to the left of it to the end of the last row
        // (width - 6 + 2, one a cycle), and the best-k list is sent the
        // cycle after the last decision
        cornersDue = beatEnd +
                     cyclesToTicks(Cycles(cycles + 4 + (width - 6) + 3));
    }
    DPRINTF(HWPE, "fast: beat of pixels %d to %d, %d corners\n",
            taken - beatBytes, taken - 1, beatCorners);
}

void
HWPEFastCellBestK::writeCorners()
{
    const unsigned grid_width = width - 6;
    const unsigned grid_height = height - 6;
    const unsigned score_lsb = rowBits + colBits;
    // As the RTL's best_corners_data: best[k - 1] is the best corner, and
    // an empty place is 0
    std::vector<uint32_t> best(k, 0);
    // The score s is greater than that of place i (never of place k)
    auto greater = [&](uint32_t s, unsigned i) {
        return i < k && s > bits(best[i], score_lsb + 7, score_lsb);
    };
    // The positions in raster order, as the non-max suppression decides
    // them
    for (unsigned row = 0; row < grid_height; ++row) {
        for (unsigned col = 0; col < grid_width; ++col) {
            const uint32_t s = scores[row * grid_width + col];
            // Kept when its score is greater than each neighbour's, 0
            // outside the grid (so a position that is not a corner never
            // is)
            bool keep = true;
            for (int dr = -1; dr <= 1; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    const int r = int(row) + dr;
                    const int c = int(col) + dc;
                    if ((dr == 0 && dc == 0) || r < 0 || c < 0 ||
                        r >= int(grid_height) || c >= int(grid_width)) {
                        continue;
                    }
                    if (s <= scores[r * grid_width + c])
                        keep = false;
                }
            }
            if (!keep)
                continue;
            const uint32_t word = (s << score_lsb) |
                (((row + 3) & mask(rowBits)) << colBits) |
                ((col + 3) & mask(colBits));
            // The corner goes in after the corners it beats, which move
            // down; on a tie it goes below (the first one stays best)
            std::vector<uint32_t> next = best;
            for (unsigned i = 0; i < k; ++i) {
                if (greater(s, i + 1))
                    next[i] = best[i + 1];
                else if (greater(s, i))
                    next[i] = word;
            }
            best = std::move(next);
        }
    }
    // Word i is the i-th best corner; the words of empty places are not
    // written
    std::vector<uint8_t> beat(beatBytes, 0);
    std::vector<bool> enables(beatBytes, false);
    for (unsigned i = 0; i < k; ++i) {
        const uint32_t word = best[k - 1 - i];
        const uint32_t le = htole(word);
        std::memcpy(&beat[4 * i], &le, 4);
        std::fill_n(enables.begin() + 4 * i, 4, word != 0);
        if (word) {
            DPRINTF(HWPE, "fast: corner %d: score %d, row %d, col %d\n", i,
                    bits(word, score_lsb + 7, score_lsb),
                    bits(word, score_lsb - 1, colBits),
                    bits(word, colBits - 1, 0));
        }
    }
    corners.pushMasked(beat.data(), enables);
}

void
HWPEFastCellBestK::compute()
{
    if (!active)
        return;
    // The beat whose last pixel is taken by now leaves the pixel FIFO, and
    // its corners count from now on
    if (consuming && curTick() >= beatEnd) {
        pixels.pop();
        consuming = false;
        cornerCount += beatCorners;
    }
    if (!consuming && taken < cell.size() && pixels.ready())
        takeBeat();
    if (!consuming && !written && curTick() >= cornersDue &&
        corners.room()) {
        writeCorners();
        written = true;
    }
    if (written && corners.done()) {
        DPRINTF(HWPE, "fast: done, %d corners\n", cornerCount);
        active = false;
        jobDone();
        return;
    }
    // Nothing more can be done now. If we wait for time (the beat being
    // taken, or the corner beat's latency) rather than for a stream,
    // schedule computeEvent for then. Waiting for a stream ends with a
    // call of streamProgress().
    Tick when = MaxTick;
    if (consuming)
        when = beatEnd;
    else if (!written && cornersDue != MaxTick)
        when = cornersDue;
    if (when != MaxTick && when > curTick())
        reschedule(computeEvent, when, true);
}

void
HWPEFastCellBestK::serializeJob(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(active);
    SERIALIZE_CONTAINER(cell);
    SERIALIZE_CONTAINER(scores);
    SERIALIZE_SCALAR(taken);
    SERIALIZE_SCALAR(consuming);
    SERIALIZE_SCALAR(beatEnd);
    SERIALIZE_SCALAR(beatCorners);
    SERIALIZE_SCALAR(nextTake);
    SERIALIZE_SCALAR(cornersDue);
    SERIALIZE_SCALAR(written);
    SERIALIZE_SCALAR(cornerCount);
    const Tick compute_tick =
        computeEvent.scheduled() ? computeEvent.when() : MaxTick;
    SERIALIZE_SCALAR(compute_tick);
}

void
HWPEFastCellBestK::unserializeJob(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(active);
    UNSERIALIZE_CONTAINER(cell);
    UNSERIALIZE_CONTAINER(scores);
    UNSERIALIZE_SCALAR(taken);
    UNSERIALIZE_SCALAR(consuming);
    UNSERIALIZE_SCALAR(beatEnd);
    UNSERIALIZE_SCALAR(beatCorners);
    UNSERIALIZE_SCALAR(nextTake);
    UNSERIALIZE_SCALAR(cornersDue);
    UNSERIALIZE_SCALAR(written);
    UNSERIALIZE_SCALAR(cornerCount);
    Tick compute_tick;
    UNSERIALIZE_SCALAR(compute_tick);
    if (compute_tick != MaxTick)
        schedule(computeEvent, compute_tick);
}

} // namespace gem5
