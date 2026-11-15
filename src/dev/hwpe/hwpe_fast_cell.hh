#ifndef __DEV_HWPE_HWPE_FAST_CELL_HH__
#define __DEV_HWPE_HWPE_FAST_CELL_HH__

#include <array>
#include <cstdint>
#include <vector>

#include "dev/hwpe/hwpe_base.hh"
#include "params/HWPEFastCell.hh"

namespace gem5
{

// fastScore (arcsim-xcel): the FAST-9 score of a pixel from its 16-pixel
// circle in OpenCV's order, with the RTL's arithmetic: d_k = center -
// circle_k; A = the largest, over the 16 arcs of 9, of the arc's smallest d
// (a tree of minima over 2, 4, 8 and 9); B the same of -d; M = max(A, B).
// A corner when M > threshold, its score M - 1; else 0.
uint8_t fastScore(uint8_t center, const std::array<uint8_t, 16> &circle,
                  int threshold);

// fastCellEngine (arcsim-xcel): the band of pixel rows y-4 .. y+4, each a
// 7-column window and a chunk-byte buffer; three fastScore units (rows y-1,
// y, y+1); a 3x3 window of scores; non-max suppression; the best K = 3; the
// corner count. clock() is one rising clock edge: the RTL's always_ff with
// the inputs of the cycle that ends.
class FastCellEngine
{
  public:
    enum State { E_IDLE, E_LOAD, E_STEP, E_OUT, E_DONE };

    struct Corner
    {
        uint8_t score = 0;
        uint8_t row = 0;
        uint8_t col = 0;
    };

    static constexpr int Cell = 64;
    static constexpr int Band = 9;
    static constexpr int Win = 7;
    static constexpr int K = 3;
    static constexpr int YFirst = 3;
    static constexpr int YLast = Cell - 4;
    static constexpr int WordBytes = 32;
    static constexpr int LastStep = Cell + 2;   // the row's last step (66)

    FastCellEngine(unsigned chunk, int threshold);

    // start_i: a new job
    void start();
    // clear_i
    void clear();
    // The edge at the end of a cycle. beat is the stream's data in that
    // cycle (nullptr: not valid); corner_taken: the sink took the corner
    // beat in it. Returns whether the engine took the beat.
    bool clock(const uint8_t *beat, bool corner_taken);

    State state() const { return _state; }
    bool ready() const { return _state == E_LOAD; }      // pix_i.ready
    uint32_t count() const { return cnt; }               // count_o
    unsigned corners() const { return nbest; }
    // corner_o's beat: one word {12'b0, score, row, col} per best corner,
    // with byte enables (strb) on those words only
    void cornerBeat(std::vector<uint8_t> &data,
                    std::vector<bool> &enables) const;

  private:
    const unsigned chunk;
    const int threshold;

    State _state = E_IDLE;
    int y = 0;          // row of the corners
    int c = 0;          // step in the row (column c shifts in)
    int lr = 0;         // band row the next word goes to
    uint32_t cnt = 0;
    // [row][0 .. 6] window, oldest column first; [7 ..] the chunk buffer
    std::vector<std::vector<uint8_t>> band;
    uint8_t sw[3][3] = {};   // scores of rows y-1 .. y+1, oldest column first
    std::array<Corner, K> best{};
    unsigned nbest = 0;

    static int bandLo(int yy) { return yy == YFirst ? 1 : 0; }
    int lrHi() const { return y == YLast ? Band - 2 : Band - 1; }
};

// arcsim-xcel's fast-with-scoring-wide-multicycle-cell-best-k (see
// HWPE.py): the top's job FSM, its per-row source sequencing and the sink
// start, around FastCellEngine; HWPEBase is its hwpe_ctrl_slave.
class HWPEFastCell : public HWPEBase
{
  public:
    PARAMS(HWPEFastCell);
    HWPEFastCell(const Params &p);

  protected:
    void startJob(const std::vector<uint32_t> &io_regs) override;
    void clearJob() override;
    uint32_t extFlags() const override { return countQ; }
    void streamProgress() override;
    void serializeJob(CheckpointOut &cp) const override;
    void unserializeJob(CheckpointIn &cp) override;

  private:
    enum { IMAGE_PTR, CORNERS_PTR };
    static constexpr int FrameW = 320;
    static constexpr uint64_t Never = ~uint64_t(0);

    const unsigned chunk;
    const Cycles sourceStartLatency;
    const Cycles rowRestartLatency;
    const Cycles sinkStartLatency;
    const Cycles sinkDoneLatency;

    FastCellEngine engine;
    Source &pix;
    Sink &corner;

    uint32_t imagePtr = 0;
    uint32_t cornersPtr = 0;
    uint32_t countQ = 0;          // COUNT (0x18): count_q

    bool active = false;          // top in WORKING
    // The cycle the next eval() computes the state of. With a timing CPU
    // the clock cycle; in atomic mode a count of evaluations.
    uint64_t cycle = 0;
    int srcY = 0;                 // src_y
    bool srcBusy = false;         // src_busy
    uint64_t srcStartCycle = 0;   // when the source makes its first read
    bool sinkStarted = false;     // sink_started
    bool sinkPushed = false;      // the corner beat is in the sink
    uint64_t sinkPushCycle = 0;   // when the sink's write request is made
    uint64_t sinkDoneCycle = Never;   // from when the top sees sink_done

    bool inEval = false;          // atomic mode: runAtomic() is running
    bool evalAgain = false;

    EventFunctionWrapper cycleEvent;

    // One clock edge: the state of cycle 'cycle' from that of the one
    // before. Returns false when nothing can change until a stream does
    // (atomic mode) or the job is done.
    bool eval();
    void startRow();
    // Atomic mode: eval() until it waits for a stream or the job is done
    void runAtomic();
};

} // namespace gem5

#endif // __DEV_HWPE_HWPE_FAST_CELL_HH__
