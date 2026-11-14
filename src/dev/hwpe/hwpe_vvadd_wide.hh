#ifndef __DEV_HWPE_HWPE_VVADD_WIDE_HH__
#define __DEV_HWPE_HWPE_VVADD_WIDE_HH__

#include <cstdint>
#include <deque>
#include <vector>

#include "dev/hwpe/hwpe_base.hh"
#include "params/HWPEVvaddWide.hh"

namespace gem5
{

// vvadd-wide: C[i] = A[i] + B[i], elems elements per TCDM access (see
// HWPE.py). Streams: sources a and b, sink c.
class HWPEVvaddWide : public HWPEBase
{
  public:
    PARAMS(HWPEVvaddWide);
    HWPEVvaddWide(const Params &p);

  protected:
    void startJob(const std::vector<uint32_t> &io_regs) override;
    void clearJob() override;
    void streamProgress() override { compute(); }
    void serializeJob(CheckpointOut &cp) const override;
    void unserializeJob(CheckpointIn &cp) override;

  private:
    // Job register indices (register n is at offset 0x40 + 4n)
    enum { A_PTR, B_PTR, C_PTR, LEN };

    // A beat of c being computed: it waits here until compute_latency has
    // passed and c has room
    struct Result
    {
        std::vector<uint8_t> data;
        unsigned bytes;       // valid bytes: 4 per element
        Tick due;             // tick from which it may be pushed to c
    };

    const unsigned elems;
    const Cycles computeLatency;
    const Cycles computeInterval;
    Source &a;
    Source &b;
    Sink &c;

    bool active = false;      // a job is running
    uint32_t remaining = 0;   // elements not yet taken from a and b
    // Earliest tick at which the next pair of beats may be taken from a
    // and b (compute_interval)
    Tick nextTake = 0;
    std::deque<Result> results;   // oldest first

    // Calls compute() when it waits for time rather than for a stream
    EventFunctionWrapper computeEvent;

    // Does everything that can be done at this moment: pushes finished
    // results to c, takes beats from a and b and adds them, and calls
    // jobDone() once all of c is written. Called on every stream change
    // (streamProgress()) and by computeEvent.
    void compute();
};

} // namespace gem5

#endif // __DEV_HWPE_HWPE_VVADD_WIDE_HH__
