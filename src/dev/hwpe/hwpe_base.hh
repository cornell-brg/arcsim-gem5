#ifndef __DEV_HWPE_HWPE_BASE_HH__
#define __DEV_HWPE_HWPE_BASE_HH__

#include <cstdint>
#include <memory>
#include <set>
#include <vector>

#include "dev/hwpe/hwpe_streamer.hh"
#include "dev/intpin.hh"
#include "dev/io_device.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "params/HWPEBase.hh"
#include "sim/eventq.hh"

namespace gem5
{

// Base class of every HWPE (register map: see HWPE.py). It implements
// everything that is the same for all HWPEs: the control registers
// (hwpe_ctrl), the job queue, the IRQ, the TCDM port and the streamer.
// A subclass implements only the computation:
//  - its constructor creates its streams with streamer.addSource() and
//    streamer.addSink();
//  - startJob() is called when a job starts; the subclass starts its
//    streams with the addresses and lengths from the job registers;
//  - streamProgress() is called whenever a stream changes (data arrived,
//    buffer space was freed, a write was acknowledged), so the subclass
//    can check whether it can continue;
//  - once the job's last write is acknowledged, the subclass calls
//    jobDone().
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
    // Checkpoints are only taken after draining, i.e. with no TCDM access
    // outstanding. A subclass adds its own state through serializeJob()
    // and unserializeJob().
    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

  protected:
    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;

    // Called when a job starts. io_regs holds the job registers of the
    // job's context. Software may overwrite them while the job runs, so
    // the subclass must copy the values it needs here.
    virtual void startJob(const std::vector<uint32_t> &io_regs) = 0;
    // Called by the subclass when its job has ended. The base class shows
    // the job as done (STATUS, FINISHED, IRQ) done_latency cycles later.
    void jobDone();
    // Called on every soft clear, whether a job runs or not: the subclass
    // must drop its job state and reset whatever the RTL's clear resets.
    // Responses to TCDM accesses sent before the clear are not passed to
    // recvTcdmResp() but to clearedTcdmResp().
    virtual void clearJob() {}
    // The value of 0x18: what the subclass gives the control slave as
    // ext_flags (hwpe_ctrl_slave's ctrl_i.ext_flags), 0 if nothing
    virtual uint32_t extFlags() const { return 0; }
    // Called when draining ends: the subclass may send TCDM accesses again.
    virtual void resumeJob() {}
    // Called whenever a stream changes: a source received data, or a sink
    // freed buffer space or got a write acknowledged. The subclass checks
    // whether it can continue.
    virtual void streamProgress() {}
    // The subclass's job state, stored in the HWPE's checkpoint section
    virtual void serializeJob(CheckpointOut &cp) const {}
    virtual void unserializeJob(CheckpointIn &cp) {}

    uint32_t genericReg(unsigned index) const { return generic[index]; }

    // Direct TCDM access. The streamer uses these; a subclass needs them
    // only if it makes TCDM accesses of its own besides the streamer's (the
    // default implementations below forward everything to the streamer).
    // sendTcdm() may only be called while canSendTcdm() is true: no packet
    // the TCDM refused is waiting to be resent, and the HWPE is not
    // draining. The response arrives at recvTcdmResp(). If the TCDM refused
    // the packet, tcdmRetried() is called once it has accepted it.
    bool canSendTcdm() const;
    void sendTcdm(PacketPtr pkt);
    unsigned tcdmOutstanding() const { return outstanding; }
    virtual void recvTcdmResp(PacketPtr pkt);
    virtual void tcdmRetried();
    // Called instead of recvTcdmResp() for an access sent before a soft
    // clear. pkt is the response, or the request itself if the TCDM had
    // refused it and not yet accepted it. The callee frees pkt.
    virtual void clearedTcdmResp(PacketPtr pkt);

    RequestorID requestorId;
    Streamer streamer;

  private:
    friend class Streamer;

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
    // What ACQUIRE returns when it cannot reserve a context
    static constexpr uint32_t AcquireFull = uint32_t(-1);
    static constexpr uint32_t AcquireLocked = uint32_t(-2);

    // Attached to every TCDM packet: the value of 'clears' when it was
    // sent. If that differs when the response comes back, a soft clear has
    // dropped the job the access belonged to.
    struct TcdmState : public Packet::SenderState
    {
        uint64_t clears;
        TcdmState(uint64_t _clears) : clears(_clears) {}
    };

    class TcdmPort : public RequestPort
    {
      public:
        TcdmPort(const std::string &name, HWPEBase &hwpe);
        // Send pkt. If the TCDM refuses it, keep it and resend it when
        // the TCDM asks for a retry (recvReqRetry()).
        void send(PacketPtr pkt);
        // A refused packet is waiting to be resent.
        bool waiting() const { return held != nullptr; }
        // Forget the refused packet (on a soft clear) and return it;
        // nullptr if there is none.
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
    // Context that software is programming: the job register accesses
    // and the next TRIGGER go to it
    unsigned pointerContext = 0;
    // Context of the oldest queued job: the one running or starting next
    unsigned runningContext = 0;
    unsigned pending = 0;                   // jobs triggered, not yet done
    bool running = false;                   // a job started, not yet done
    bool acquired = false;                  // ACQUIRE read, no TRIGGER since
    uint8_t jobId = 0;                      // id the next ACQUIRE returns
    uint8_t doneJobs = 0;                   // value of RUNNING_JOB
    uint32_t finished = 0;                  // value of FINISHED

    unsigned outstanding = 0;   // TCDM accesses sent, response not yet back
    uint64_t clears = 0;        // soft clears so far (see TcdmState)

    EventFunctionWrapper startEvent;     // calls start()
    EventFunctionWrapper doneEvent;      // calls finish()
    EventFunctionWrapper irqLowerEvent;  // ends the IRQ pulse

    // Offsets unmodelled() has already warned about
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

    // Called by tcdmPort; they forward to the virtual hooks above
    void recvTcdmRespFromPort(PacketPtr pkt);
    void tcdmRetriedFromPort();
};

} // namespace gem5

#endif // __DEV_HWPE_HWPE_BASE_HH__
