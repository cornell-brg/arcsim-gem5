#ifndef __DEV_HWPE_HWPE_LK_HH__
#define __DEV_HWPE_HWPE_LK_HH__

#include <cstdint>
#include <functional>

#include "dev/hwpe/hwpe_base.hh"
#include "dev/hwpe/of_reference.hh"
#include "params/HWPELk.hh"

namespace gem5
{

// arcsim-xcel's lkTop, the LK engine (see HWPE.py): a job's point records
// tracked one at a time through lkPoint's states, a 32-bit TCDM read at a
// time.
class HWPELk : public HWPEBase
{
  public:
    PARAMS(HWPELk);
    HWPELk(const Params &p);

    DrainState drain() override;

  protected:
    void startJob(const std::vector<uint32_t> &io_regs) override;
    void clearJob() override;
    void recvTcdmResp(PacketPtr pkt) override;
    void tcdmRetried() override;
    uint32_t extFlags() const override;

  private:
    // Job register indices (register n is at offset 0x40 + 4n)
    enum
    {
        LIST_PTR, N_POINTS, PATCH_BASE, MIN_EIG, EPS2, MAX_ITER,
        L0_BASE, L0_SIZE, L0_HI
    };
    static constexpr unsigned Levels = 3;
    static constexpr unsigned SlotBytes = 3 * of_reference::lkBlock *
                                          of_reference::lkBlock;
    static constexpr unsigned PatchWords = of_reference::lkBlock *
                                           of_reference::lkBlock / 4;
    static constexpr unsigned WindowPx = of_reference::lkWindow *
                                         of_reference::lkWindow;
    static constexpr int32_t Half = (of_reference::lkWin - 1) << 13;

    enum Stop : uint32_t
    {
        Converged, Backoff, MaxIter, Left, TemplateOutside, Rejected, Beyond
    };

    const int bShift;
    const Cycles pointCycles;     // the last record word to LEVEL_START
    const Cycles templateCycles;  // the last patch word to ITER_START
    const Cycles rejectCycles;    // the last patch word to LEVEL_END
    const Cycles mismatchCycles;  // the last window pixel to ITER_START
    const Cycles endCycles;       // FINISHED to the next record read

    // The job
    uint32_t listPtr = 0, nPoints = 0, patchBase = 0;
    int64_t minEig = 0, eps2 = 0;
    unsigned maxIter = 0;
    uint32_t levBase[Levels] = {}, levW[Levels] = {}, levH[Levels] = {},
             levHi[Levels] = {};
    unsigned record = 0;          // of the job
    uint32_t processed = 0, notDone = 0;

    // The point (lkPoint's registers)
    uint32_t rec[5] = {};
    unsigned recWord = 0;
    int level = 0;
    int32_t x = 0, y = 0, px = 0, py = 0, qx = 0, qy = 0, pdx = 0, pdy = 0;
    unsigned j = 0;
    bool st = true, done = true;
    uint32_t iters[Levels] = {}, stops[Levels] = {};
    uint8_t block[of_reference::lkBlock * of_reference::lkBlock] = {};
    uint8_t window[WindowPx] = {};
    unsigned word = 0, pix = 0;
    uint32_t cacheAddr = 0, cacheData = 0;
    bool cacheValid = false;
    of_reference::LkTemplate tmpl;

    // A read in flight and what follows its response
    std::function<void()> afterRead;
    PacketPtr retryPkt = nullptr;
    uint32_t readData = 0;

    EventFunctionWrapper stepEvent;
    std::function<void()> next;

    // Run f n cycles after the current cycle
    void at(Cycles n, std::function<void()> f);
    void readWord(Addr addr, std::function<void()> then);
    void send(PacketPtr pkt);

    void readRecord();
    void levelStart();
    void readPatch();
    void iterStart();
    void winPixel();
    void levelEnd();
    void finished();
    bool outside(int ix, int iy) const;
    static int reflect(int i, int n);
};

} // namespace gem5

#endif // __DEV_HWPE_HWPE_LK_HH__
