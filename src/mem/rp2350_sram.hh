#ifndef __MEM_RP2350_SRAM_HH__
#define __MEM_RP2350_SRAM_HH__

#include <array>
#include <deque>
#include <list>
#include <memory>
#include <vector>

#include "base/statistics.hh"
#include "mem/abstract_mem.hh"
#include "params/RP2350SRAM.hh"
#include "sim/eventq.hh"

namespace gem5::memory
{

class RP2350SRAM : public AbstractMemory
{
  private:
    struct Transaction
    {
        PacketPtr pkt;
        PortID port;
        Tick ready;
        unsigned granted = 0;
        unsigned completed = 0;
        bool needsResponse;
    };
    using TransactionPtr = std::shared_ptr<Transaction>;
    struct Beat
    {
        TransactionPtr txn;
        unsigned offset;
        unsigned size;
        Tick due;
    };

    class MemoryPort : public ResponsePort
    {
      public:
        RP2350SRAM &memory;
        const unsigned priority;
        bool retryReq = false;
        bool retryResp = false;
        unsigned outstanding = 0;
        std::deque<TransactionPtr> requests;
        std::deque<TransactionPtr> responses;

        MemoryPort(const std::string &name, RP2350SRAM &mem, PortID id,
                   unsigned priority);
        AddrRangeList getAddrRanges() const override;
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
    };

    std::vector<std::unique_ptr<MemoryPort>> ports;
    std::array<PortID, 10> lastWinner;
    // When each bank can grant its next beat: one per cycle
    std::array<Tick, 10> bankFree;
    std::deque<Beat> beats;
    std::list<TransactionPtr> active;
    const Tick window;
    const Tick latency;
    const unsigned queueDepth;
    // Runs after everything else of its tick, so that every request of a
    // cycle is in before the banks choose.
    EventFunctionWrapper arbitrateEvent;
    EventFunctionWrapper completeEvent;
    // Defer destruction of no-response requests until their sender has
    // returned from sendTimingReq (the standard gem5 ownership convention).
    std::unique_ptr<Packet> pendingDelete;

    struct SRAMStats : public statistics::Group
    {
        statistics::Vector grants;
        statistics::Vector contested;
        statistics::Vector waitCycles;
        statistics::Vector instructionGrants;
        statistics::Scalar requestRetries;
        statistics::Scalar responseRetries;
        statistics::Scalar splitPackets;
        SRAMStats(RP2350SRAM &memory);
    } sramStats;

    unsigned bankFor(Addr address) const;
    unsigned beatSize(Addr address, unsigned remaining) const;
    void validate(PacketPtr pkt) const;
    bool receive(PacketPtr pkt, PortID id);
    void functional(PacketPtr pkt);
    void complete(const Beat &beat);
    void sendResponses(MemoryPort &port);
    void retire(const TransactionPtr &txn);
    void arbitrate();
    void arbitrateAt(Tick when);
    void completeBeats();
    bool idle() const;

  public:
    explicit RP2350SRAM(const RP2350SRAMParams &p);
    Port &getPort(const std::string &name, PortID id = InvalidPortID) override;
    void init() override;
    DrainState drain() override;
};

} // namespace gem5::memory
#endif
