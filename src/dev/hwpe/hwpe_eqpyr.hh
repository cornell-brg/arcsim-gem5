#ifndef __DEV_HWPE_HWPE_EQPYR_HH__
#define __DEV_HWPE_HWPE_EQPYR_HH__

#include <array>
#include <deque>
#include <cstdint>
#include <vector>

#include "dev/hwpe/hwpe_base.hh"
#include "params/HWPEEqpyr.hh"

namespace gem5
{

// arcsim-xcel's eqpyrTop, the stripe engine (see HWPE.py). Streams: words
// in (the table or a stripe), levels 0 to 2 and the histogram out.
class HWPEEqpyr : public HWPEBase
{
  public:
    PARAMS(HWPEEqpyr);
    HWPEEqpyr(const Params &p);

    DrainState drain() override;

  protected:
    void startJob(const std::vector<uint32_t> &io_regs) override;
    void clearJob() override;
    void streamProgress() override { compute(); }
    uint32_t extFlags() const override;
    void serializeJob(CheckpointOut &cp) const override;
    void unserializeJob(CheckpointIn &cp) override;

  private:
    // Job register indices (register n is at offset 0x40 + 4n)
    enum
    {
        L0_PTR, L1_PTR, L2_PTR, TABLE_PTR, HIST_PTR, NEW_FRAME, RING_PTR,
        RING_STRIPES
    };
    enum class Phase { Idle, Table, Stripe, Hist };

    const unsigned width;       // of level 0
    const unsigned height;
    const unsigned stripeRows;
    const unsigned beatBytes;   // of the load and the level 0 and 1 stores
    const unsigned l2BeatBytes;
    const Cycles pixelCycles;   // a pixel taken
    const Cycles tableCycles;   // a 32-bit table word taken
    const Cycles histCycles;    // a histogram beat made
    const Cycles tailCycles;    // the last element to the engine idle
    const Cycles outCycles;     // a word's last pixel to its store
    const Cycles pyrCycles;     // added for each level above 0
    const Cycles histLatency;   // the last store to the histogram
    const Cycles frameCycles;   // a NEW_FRAME job's start to its table load

    Source &words;
    Sink &l0Out;
    Sink &l1Out;
    Sink &l2Out;
    Sink &histOut;

    // The frame's state, kept across its jobs (eqpyrTop's latched
    // registers, the equalizer's table and histogram, pyrDown's rows and
    // the packers)
    uint32_t l0Ptr = 0, l1Ptr = 0, l2Ptr = 0, tablePtr = 0, histPtr = 0;
    uint32_t ringPtr = 0, ringStripes = 0, ringSlot = 0;
    uint32_t stripeRow = 0;                 // rows of level 0 made
    std::array<uint8_t, 256> lut{};
    std::array<uint32_t, 256> hist{};
    std::vector<uint8_t> level[3];          // the levels as made so far
    std::vector<uint8_t> pack[3];           // pixels waiting for a word
    uint32_t wordsOut[3] = {0, 0, 0};       // words pushed to each level
    bool histWritten = false;

    // The job
    Phase phase = Phase::Idle;
    uint32_t taken = 0;         // pixels (or table words) of the job taken
    uint32_t total = 0;         // of the job
    Tick nextTake = 0;          // earliest tick to take the next element
    Tick tailDone = MaxTick;    // the engine is idle from then
    uint32_t histBeats = 0;     // histogram beats pushed
    // Words made, waiting for their tick to go to their store, in order
    struct Due
    {
        Tick when;
        unsigned level;
        std::vector<uint8_t> data;
    };
    std::deque<Due> due;
    Tick when = 0;              // the tick of the pixel being made

    // Source rows of the level below whose arrival completes rows (or
    // columns) of a level: completes[l][i] lists the rows of level l + 1
    // that row i of level l completes
    std::vector<std::vector<unsigned>> completesRow[2], completesCol[2];

    EventFunctionWrapper computeEvent;
    EventFunctionWrapper frameEvent;

    unsigned levelWidth(unsigned l) const { return width >> l; }
    unsigned levelHeight(unsigned l) const { return height >> l; }
    uint8_t pyrDown(unsigned l, unsigned y, unsigned x) const;
    // Pixel (y, x) of level l is made: store and pack it, and make the
    // pixels of level l + 1 it completes.
    void make(unsigned l, unsigned y, unsigned x, uint8_t value);
    // Push the words due; false if a store has no room for one
    bool flush();
    void startStores();
    void loadTable();
    void compute();
};

} // namespace gem5

#endif // __DEV_HWPE_HWPE_EQPYR_HH__
