#include "dev/hwpe/hwpe_fast_cell.hh"

#include <algorithm>

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/HWPE.hh"
#include "sim/serialize.hh"

namespace gem5
{

// ---- fastScore ----------------------------------------------------------

uint8_t
fastScore(uint8_t center, const std::array<uint8_t, 16> &circle,
          int threshold)
{
    // The largest arc minimum of e (fastScore.sv's best_arc)
    const auto best_arc = [](const std::array<int, 16> &e) {
        std::array<int, 16> m2, m4, m8;
        for (int k = 0; k < 16; ++k)
            m2[k] = std::min(e[k], e[(k + 1) % 16]);
        for (int k = 0; k < 16; ++k)
            m4[k] = std::min(m2[k], m2[(k + 2) % 16]);
        for (int k = 0; k < 16; ++k)
            m8[k] = std::min(m4[k], m4[(k + 4) % 16]);
        int best = -512;
        for (int k = 0; k < 16; ++k)
            best = std::max(best, std::min(m8[k], e[(k + 8) % 16]));
        return best;
    };
    std::array<int, 16> d, nd;
    for (int k = 0; k < 16; ++k) {
        d[k] = int(center) - int(circle[k]);
        nd[k] = -d[k];
    }
    const int m = std::max(best_arc(d), best_arc(nd));
    return m > threshold ? uint8_t(m - 1) : 0;
}

// ---- fastCellEngine -----------------------------------------------------

namespace
{

// OpenCV's circle order, (dx, dy)
constexpr int CX[16] = {0, 1, 2, 3, 3, 3, 2, 1, 0, -1, -2, -3, -3, -3, -2, -1};
constexpr int CY[16] = {3, 3, 2, 1, 0, -1, -2, -3, -3, -3, -2, -1, 0, 1, 2, 3};

} // anonymous namespace

FastCellEngine::FastCellEngine(unsigned _chunk, int _threshold)
    : chunk(_chunk), threshold(_threshold),
      band(Band, std::vector<uint8_t>(Win + _chunk, 0))
{
}

void
FastCellEngine::start()
{
    _state = E_LOAD;
    nbest = 0;
    cnt = 0;
    y = YFirst;
    c = 0;
    lr = bandLo(YFirst);
}

void
FastCellEngine::clear()
{
    _state = E_IDLE;
    nbest = 0;
    cnt = 0;
    y = 0;
    c = 0;
    lr = 0;
}

bool
FastCellEngine::clock(const uint8_t *beat, bool corner_taken)
{
    // Combinational: the three scores at the window's centre (column c-4)
    uint8_t sc[3];
    for (int r = 0; r < 3; ++r) {
        std::array<uint8_t, 16> circle;
        for (int k = 0; k < 16; ++k)
            circle[k] = band[r + 3 + CY[k]][3 + CX[k]];
        sc[r] = fastScore(band[r + 3][3], circle, threshold);
    }
    const bool col_ok = c >= Win && c <= Cell;
    const bool row_ok[3] = {y != YFirst, true, y != YLast};
    uint8_t masked[3];
    for (int r = 0; r < 3; ++r)
        masked[r] = (col_ok && row_ok[r]) ? sc[r] : 0;

    // Non-max suppression at row y, column c-6, and the place among the best
    const uint8_t s = sw[1][1];
    bool nms_pass = c >= Win + 2 && c <= LastStep;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (!(i == 1 && j == 1) && !(s > sw[i][j]))
                nms_pass = false;
    unsigned pos = 0;
    for (unsigned i = 0; i < K; ++i)
        if (i < nbest && best[i].score >= s)
            ++pos;
    const bool keep = nms_pass && pos < K;
    const Corner cand{s, uint8_t(y), uint8_t(c - 6)};

    bool took = false;
    switch (_state) {
      case E_LOAD:
        if (beat) {
            // The chunk's bytes of the word: chunk c / chunk of the row
            const unsigned rep = (unsigned(c) / chunk) % (WordBytes / chunk);
            for (unsigned i = 0; i < chunk; ++i)
                band[lr][Win + i] = beat[rep * chunk + i];
            took = true;
            if (lr == lrHi())
                _state = E_STEP;
            else
                ++lr;
        }
        break;
      case E_STEP: {
        if (col_ok && sc[1])
            ++cnt;
        if (keep) {
            for (int i = K - 1; i > int(pos); --i)
                best[i] = best[i - 1];
            best[pos] = cand;
            if (nbest < K)
                ++nbest;
        }
        for (int r = 0; r < 3; ++r) {
            sw[r][0] = sw[r][1];
            sw[r][1] = sw[r][2];
            sw[r][2] = masked[r];
        }
        for (auto &row : band)
            std::rotate(row.begin(), row.begin() + 1, row.end());
        if (c == LastStep) {
            c = 0;
            if (y == YLast) {
                _state = nbest ? E_OUT : E_DONE;
            } else {
                ++y;
                lr = bandLo(y);
                _state = E_LOAD;
            }
        } else {
            if (c < Cell - 1 && unsigned(c) % chunk == chunk - 1) {
                lr = bandLo(y);
                _state = E_LOAD;
            }
            ++c;
        }
        break;
      }
      case E_OUT:
        if (corner_taken)
            _state = E_DONE;
        break;
      default:
        break;
    }
    return took;
}

void
FastCellEngine::cornerBeat(std::vector<uint8_t> &data,
                           std::vector<bool> &enables) const
{
    data.assign(WordBytes, 0);
    enables.assign(WordBytes, false);
    for (unsigned i = 0; i < K; ++i) {
        const uint32_t word = uint32_t(best[i].score) << 12 |
                              uint32_t(best[i].row) << 6 | best[i].col;
        for (int b = 0; b < 4; ++b) {
            data[4 * i + b] = uint8_t(word >> (8 * b));
            enables[4 * i + b] = i < nbest;
        }
    }
}

// ---- the top ------------------------------------------------------------

HWPEFastCell::HWPEFastCell(const Params &p)
    : HWPEBase(p),
      chunk(p.chunk),
      sourceStartLatency(p.source_start_latency),
      rowRestartLatency(p.row_restart_latency),
      sinkStartLatency(p.sink_start_latency),
      sinkDoneLatency(p.sink_done_latency),
      engine(p.chunk, p.threshold),
      pix(streamer.addSource("pix", p.pix_depth)),
      corner(streamer.addSink("corner", 1)),
      cycleEvent([this] {
                     cycle = curCycle();
                     eval();
                     if (active)
                         schedule(cycleEvent, clockEdge(Cycles(1)));
                 },
                 name() + ".cycle", false, Event::Default_Pri - 1)
{
    fatal_if(chunk < 1 || FastCellEngine::WordBytes % chunk != 0 ||
             (chunk & (chunk - 1)) != 0,
             "%s: chunk %d must be a power of two dividing 32", name(),
             chunk);
    // The engine is idle: not ready for pixels
    pix.setReady(false);
}

void
HWPEFastCell::startJob(const std::vector<uint32_t> &io_regs)
{
    imagePtr = io_regs[IMAGE_PTR];
    cornersPtr = io_regs[CORNERS_PTR];
    DPRINTF(HWPE, "fast-cell: IMAGE_PTR %#x, CORNERS_PTR %#x\n", imagePtr,
            cornersPtr);
    // STARTING -> WORKING: start_i to the engine, req_start to the source
    engine.start();
    srcY = FastCellEngine::YFirst;
    srcBusy = false;
    sinkStarted = false;
    sinkPushed = false;
    sinkDoneCycle = Never;
    active = true;
    if (atomicMode()) {
        cycle = 0;
        srcStartCycle = sourceStartLatency;
        pix.setReady(true);
        runAtomic();
    } else {
        cycle = curCycle();
        srcStartCycle = cycle + sourceStartLatency;
        pix.setReady(true);
        schedule(cycleEvent, clockEdge(Cycles(1)));
    }
}

void
HWPEFastCell::startRow()
{
    // The band rows of srcY in the cell: from max(srcY - 4, 0), 8 at the
    // first and last row, else 9; per row d0 the band rows, d1 the chunks
    // of a word (the same word again), d2 the row's two words
    const int r0 = srcY == FastCellEngine::YFirst ? 0 : srcY - 4;
    const unsigned rows = (srcY == FastCellEngine::YFirst ||
                           srcY == FastCellEngine::YLast)
                              ? FastCellEngine::Band - 1
                              : FastCellEngine::Band;
    AddressGenConfig config;
    config.base = imagePtr + uint32_t(r0) * FrameW;
    config.totLen = rows * (FastCellEngine::Cell / chunk);
    config.dims = 3;
    config.d0Len = rows;
    config.d0Stride = FrameW;
    config.d1Len = FastCellEngine::WordBytes / chunk;
    config.d1Stride = 0;
    config.d2Stride = FastCellEngine::WordBytes;
    DPRINTF(HWPE, "fast-cell: row %d, %d band rows from %#x\n", srcY, rows,
            config.base);
    pix.start(config, FastCellEngine::WordBytes);
}

bool
HWPEFastCell::eval()
{
    if (!active)
        return false;
    const uint64_t ended = cycle - 1;   // the cycle whose edge this is
    const FastCellEngine::State prev = engine.state();

    // The engine's always_ff, with the stream's beat and the sink's
    // handshake of the cycle that ended
    const uint8_t *beat = pix.ready() ? pix.front().data() : nullptr;
    const bool corner_taken = sinkPushed && corner.room();
    if (engine.clock(beat, corner_taken)) {
        pix.pop();
        if (pix.exhausted()) {
            // The source's done: the top steps src_y and starts it again
            srcBusy = false;
            ++srcY;
            srcStartCycle = ended + rowRestartLatency + sourceStartLatency;
        }
    }

    // The sink: req_start while the engine is in OUT, its write request
    // sink_start_latency later
    if (engine.state() == FastCellEngine::E_OUT && !sinkStarted) {
        sinkStarted = true;
        sinkPushCycle = cycle + sinkStartLatency;
    }
    if (sinkStarted && !sinkPushed && cycle >= sinkPushCycle) {
        AddressGenConfig config;
        config.base = cornersPtr;
        config.totLen = 1;
        config.d0Stride = FastCellEngine::WordBytes;
        corner.start(config, FastCellEngine::WordBytes);
        std::vector<uint8_t> data;
        std::vector<bool> enables;
        engine.cornerBeat(data, enables);
        corner.pushMasked(data.data(), enables);
        sinkPushed = true;
        DPRINTF(HWPE, "fast-cell: %d corners to %#x\n", engine.corners(),
                cornersPtr);
    }
    // The write is answered the cycle after its grant
    if (sinkPushed && corner.done() && sinkDoneCycle == Never)
        sinkDoneCycle = cycle - 2 + sinkDoneLatency;

    // The source for the next row
    if (!srcBusy && srcY <= FastCellEngine::YLast &&
        cycle >= srcStartCycle) {
        startRow();
        srcBusy = true;
    }

    // WORKING -> FINISHED: the engine was done in the cycle that ended, and
    // the sink, if started, seen done
    if (prev == FastCellEngine::E_DONE &&
        (!sinkStarted || sinkDoneCycle <= ended)) {
        countQ = engine.count();
        active = false;
        pix.setReady(false);
        DPRINTF(HWPE, "fast-cell: done, %d corners, best %d\n", countQ,
                engine.corners());
        jobDone();
        return false;
    }

    pix.setReady(engine.ready());

    // Atomic mode: go on unless only a stream can change something
    const bool wait_pix = engine.ready() && !pix.ready() && srcBusy;
    const bool wait_sink = sinkPushed && !corner.done();
    return !(wait_pix || wait_sink);
}

void
HWPEFastCell::runAtomic()
{
    if (inEval) {
        evalAgain = true;
        return;
    }
    inEval = true;
    do {
        evalAgain = false;
        while (active) {
            ++cycle;
            if (!eval())
                break;
        }
    } while (evalAgain && active);
    inEval = false;
}

void
HWPEFastCell::streamProgress()
{
    // With a timing CPU the engine runs on the clock (cycleEvent)
    if (atomicMode() && active)
        runAtomic();
}

void
HWPEFastCell::clearJob()
{
    // clear_o: the top, the engine and count_q
    active = false;
    if (cycleEvent.scheduled())
        deschedule(cycleEvent);
    engine.clear();
    countQ = 0;
    srcBusy = false;
    sinkStarted = false;
    sinkPushed = false;
    sinkDoneCycle = Never;
    pix.setReady(false);
}

// Checkpoints are taken drained, between jobs: only COUNT is kept
void
HWPEFastCell::serializeJob(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(countQ);
}

void
HWPEFastCell::unserializeJob(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(countQ);
}

} // namespace gem5
