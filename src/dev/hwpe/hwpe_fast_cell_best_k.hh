#ifndef __DEV_HWPE_HWPE_FAST_CELL_BEST_K_HH__
#define __DEV_HWPE_HWPE_FAST_CELL_BEST_K_HH__

#include <cstdint>
#include <vector>

#include "dev/hwpe/hwpe_base.hh"
#include "params/HWPEFastCellBestK.hh"

namespace gem5
{

// fast-with-scoring-wide-multicycle-cell-best-k: FAST on one cell, its best
// k corners after non-max suppression (see HWPE.py). Streams: source
// pixels, sink corners.
class HWPEFastCellBestK : public HWPEBase
{
  public:
    PARAMS(HWPEFastCellBestK);
    HWPEFastCellBestK(const Params &p);

  protected:
    void startJob(const std::vector<uint32_t> &io_regs) override;
    void clearJob() override;
    void streamProgress() override { compute(); }
    uint32_t extFlags() const override { return cornerCount; }
    void serializeJob(CheckpointOut &cp) const override;
    void unserializeJob(CheckpointIn &cp) override;

  private:
    // Job register indices (register n is at offset 0x40 + 4n)
    enum { IMAGE_PTR, CORNERS_PTR };

    const unsigned width;       // of the cell
    const unsigned height;
    const unsigned frameWidth;
    const unsigned n;
    const unsigned threshold;
    const unsigned cycles;
    const unsigned k;
    const unsigned beatBytes;   // bytes, and so pixels, of a beat
    const unsigned colBits;     // of a corner word's col and row
    const unsigned rowBits;
    Source &pixels;
    Sink &corners;

    bool active = false;        // a job is running
    std::vector<uint8_t> cell;  // the cell's pixels, row-major
    // Score of each scored pixel (the cell without its 3-pixel border),
    // row-major; 0 if it is not a corner
    std::vector<uint8_t> scores;
    uint32_t taken = 0;         // pixels taken, with the beat being taken
    // The front beat of pixels is being taken: its last pixel is taken
    // at beatEnd, which pops it, and beatCorners of its scores are corners
    bool consuming = false;
    Tick beatEnd = 0;
    uint32_t beatCorners = 0;
    Tick nextTake = 0;          // earliest tick to take the next pixel
    Tick cornersDue = MaxTick;  // tick the corner beat goes to the sink
    bool written = false;       // the corner beat went to the sink
    uint32_t cornerCount = 0;   // 0x18: corners in the pixels popped

    // Calls compute() when it waits for time rather than for a stream
    EventFunctionWrapper computeEvent;

    // Cycles the engine is busy with the pixel at row, col of the cell
    Cycles pixelCycles(unsigned row, unsigned col) const;
    // Score of the pixel at row, col of the cell; 0 if it is not a corner
    uint8_t score(unsigned row, unsigned col) const;
    // Takes the front beat's pixels and scores the pixels they complete
    void takeBeat();
    // Non-max suppression and the best k, pushed to corners as one beat
    void writeCorners();
    // Does everything that can be done at this moment: pops the beat whose
    // pixels are all taken, takes the next beat, pushes the corner beat,
    // and calls jobDone() once it is written. Called on every stream
    // change (streamProgress()) and by computeEvent.
    void compute();
};

} // namespace gem5

#endif // __DEV_HWPE_HWPE_FAST_CELL_BEST_K_HH__
