#ifndef __DEV_HWPE_HWPE_BASE_HH__
#define __DEV_HWPE_HWPE_BASE_HH__

#include <cstdint>
#include <memory>
#include <set>
#include <vector>

#include "dev/intpin.hh"
#include "dev/io_device.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "params/HWPEBase.hh"
#include "sim/eventq.hh"

namespace gem5
{

// The shell and standard control every HWPE shares (see HWPE.py). A
// subclass gives the job: startJob() starts it, and the subclass calls
// jobDone() once its last TCDM access has completed.
class HWPEBase : public BasicPioDevice
{
  public:
    PARAMS(HWPEBase);
    HWPEBase(const Params &p);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
    void init() override;
    DrainState drain() override;
    void drainResume() override;
    // A subclass with job state of its own extends these, calling them
    // first. A checkpoint is taken drained: no TCDM access is outstanding.
    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

  protected:
    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;

    // The job. io_regs are the job registers of the job's context; the
    // subclass copies what it needs, as software may write them while the
    // job runs.
    virtual void startJob(const std::vector<uint32_t> &io_regs) = 0;
    void jobDone();
    // A soft clear dropped the running job. Its outstanding TCDM accesses
    // are not answered to the subclass (see clearedTcdmResp()).
    virtual void clearJob() {}
    // Draining is over: the subclass may send to the TCDM again.
    virtual void resumeJob() {}

    uint32_t genericReg(unsigned index) const { return generic[index]; }

    // The TCDM. sendTcdm() takes a packet only while canSendTcdm(): the
    // TCDM has not refused the last one and the HWPE is not draining. The
    // packet's response comes back through recvTcdmResp(), and once a
    // refused packet has gone, tcdmRetried() is called.
    bool canSendTcdm() const;
    void sendTcdm(PacketPtr pkt);
    unsigned tcdmOutstanding() const { return outstanding; }
    virtual void recvTcdmResp(PacketPtr pkt);
    virtual void tcdmRetried() {}
    // An access sent before a soft clear: its response, or the request if
    // the TCDM had not taken it yet. The default deletes the packet; a
    // subclass with sender state of its own on its packets pops and frees
    // it first.
    virtual void clearedTcdmResp(PacketPtr pkt) { delete pkt; }

    RequestorID requestorId;

  private:
    // Register offsets (hwpe_ctrl_package)
    enum : Addr
    {
        TRIGGER = 0x00,
        ACQUIRE = 0x04,
        FINISHED = 0x08,
        STATUS = 0x0C,
        RUNNING_JOB = 0x10,
        SOFT_CLEAR = 0x14,
        RESERVED = 0x18,
        SW_EVT = 0x1C,
        GENERIC = 0x20,
        IO = 0x40,
        WINDOW_END = 0x100,
    };
    static constexpr unsigned MaxContexts = 4;  // one STATUS byte each
    static constexpr unsigned MaxGenericRegs = (IO - GENERIC) / 4;
    static constexpr unsigned MaxIoRegs = (WINDOW_END - IO) / 4;
    // ACQUIRE's answers when it reserves no context
    static constexpr uint32_t AcquireFull = uint32_t(-1);
    static constexpr uint32_t AcquireLocked = uint32_t(-2);

    // Sender state on every TCDM packet: the soft clears before it was sent.
    struct TcdmState : public Packet::SenderState
    {
        uint64_t clears;
        TcdmState(uint64_t _clears) : clears(_clears) {}
    };

    class TcdmPort : public RequestPort
    {
      public:
        TcdmPort(const std::string &name, HWPEBase &hwpe);
        // Send, or hold the packet until the TCDM takes it.
        void send(PacketPtr pkt);
        bool waiting() const { return held != nullptr; }
        // Forget a held packet; returns it.
        PacketPtr drop();

      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override {}

      private:
        HWPEBase &hwpe;
        PacketPtr held = nullptr;
    };

    TcdmPort tcdmPort;
    std::unique_ptr<IntSourcePin<HWPEBase>> irqPin;

    const unsigned numContexts;
    const Cycles startLatency;
    const Cycles doneLatency;

    std::vector<uint32_t> generic;
    std::vector<std::vector<uint32_t>> io;  // [context][register]
    unsigned pointerContext = 0;            // the context software programs
    unsigned runningContext = 0;            // the oldest queued job's
    unsigned pending = 0;                   // jobs triggered, not yet done
    bool running = false;                   // started, not yet ended
    bool acquired = false;                  // ACQUIRE read, no TRIGGER yet
    uint8_t jobId = 0;                      // the next ACQUIRE's
    uint8_t doneJobs = 0;                   // RUNNING_JOB
    uint32_t finished = 0;                  // FINISHED

    unsigned outstanding = 0;               // TCDM accesses sent, unanswered
    uint64_t clears = 0;                    // soft clears so far

    EventFunctionWrapper startEvent;
    EventFunctionWrapper doneEvent;
    EventFunctionWrapper irqLowerEvent;

    // Offsets reported as not modelled
    std::set<Addr> warned;

    uint32_t readRegister(Addr offset);
    void writeRegister(Addr offset, uint32_t value, uint32_t mask);
    uint32_t status() const;
    void trigger();
    void softClear(bool registers);
    void start();
    void finish();
    void pulseIrq();
    void unmodelled(Addr offset, bool is_write);

    void recvTcdmRespFromPort(PacketPtr pkt);
    void tcdmRetriedFromPort();
};

} // namespace gem5

#endif // __DEV_HWPE_HWPE_BASE_HH__
