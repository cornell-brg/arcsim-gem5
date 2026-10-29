#include "dev/hwpe/hwpe_lk.hh"

#include <cstring>

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/HWPE.hh"
#include "mem/packet_access.hh"

namespace gem5
{

using of_reference::lkWin;
using of_reference::lkWindow;

HWPELk::HWPELk(const Params &p)
    : HWPEBase(p),
      rotated(p.rotated),
      side(p.rotated ? of_reference::lkRotSide : of_reference::lkBlock),
      bShiftParam(p.b_shift),
      pointCycles(p.point_cycles),
      templateCycles(p.template_cycles),
      rejectCycles(p.reject_cycles),
      mismatchCycles(p.mismatch_cycles),
      endCycles(p.end_cycles),
      stepEvent([this] { auto f = std::move(next); next = nullptr; f(); },
                name() + ".step")
{}

void
HWPELk::at(Cycles n, std::function<void()> f)
{
    // n cycles from the start of the current cycle: a response that
    // arrives within a cycle is that cycle's
    const Tick edge = clockEdge();
    const Tick cycle = edge == curTick() ? edge : edge - clockPeriod();
    next = std::move(f);
    schedule(stepEvent, cycle + cyclesToTicks(n));
}

void
HWPELk::send(PacketPtr pkt)
{
    if (canSendTcdm())
        sendTcdm(pkt);
    else
        retryPkt = pkt;
}

void
HWPELk::tcdmRetried()
{
    if (retryPkt && canSendTcdm()) {
        PacketPtr pkt = retryPkt;
        retryPkt = nullptr;
        sendTcdm(pkt);
    }
}

void
HWPELk::readWord(Addr addr, std::function<void()> then)
{
    afterRead = std::move(then);
    auto req = std::make_shared<Request>(addr, 4, 0, requestorId);
    PacketPtr pkt = new Packet(req, MemCmd::ReadReq);
    pkt->allocate();
    send(pkt);
}

void
HWPELk::recvTcdmResp(PacketPtr pkt)
{
    const bool is_read = pkt->isRead();
    if (is_read)
        readData = pkt->getLE<uint32_t>();
    delete pkt;
    if (is_read) {
        auto f = std::move(afterRead);
        afterRead = nullptr;
        f();
    }
}

int
HWPELk::reflect(int i, int n)
{
    if (n == 1)
        return 0;
    for (int pass = 0; pass < 4; ++pass) {
        if (i < 0)
            i = -i;
        else if (i >= n)
            i = 2 * n - 2 - i;
    }
    return i;
}

bool
HWPELk::outside(int ix, int iy) const
{
    return ix < -lkWin || ix >= int(levW[level]) || iy < -lkWin ||
           iy >= int(levH[level]);
}

void
HWPELk::startJob(const std::vector<uint32_t> &r)
{
    listPtr = r[LIST_PTR];
    nPoints = r[N_POINTS] & 0xff;
    patchBase = r[PATCH_BASE];
    minEig = r[MIN_EIG];
    eps2 = r[EPS2];
    maxIter = r[MAX_ITER] & 0x3f;
    bShift = bShiftParam;
    if (rotated) {
        fatal_if(r.size() <= B_SHIFT, "%s: rotated needs num_io_regs 19",
                 name());
        map[0] = int16_t(r[MAP_A] & 0xffff);
        map[1] = int16_t(r[MAP_A] >> 16);
        map[2] = int16_t(r[MAP_B] & 0xffff);
        map[3] = int16_t(r[MAP_B] >> 16);
        bShift = r[B_SHIFT] & 3;
    }
    for (unsigned l = 0; l < Levels; ++l) {
        levBase[l] = r[L0_BASE + 3 * l];
        levW[l] = r[L0_SIZE + 3 * l] & 0xffff;
        levH[l] = r[L0_SIZE + 3 * l] >> 16;
        levHi[l] = r[L0_HI + 3 * l] & 0xffff;
    }
    record = 0;
    processed = notDone = 0;
    recWord = 0;
    DPRINTF(HWPE, "lk: %d points\n", nPoints);
    if (nPoints == 0) {
        at(Cycles(1), [this] { jobDone(); });
        return;
    }
    readRecord();
}

void
HWPELk::clearJob()
{
    if (stepEvent.scheduled())
        deschedule(stepEvent);
    next = nullptr;
    afterRead = nullptr;
    if (retryPkt) {
        delete retryPkt;
        retryPkt = nullptr;
    }
}

// lkTop's REC_REQ and REC_RSP, then POINT_START and lkPoint's IDLE
void
HWPELk::readRecord()
{
    readWord(listPtr + 32 * record + 4 * recWord, [this] {
        rec[recWord] = readData;
        if (++recWord < 5) {
            at(Cycles(1), [this] { readRecord(); });
            return;
        }
        recWord = 0;
        const int32_t gx = rec[3], gy = rec[4];
        level = Levels - 1;
        x = gx >> (Levels - 1);
        y = gy >> (Levels - 1);
        st = done = true;
        cacheValid = false;
        for (unsigned l = 0; l < Levels; ++l) {
            iters[l] = 0;
            stops[l] = Converged;
        }
        at(pointCycles, [this] { levelStart(); });
    });
}

void
HWPELk::levelStart()
{
    const int32_t px0 = rec[1], py0 = rec[2];
    px = (px0 >> level) - Half;
    py = (py0 >> level) - Half;
    if (outside(px >> 14, py >> 14)) {
        stops[level] = TemplateOutside;
        if (level == 0)
            st = false;
        at(Cycles(1), [this] { levelEnd(); });
        return;
    }
    word = 0;
    at(Cycles(1), [this] { readPatch(); });
}

void
HWPELk::readPatch()
{
    const uint32_t slot = rec[0] & 0xff;
    const Addr base = patchBase + (slot * 3 + level) * side * side;
    readWord(base + 4 * word, [this] {
        std::memcpy(&block[4 * word], &readData, 4);
        if (++word < side * side / 4) {
            at(Cycles(1), [this] { readPatch(); });
            return;
        }
        const int32_t px0 = rec[1], py0 = rec[2];
        const bool accept = rotated ?
            tmpl.makeMapped(block, px0 >> level, py0 >> level, map,
                            levW[level], levH[level], minEig, bShift) :
            tmpl.make(block, px, py, levW[level], levH[level], minEig,
                      bShift);
        if (!accept) {
            stops[level] = Rejected;
            if (level == 0)
                st = false;
            at(rejectCycles, [this] { levelEnd(); });
            return;
        }
        qx = x - Half;
        qy = y - Half;
        pdx = pdy = 0;
        j = 0;
        at(templateCycles, [this] { iterStart(); });
    });
}

void
HWPELk::iterStart()
{
    const int ix = qx >> 14, iy = qy >> 14;
    if (j == maxIter) {
        stops[level] = MaxIter;
        at(Cycles(1), [this] { levelEnd(); });
        return;
    }
    if (outside(ix, iy)) {
        stops[level] = Left;
        if (level == 0)
            st = false;
        at(Cycles(1), [this] { levelEnd(); });
        return;
    }
    const int h = levH[level];
    const int first_row = iy > 0 ? iy : 0;
    const int reach = std::max(iy + lkWin, -iy);
    const int last_row = std::min(reach, h - 1);
    if (last_row >= int(levHi[level]) || first_row < 0) {
        stops[level] = Beyond;
        done = false;
        at(Cycles(1), [this] { finished(); });
        return;
    }
    pix = 0;
    at(Cycles(1), [this] { winPixel(); });
}

// WIN_PIXEL: the window's pixels from the last word read, one a cycle,
// until one needs another word (WIN_REQ, WIN_RSP) or the last is taken
void
HWPELk::winPixel()
{
    const int ix = qx >> 14, iy = qy >> 14;
    const int w = levW[level], h = levH[level];
    unsigned hits = 0;
    while (true) {
        const int wy = reflect(iy + int(pix / lkWindow), h);
        const int wx = reflect(ix + int(pix % lkWindow), w);
        const uint32_t addr = levBase[level] + wy * w + wx;
        if (!cacheValid || cacheAddr != (addr & ~3u)) {
            const uint32_t word_addr = addr & ~3u;
            at(Cycles(hits + 1), [this, word_addr] {
                readWord(word_addr, [this, word_addr] {
                    cacheAddr = word_addr;
                    cacheData = readData;
                    cacheValid = true;
                    at(Cycles(1), [this] { winPixel(); });
                });
            });
            return;
        }
        window[pix] = cacheData >> (8 * (addr & 3));
        ++hits;
        if (++pix == WindowPx)
            break;
    }
    // lkMismatch and lkStep, then STEP's stop tests
    int64_t b1, b2;
    tmpl.mismatch(window, qx, qy, b1, b2);
    const auto s = tmpl.step(b1, b2, pdx, pdy, j == 0, eps2, bShift);
    qx += s.dx;
    qy += s.dy;
    x = qx + Half;
    y = qy + Half;
    ++iters[level];
    const Cycles after(hits - 1 + mismatchCycles);
    if (s.converged) {
        stops[level] = Converged;
        at(after, [this] { levelEnd(); });
    } else if (s.oscillating) {
        x -= s.dx >> 1;
        y -= s.dy >> 1;
        stops[level] = Backoff;
        at(after, [this] { levelEnd(); });
    } else {
        pdx = s.dx;
        pdy = s.dy;
        ++j;
        at(after, [this] { iterStart(); });
    }
}

void
HWPELk::levelEnd()
{
    if (level == 0) {
        if (outside((x - Half) >> 14, (y - Half) >> 14))
            st = false;
        at(Cycles(1), [this] { finished(); });
        return;
    }
    --level;
    x *= 2;
    y *= 2;
    at(Cycles(1), [this] { levelStart(); });
}

// lkPoint's FINISHED, then lkTop's RESULT: x, y and the flags into words
// 5..7 of the record
void
HWPELk::finished()
{
    at(Cycles(1), [this] {
        uint32_t flags = (st ? 1u : 0u) | (done ? 2u : 0u) | (1u << 31);
        for (unsigned l = 0; l < Levels; ++l) {
            flags |= (stops[l] & 7) << (2 + 3 * l);
            flags |= (iters[l] & 0x3f) << (11 + 6 * l);
        }
        const uint32_t words[3] = {uint32_t(x), uint32_t(y), flags};
        auto req = std::make_shared<Request>(listPtr + 32 * record + 20, 12,
                                             0, requestorId);
        PacketPtr pkt = new Packet(req, MemCmd::WriteReq);
        pkt->allocate();
        pkt->setData(reinterpret_cast<const uint8_t *>(words));
        send(pkt);
        ++processed;
        if (!done)
            ++notDone;
        if (++record == nPoints) {
            at(endCycles, [this] { jobDone(); });
            return;
        }
        at(Cycles(1), [this] { readRecord(); });
    });
}

uint32_t
HWPELk::extFlags() const
{
    return (notDone & 0xff) << 8 | (processed & 0xff);
}

DrainState
HWPELk::drain()
{
    if (stepEvent.scheduled() || afterRead || retryPkt)
        return DrainState::Draining;
    return HWPEBase::drain();
}

} // namespace gem5
